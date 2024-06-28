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

#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_shader_explicit_arithmetic_types : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_shader_image_load_formatted : enable  // The folowing extension allow to pass images as function parameters


#include "device_host.h"
#include "dh_bindings.h"
#include "payload.h"

layout(location = 0) rayPayloadInEXT HitPayload hitPayload;
#include "rt_payload.h"

#include "nvvkhl/shaders/pbr_mat_eval.glsl"      // Need texturesMap[]
#include "nvvkhl/shaders/hdr_env_sampling.glsl"  // nedd envSamplingData[]

// For the hitTest()
#include "rt_common.h"

hitAttributeEXT vec2 barys;


//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
void main()
{
  RenderNode      renderNode = RenderNodeBuf(sceneDesc.renderNodeAddress)._[gl_InstanceID];
  RenderPrimitive renderPrim = RenderPrimitiveBuf(sceneDesc.renderPrimitiveAddress)._[gl_InstanceCustomIndexEXT];

  // Barycentric coordinate on the triangle
  const vec3 barycentrics = vec3(1.0 - barys.x - barys.y, barys.x, barys.y);
  int        triangleID   = gl_PrimitiveID;

  float opacity = getOpacity(renderNode, renderPrim, triangleID, barycentrics);
  if(rand(hitPayload.seed) > opacity)
  {
    ignoreIntersectionEXT;
  }
}
