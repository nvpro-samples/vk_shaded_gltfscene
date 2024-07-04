/*
 * Copyright (c) 2019-2021, NVIDIA CORPORATION.  All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-FileCopyrightText: Copyright (c) 2019-2021 NVIDIA CORPORATION
 * SPDX-License-Identifier: Apache-2.0
 */

#version 450
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_NV_fragment_shader_barycentric : enable
#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_EXT_debug_printf : enable


#include "device_host.h"
#include "dh_bindings.h"
#include "nvvkhl/shaders/dh_sky.h"
#include "nvvkhl/shaders/dh_scn_desc.h"
#include "nvvkhl/shaders/func.h"
#include "nvvkhl/shaders/pbr_mat_struct.h"
#include "nvvkhl/shaders/bsdf_structs.h"
#include "nvvkhl/shaders/bsdf_functions.h"
#include "nvvkhl/shaders/light_contrib.h"
#include "nvvkhl/shaders/vertex_accessor.h"

// clang-format off
// Incoming 
layout(location = 0) in Interpolants {
    vec3 pos;
} IN;

// Outgoing
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outSelection;

// Buffers
layout(buffer_reference, scalar) readonly buffer GltfMaterialBuf    { GltfShadeMaterial m[]; };

layout(set = 0, binding = eFrameInfo, scalar) uniform FrameInfo_ { SceneFrameInfo frameInfo; };
layout(set = 0, binding = eSceneDesc) readonly buffer SceneDesc_ { SceneDescription sceneDesc; } ;
layout(set = 0, binding = eTextures) uniform sampler2D[] texturesMap;

layout(set = 1, binding = 0) uniform sampler2D   u_GGXLUT; // lookup table
layout(set = 1, binding = 1) uniform samplerCube u_LambertianEnvSampler; // 
layout(set = 1, binding = 2) uniform samplerCube u_GGXEnvSampler;  //

layout(set = 2, binding = eSkyParam) uniform SkyInfo_ { ProceduralSkyShaderParameters skyInfo; };

// clang-format on

#include "nvvkhl/shaders/pbr_mat_eval.h"
#include "get_hit.h"

layout(push_constant) uniform RasterPushConstant_
{
  PushConstantRaster pc;
};


vec3 getDiffuseLight(vec3 n)
{
  vec3 dir = rotate(n, vec3(0, 1, 0), -frameInfo.envRotation);
  return texture(u_LambertianEnvSampler, dir).rgb * frameInfo.envIntensity.rgb;
}

vec4 getSpecularSample(vec3 reflection, float lod)
{
  vec3 dir = rotate(reflection, vec3(0, 1, 0), -frameInfo.envRotation);
  return textureLod(u_GGXEnvSampler, dir, lod) * frameInfo.envIntensity;
}

vec3 getIBLRadianceGGX(vec3 n, vec3 v, float roughness, vec3 F0)
{
  int   u_MipCount = textureQueryLevels(u_GGXEnvSampler);
  float NdotV      = clampedDot(n, v);
  float lod        = roughness * float(u_MipCount - 1);
  vec3  reflection = normalize(reflect(-v, n));

  vec2 brdfSamplePoint = clamp(vec2(NdotV, roughness), vec2(0.0, 0.0), vec2(1.0, 1.0));
  vec2 f_ab            = texture(u_GGXLUT, brdfSamplePoint).rg;
  vec4 specularSample  = getSpecularSample(reflection, lod);

  vec3 specularLight = specularSample.rgb;

  // see https://bruop.github.io/ibl/#single_scattering_results at Single Scattering Results
  // Roughness dependent fresnel, from Fdez-Aguera
  vec3 Fr     = max(vec3(1.0 - roughness), F0) - F0;
  vec3 k_S    = F0 + Fr * pow(1.0 - NdotV, 5.0);
  vec3 FssEss = k_S * f_ab.x + f_ab.y;

  return specularLight * FssEss;
}

// specularWeight is introduced with KHR_materials_specular
vec3 getIBLRadianceLambertian(vec3 n, vec3 v, float roughness, vec3 diffuseColor, vec3 F0)
{
  float NdotV           = clampedDot(n, v);
  vec2  brdfSamplePoint = clamp(vec2(NdotV, roughness), vec2(0.0, 0.0), vec2(1.0, 1.0));
  vec2  f_ab            = texture(u_GGXLUT, brdfSamplePoint).rg;

  vec3 irradiance = getDiffuseLight(n);

  // see https://bruop.github.io/ibl/#single_scattering_results at Single Scattering Results
  // Roughness dependent fresnel, from Fdez-Aguera

  vec3 Fr  = max(vec3(1.0 - roughness), F0) - F0;
  vec3 k_S = F0 + Fr * pow(1.0 - NdotV, 5.0);
  vec3 FssEss = k_S * f_ab.x + f_ab.y;  // <--- GGX / specular light contribution (scale it down if the specularWeight is low)

  // Multiple scattering, from Fdez-Aguera
  float Ems    = (1.0 - (f_ab.x + f_ab.y));
  vec3  F_avg  = (F0 + (1.0 - F0) / 21.0);
  vec3  FmsEms = Ems * FssEss * F_avg / (1.0 - F_avg * Ems);
  vec3 k_D = diffuseColor * (1.0 - FssEss + FmsEms);  // we use +FmsEms as indicated by the formula in the blog post (might be a typo in the implementation)

  return (FmsEms + k_D) * irradiance;
}


