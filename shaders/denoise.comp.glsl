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
#version 450

#extension GL_GOOGLE_include_directive : enable
#include "device_host.h"

// https://jo.dreggn.org/home/2010_atrous.pdf

layout(local_size_x = 16, local_size_y = 16) in;

layout(set = 0, binding = 0, rgba32f) uniform readonly image2D inputImage;
layout(set = 0, binding = 1, rgba32f) uniform readonly image2D inputNormalDepth;
layout(set = 0, binding = 2, rgba32f) uniform writeonly image2D outputImage;

layout(push_constant) uniform PushConstant_
{
  PushConstantDenoiser pc;
};

// clang-format off

// This kernel will likely have less smoothing effect due to the lower weight of the center pixel.
// It may preserve more high-frequency details, which could be beneficial in some cases but might also retain more noise.
const float kernel[25] = float[25](
    1.0 / 256.0,    1.0 / 64.0,     3.0 / 128.0,    1.0 / 64.0,     1.0 / 256.0,
    1.0 / 64.0,     1.0 / 16.0,     3.0 / 32.0,     1.0 / 16.0,     1.0 / 64.0,
    3.0 / 128.0,    3.0 / 32.0,     9.0 / 64.0,     3.0 / 32.0,     3.0 / 128.0,
    1.0 / 64.0,     1.0 / 16.0,     3.0 / 32.0,     1.0 / 16.0,     1.0 / 64.0,
    1.0 / 256.0,    1.0 / 64.0,     3.0 / 128.0,    1.0 / 64.0,     1.0 / 256.0
    );
// Original kernel
const float b3SplineKernel[25] = float[25](
    1.0 / 256.0,    1.0 / 64.0,     3.0 / 128.0,    1.0 / 64.0,     1.0 / 256.0,
    1.0 / 64.0,     1.0 / 16.0,     3.0 / 32.0,     1.0 / 16.0,     1.0 / 64.0,
    3.0 / 128.0,    3.0 / 32.0,     3.0 / 8.0,      3.0 / 32.0,     3.0 / 128.0,
    1.0 / 64.0,     1.0 / 16.0,     3.0 / 32.0,     1.0 / 16.0,     1.0 / 64.0,
    1.0 / 256.0,    1.0 / 64.0,     3.0 / 128.0,    1.0 / 64.0,     1.0 / 256.0
);
// clang-format on


vec4 aTrousWaveletTransform(ivec2 pixelCoords)
{
  vec4  color       = vec4(0.0);
  float totalWeight = 0.0;

  vec4 centerColor = imageLoad(inputImage, pixelCoords);
  vec4 centerND    = imageLoad(inputNormalDepth, pixelCoords);
  if(centerND.a == 0.0)  // Background
    return centerColor;

  for(int i = -2; i <= 2; i++)
  {
    for(int j = -2; j <= 2; j++)
    {
      ivec2 offset       = ivec2(i, j) * pc.stepWidth;
      ivec2 sampleCoords = pixelCoords + offset;

      if(all(greaterThanEqual(sampleCoords, ivec2(0))) && all(lessThan(sampleCoords, imageSize(inputImage))))
      {
        vec4 sampleColor = imageLoad(inputImage, sampleCoords);
        vec4 sampleND    = imageLoad(inputNormalDepth, sampleCoords);

        float kernelWeight  = kernel[(i + 2) * 5 + (j + 2)];
        float colorDistance = distance(sampleColor.rgb, centerColor.rgb);
        float colorWeight   = exp(-colorDistance / pc.colorPhi);
        if(isnan(colorWeight))
          return centerColor;

        float nDist        = 1. - clamp(dot(sampleND.xyz, centerND.xyz), 0, 1);
        float normalWeight = min(exp(-nDist) / pc.normalPhi, 1.0);

        float wDist       = abs(sampleND.w - centerND.w);
        float depthWeight = min(exp(-wDist) / pc.depthPhi, 1.0);

        float weight = kernelWeight * colorWeight * normalWeight * depthWeight;

        color += sampleColor * weight;
        totalWeight += weight;
      }
    }
  }

  return color / totalWeight;
}

void main()
{
  ivec2 pixelCoords = ivec2(gl_GlobalInvocationID.xy);

  if(all(lessThan(pixelCoords, imageSize(outputImage))))
  {
    if(pc.colorPhi > 0.0)
    {
      vec4 denoisedColor = aTrousWaveletTransform(pixelCoords);
      imageStore(outputImage, pixelCoords, denoisedColor);
    }
    else
    {
      imageStore(outputImage, pixelCoords, imageLoad(inputImage, pixelCoords));
    }
  }
}
