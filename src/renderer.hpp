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
  
  This is the base class for the Renderer.

*/


#include "resources.h"
#include "scene.hpp"
#include "nvvk/profiler_vk.hpp"

namespace DH {
#include "shaders/device_host.h"  // Include the device/host structures
}  // namespace DH


namespace gltfr {

class Renderer
{
public:
  virtual ~Renderer() = default;

  // Use init to create the resources and the pipeline
  virtual bool init(Resources& res, Scene& scene) = 0;

  // Use render to render the scene
  virtual void render(VkCommandBuffer primary, Resources& res, Scene& scene, Settings& settings, nvvk::ProfilerVK& profiler) = 0;

  // Use deinit to destroy the resources and the pipeline
  virtual void deinit(Resources& res) = 0;

  // Use onUI to show the UI for the renderer
  virtual bool onUI() { return false; }

  // Use handleChange to react to changes in the scene
  virtual void handleChange(Resources& res, Scene& scene) = 0;

  // Use getOutputImage to get the final rendered image
  virtual VkDescriptorImageInfo getOutputImage() const { return {}; }
};

// Add under here all the different renderers
std::unique_ptr<Renderer> makeRendererEmpty();
std::unique_ptr<Renderer> makeRendererPathtracer();
std::unique_ptr<Renderer> makeRendererRaster();

}  // namespace gltfr