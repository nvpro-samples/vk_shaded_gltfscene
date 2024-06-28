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
#extension GL_EXT_debug_printf : enable

#define USE_FIREFLY_FILTER 1
#define USE_RUSIAN_ROULETTE 1

#include "device_host.h"
#include "dh_bindings.h"
#include "payload.h"


layout(location = 0) rayPayloadEXT HitPayload hitPayload;

#include "rt_payload.h"

#include "nvvkhl/shaders/pbr_mat_eval.glsl"      // Need texturesMap[]
#include "nvvkhl/shaders/hdr_env_sampling.glsl"  // nedd envSamplingData[]

// Depend on hitPayload and other layouts
#include "rt_rtx.h"
#include "rt_common.h"


/// ---------------------------------------------------------------------------------------
///
///
void main()
{
  vec2 imageSize = gl_LaunchSizeEXT.xy;
  vec2 samplePos = vec2(gl_LaunchIDEXT.xy);

  // Debugging, single frame
  if(pc.dbgMethod != eDbgMethod_none)
  {
    if(pc.frame == 0)
    {
      vec3 result = debugRendering(samplePos, imageSize);
      imageStore(image, ivec2(samplePos.xy), vec4(result, 1.0F));
      selectObject(samplePos, imageSize);
    }
    return;
  }

  // Initialize the random number
  uint seed = xxhash32(uvec3(gl_LaunchIDEXT.xy, pc.frame));

  // Subpixel jitter: send the ray through a different position inside the pixel each time, to provide antialiasing.
  vec2 subpixelJitter = pc.frame == 0 ? vec2(0.5f, 0.5f) : vec2(rand(seed), rand(seed));

  // Sampling n times the pixel
  vec3 pixel_color = samplePixel(seed, samplePos, subpixelJitter, imageSize, frameInfo.projMatrixI, frameInfo.viewMatrixI);
  for(int s = 1; s < pc.maxSamples; s++)
  {
    subpixelJitter = vec2(rand(seed), rand(seed));
    pixel_color += samplePixel(seed, samplePos, subpixelJitter, imageSize, frameInfo.projMatrixI, frameInfo.viewMatrixI);
  }
  pixel_color /= pc.maxSamples;

  if(pc.frame == 0)  // first frame
  {
    imageStore(image, ivec2(gl_LaunchIDEXT.xy), vec4(pixel_color, 1.0F));
    selectObject(samplePos, imageSize);
  }
  else
  {
    // Do accumulation over time
    float a         = 1.0F / float(pc.frame + 1);
    vec3  old_color = imageLoad(image, ivec2(gl_LaunchIDEXT.xy)).xyz;
    imageStore(image, ivec2(gl_LaunchIDEXT.xy), vec4(mix(old_color, pixel_color, a), 1.0F));
  }
}
