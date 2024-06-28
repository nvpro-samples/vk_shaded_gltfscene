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


#include "renderer.hpp"
#include "nvvk/dynamicrendering_vk.hpp"
#include "nvvkhl/shaders/dh_tonemap.h"

namespace gltfr {

// This renderer is empty, it just clears the screen
class RendererEmpty : public Renderer
{
public:
  RendererEmpty()           = default;
  ~RendererEmpty() override = default;

  bool init(Resources& /*res*/, Scene& /*scene*/) override { return true; }
  void render(VkCommandBuffer cmd, Resources& res, Scene& /*scene*/, Settings& /*settings*/, nvvk::ProfilerVK& /*profiler*/) override
  {
    glm::vec3               linearColor = nvvkhl_shaders::toLinear({0.2, 0.23, 0.25});
    VkClearColorValue       clearColor  = {{linearColor.x, linearColor.y, linearColor.z, 1.0}};
    VkImage                 image       = res.m_finalImage->getColorImage();
    VkImageSubresourceRange subresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(cmd, image, VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1, &subresourceRange);
  }
  void deinit(Resources& /*res*/) override{};
  void handleChange(Resources& /*res*/, Scene& /*scene*/) override{};
};

std::unique_ptr<Renderer> makeRendererEmpty()
{
  return std::make_unique<RendererEmpty>();
}

}  // namespace gltfr
