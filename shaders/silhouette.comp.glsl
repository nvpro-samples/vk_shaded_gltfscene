/*
 * Copyright (c) 2019-2024, NVIDIA CORPORATION.  All rights reserved.
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

#version 430 core

#extension GL_GOOGLE_include_directive : enable

#include "device_host.h"


layout(local_size_x = WORKGROUP_SIZE, local_size_y = WORKGROUP_SIZE) in;  // Define the workgroup size

// Input and output images
layout(binding = 0, r8) readonly uniform image2D u_R8Buffer;
layout(binding = 1, rgba32f) writeonly uniform image2D u_OutputImage;

layout(push_constant) uniform PushConstant_
{
  PushConstantSilhouette pc;
};

void main()
{
  ivec2 texSize    = imageSize(u_R8Buffer);
  ivec2 pixelCoord = ivec2(gl_GlobalInvocationID.xy);

  // Define the Sobel kernel in the x and y directions
  float kernelX[9] = float[](-1, 0, 1, -2, 0, 2, -1, 0, 1);
  float kernelY[9] = float[](-1, -2, -1, 0, 0, 0, 1, 2, 1);

  float sumX = 0.0;
  float sumY = 0.0;

  // Check boundaries
  if(pixelCoord.x > 0 && pixelCoord.x < texSize.x - 1 && pixelCoord.y > 0 && pixelCoord.y < texSize.y - 1)
  {
    for(int i = -1; i <= 1; i++)
    {
      for(int j = -1; j <= 1; j++)
      {
        ivec2 offset = ivec2(i, j);
        float val    = imageLoad(u_R8Buffer, pixelCoord + offset).r;
        sumX += val * kernelX[(i + 1) * 3 + (j + 1)];
        sumY += val * kernelY[(i + 1) * 3 + (j + 1)];
      }
    }
  }

  // Compute the magnitude of the gradient
  float magnitude = sqrt(sumX * sumX + sumY * sumY);

  // If the magnitude is above a certain threshold, we consider it an edge
  float threshold = 2.56;

  // Write the result to the output image
  if(magnitude > threshold)
    imageStore(u_OutputImage, pixelCoord, vec4(pc.color, 1.0));
}