void main()
{
  // Current Instance
  RenderNode renderNode = RenderNodeBuf(sceneDesc.renderNodeAddress)._[pc.renderNodeID];

  // Mesh used by instance
  RenderPrimitive renderPrim = RenderPrimitiveBuf(sceneDesc.renderPrimitiveAddress)._[pc.renderPrimID];

  // Using same hit code as for ray tracing
  const vec3 worldRayOrigin = vec3(frameInfo.viewMatrixI[3].x, frameInfo.viewMatrixI[3].y, frameInfo.viewMatrixI[3].z);
  HitState   hit            = getHitState(renderPrim, gl_BaryCoordNV, gl_PrimitiveID, worldRayOrigin,
                                          mat4x3(renderNode.objectToWorld), mat4x3(renderNode.worldToObject));

  // Material of the object
  GltfShadeMaterial gltfMat = GltfMaterialBuf(sceneDesc.materialAddress).m[renderNode.materialID];

  gltfMat.pbrBaseColorFactor *= hit.color;  // Color at vertices
  MeshState   mesh   = MeshState(hit.nrm, hit.tangent, hit.bitangent, hit.geonrm, hit.uv, false);
  PbrMaterial pbrMat = evaluateMaterial(gltfMat, mesh);

  // Selection
  if(pc.renderNodeID == pc.selectedRenderNode)
    outSelection = vec4(1);
  else
    outSelection = vec4(0);

  // Debugging
  switch(pc.dbgMethod)
  {
    case eDbgMethod_metallic:
      outColor = vec4(vec3(pbrMat.metallic), 1);
      return;
    case eDbgMethod_roughness:
      outColor = vec4(pbrMat.roughness, 0, 1);
      return;
    case eDbgMethod_normal:
      outColor = vec4(vec3(pbrMat.N * .5 + .5), 1);
      return;
    case eDbgMethod_tangent:
      outColor = vec4(pbrMat.T * .5 + .5, 1);
      return;
    case eDbgMethod_bitangent:
      outColor = vec4(pbrMat.B * .5 + .5, 1);
      return;
    case eDbgMethod_basecolor:
      outColor = vec4(vec3(pbrMat.baseColor), 1);
      return;
    case eDbgMethod_emissive:
      outColor = vec4(vec3(pbrMat.emissive), 1);
      return;
    case eDbgMethod_opacity:
      outColor = vec4(vec3(pbrMat.opacity * (1.0 - pbrMat.transmission)), 1);
      return;
  }

  if(gltfMat.alphaMode == ALPHA_MASK)
  {
    if(pbrMat.opacity < gltfMat.alphaCutoff)
      discard;
  }

  const vec3 eyePos = vec3(frameInfo.viewMatrixI[3].x, frameInfo.viewMatrixI[3].y, frameInfo.viewMatrixI[3].z);
  const vec3 worldRayDirection = normalize(hit.pos - eyePos);
  const vec3 toEye             = -worldRayDirection;

  // Result
  vec3 contribution = vec3(0);

  vec3  f0            = mix(vec3(0.04), pbrMat.baseColor, pbrMat.metallic);
  float ambientFactor = 0.3;
  if(frameInfo.useSky != 0)
  {
    vec3 ambientColor = mix(skyInfo.groundColor.rgb, skyInfo.skyColor.rgb, pbrMat.N.y * 0.5 + 0.5) * ambientFactor;
    contribution += ambientColor * pbrMat.baseColor;
    contribution += ambientColor;  // *pbrMat.f0;
  }
  else
  {
    // Calculate lighting contribution from image based lighting source (IBL)
    float perceptualRoughness = mix(pbrMat.roughness.r, pbrMat.roughness.g, 0.5);  // Ad-hoc anisotropic -> isotropic
    vec3  c_diff              = mix(pbrMat.baseColor, vec3(0), pbrMat.metallic);

    vec3 f_specular = getIBLRadianceGGX(pbrMat.N, -worldRayDirection, perceptualRoughness, f0);
    vec3 f_diffuse  = getIBLRadianceLambertian(pbrMat.N, -worldRayDirection, perceptualRoughness, c_diff, f0);

    contribution += f_specular + f_diffuse;
  }


  contribution += pbrMat.emissive;  // emissive

  // All lights
  for(int i = 0; i < frameInfo.nbLights; i++)
  {
    Light        light        = frameInfo.light[i];
    LightContrib lightContrib = singleLightContribution(light, hit.pos, pbrMat.N, -worldRayDirection);

    BsdfEvaluateData evalData;
    evalData.k1 = -worldRayDirection;
    evalData.k2 = -lightContrib.incidentVector;

    bsdfEvaluate(evalData, pbrMat);

    const vec3 w = lightContrib.intensity;
    contribution += w * evalData.bsdf_diffuse;
    contribution += w * evalData.bsdf_glossy;
  }

  outColor = vec4(contribution, pbrMat.opacity);
}
