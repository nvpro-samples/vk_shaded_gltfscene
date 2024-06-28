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

#include "resources.hpp"
#include "vulkan/vulkan_core.h"
#include "nvvk/renderpasses_vk.hpp"
#include "nvvk/debug_util_vk.hpp"
#include "nvvk/error_vk.hpp"
#include "nvvkhl/glsl_compiler.hpp"
#include "nvh/timesampler.hpp"

extern std::vector<std::string> g_applicationSearchPaths;  // Used by the shader manager

//------------------------------------------------------------------
// The resources are the Vulkan objects that are shared among the
// different renderers. It includes the G-Buffers, the allocator,
// the shader manager, and the temporary command pool.
//
// The G-Buffers are used to store the result of the renderers.
// The allocator is used to allocate memory for the Vulkan objects.
// The shader manager is used to compile the shaders.
// The temporary command pool is used to create temporary command buffers.
void gltfr::Resources::init(VulkanInfo& _ctx)
{
  ctx = _ctx;

  m_allocator       = std::make_unique<AllocDma>(ctx.device, ctx.physicalDevice);
  m_finalImage      = std::make_unique<nvvkhl::GBuffer>(ctx.device, m_allocator.get());
  m_tempCommandPool = std::make_unique<nvvk::CommandPool>(ctx.device, ctx.GCT0.familyIndex,
                                                          VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, ctx.GCT0.queue);

  // ShaderC compilation
  m_glslC = std::make_unique<nvvkhl::GlslCompiler>();
  for(const auto& path : g_applicationSearchPaths)
    m_glslC->addInclude(path);


  resizeGbuffers({128, 128});
}

//------------------------------------------------------------------
// Resize the G-Buffers, which is only a color buffer, as this is
// to display the result of the renderers.
// The image is created with the VK_FORMAT_R8G8B8A8_UNORM format,
// therefore the image should be tonemapped before displaying.
void gltfr::Resources::resizeGbuffers(const VkExtent2D& size)
{
  vkDeviceWaitIdle(ctx.device);
  m_finalImage->destroy();
  m_finalImage->create(size, {VK_FORMAT_R8G8B8A8_UNORM}, VK_FORMAT_UNDEFINED);
  setGBuffersChanged(true);
}

//------------------------------------------------------------------
// Utility function to create a temporary command buffer
VkCommandBuffer gltfr::Resources::createTempCmdBuffer()
{
  VkCommandBuffer cmd = m_tempCommandPool->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
  nvvk::DebugUtil(ctx.device).setObjectName(cmd, "tempCmdBuffer");
  return cmd;
}

//------------------------------------------------------------------
// Utility function to submit and wait for a temporary command buffer
void gltfr::Resources::submitAndWaitTempCmdBuffer(VkCommandBuffer cmd)
{
  m_tempCommandPool->submitAndWait(cmd);
  VkResult result = vkDeviceWaitIdle(ctx.device);
  if(nvvk::checkResult(result, "Resources::synchronize"))
  {
    exit(-1);
  }
}

//------------------------------------------------------------------
// Those are the default compiler options for the GLSL to SPIR-V
//
void setCompilerOptions(nvvkhl::GlslCompiler* glslC)
{
  glslC->resetOptions();
  glslC->options()->SetTargetSpirv(shaderc_spirv_version::shaderc_spirv_version_1_6);
  glslC->options()->SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
  glslC->options()->SetGenerateDebugInfo();
  //#if _DEBUG
  glslC->options()->SetOptimizationLevel(shaderc_optimization_level_zero);
  //#else
  //  glslC->options()->SetOptimizationLevel(shaderc_optimization_level_performance);
  //#endif
}


//------------------------------------------------------------------
// Compile a GLSL shader to SPIR-V
// Return the result of the compilation as ShaderC object
shaderc::SpvCompilationResult gltfr::Resources::compileGlslShader(const std::string& filename, shaderc_shader_kind shaderKind) const
{
  // nvh::ScopedTimer st(__FUNCTION__);
  setCompilerOptions(m_glslC.get());
  return m_glslC->compileFile(filename, shaderKind);
}

//------------------------------------------------------------------
// Create a shader module from the SPIR-V result
//
VkShaderModule gltfr::Resources::createShaderModule(shaderc::SpvCompilationResult& compResult) const
{
  // nvh::ScopedTimer st(__FUNCTION__);
  return m_glslC->createModule(ctx.device, compResult);
}
