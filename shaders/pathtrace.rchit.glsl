/*
 * Copyright (c) 2022, NVIDIA CORPORATION.  All rights reserved.
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
 * SPDX-FileCopyrightText: Copyright (c) 2014-2022 NVIDIA CORPORATION
 * SPDX-License-Identifier: Apache-2.0
 */


#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference2 : require

#include "device_host.h"
#include "dh_bindings.h"
#include "payload.h"
#include "nvvkhl/shaders/dh_scn_desc.h"
#include "nvvkhl/shaders/vertex_accessor.h"


hitAttributeEXT vec2 barys;

// clang-format off
layout(location = 0) rayPayloadInEXT HitPayload payload;
layout(set = 1, binding = eSceneDesc) readonly buffer SceneDesc_ { SceneDescription sceneDesc; };
// clang-format on


#include "nvvkhl/shaders/func.h"
#include "get_hit.h"


layout(constant_id = 0) const int USE_SER = 0;


//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
void main()
{
  // Retrieve the Primitive mesh buffer information
  RenderNode      renderNode = RenderNodeBuf(sceneDesc.renderNodeAddress)._[gl_InstanceID];
  RenderPrimitive renderPrim = RenderPrimitiveBuf(sceneDesc.renderPrimitiveAddress)._[gl_InstanceCustomIndexEXT];

  const vec3 barycentrics = vec3(1.0 - barys.x - barys.y, barys.x, barys.y);
  bool       frontFacing  = (gl_HitKindEXT == gl_HitKindFrontFacingTriangleEXT);

  payload.hitT    = gl_HitTEXT;
  payload.rnodeID = gl_InstanceID;
  payload.rprimID = gl_InstanceCustomIndexEXT;  // Should be equal to renderNode.rprimID
  payload.hit = getHitState(renderPrim, barycentrics, gl_PrimitiveID, gl_WorldRayOriginEXT, gl_ObjectToWorldEXT, gl_WorldToObjectEXT);
}
