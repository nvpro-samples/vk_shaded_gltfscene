/*
 * Copyright (c) 2024, NVIDIA CORPORATION.  All rights reserved.
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
 * SPDX-FileCopyrightText: Copyright (c) 2014-2024 NVIDIA CORPORATION
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

/*

This is the settings of the application.
It allow to control which renderer to use, the environment system, and the intensity of the environment.

*/


namespace gltfr {

// Settings for the renderer
struct Settings
{
  enum EnvSystem
  {
    eSky,
    eHdr,
  };

  // Here: add all renderers and their name
  enum RenderSystem
  {
    ePathtracer,
    eRaster,
  };
  static constexpr const char* rendererNames[] = {"Pathtracer", "Raster"};


  int          maxFrames       = 200000;       // Maximum number of frames to render (used by pathtracer)
  bool         showAxis        = true;         // Show the axis (bottom left)
  EnvSystem    envSystem       = eSky;         // Environment system: Sky or HDR
  RenderSystem renderSystem    = ePathtracer;  // Renderer to use
  float        hdrEnvIntensity = 1.F;          // Intensity of the environment (HDR)
  float        hdrEnvRotation  = 0.F;          // Rotation of the environment (HDR)
  float        maxLuminance    = 1;            // For firefly
  glm::vec3    silhouetteColor = {1.0f, 1.0f, 1.0f};

  void onUI();
};

}  // namespace gltfr