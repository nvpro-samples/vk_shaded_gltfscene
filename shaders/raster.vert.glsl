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
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_GOOGLE_include_directive : enable

#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference2 : require

#include "device_host.h"
#include "dh_bindings.h"
#include "nvvkhl/shaders/dh_scn_desc.h"

// clang-format off
layout(buffer_reference, scalar) buffer  RenderNodeBuf { RenderNode _[]; };

layout(set = 0, binding = eFrameInfo) uniform FrameInfo_ { SceneFrameInfo frameInfo; };
layout(set = 0, binding = eSceneDesc) readonly buffer SceneDesc_ { SceneDescription sceneDesc; } ;
// clang-format on

layout(push_constant) uniform RasterPushConstant_
{
  PushConstantRaster pc;
};

layout(location = 0) in vec3 i_pos;

layout(location = 0) out Interpolants
{
  vec3 pos;
}
OUT;

out gl_PerVertex
{
  vec4 gl_Position;
};


void main()
{
  RenderNode renderNode = RenderNodeBuf(sceneDesc.renderNodeAddress)._[pc.renderNodeID];
  OUT.pos               = vec3(renderNode.objectToWorld * vec4(i_pos, 1.0));
  gl_Position           = frameInfo.projMatrix * frameInfo.viewMatrix * vec4(OUT.pos, 1.0);
}
