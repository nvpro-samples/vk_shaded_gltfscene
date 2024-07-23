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

#include <vulkan/vulkan_core.h>
#include <memory>

#include "nvvk/descriptorsets_vk.hpp"
#include "nvvk/error_vk.hpp"

namespace gltfr {

/* @DOC_START
     
    This class is used to create one or multiple compute shaders that can be dispatched with share push constants and descriptors.
    
    Usage:
    - Create the object with the device
    - Add the resources that will be used by the shader
    - Add the shader(s) to be used
    - Create the shader object and layout
    - Set the descriptor(s) for the shader
    - Dispatch the shader

    Note: this class is templated to allow the user to define the push constant and the binding enum. To avoid 
          enum, replace the enum with a uint32_t. For the push constant, you can use a struct with the data you need, or even just a float.
          It is unfortunately not possible to have no push constant in this version of the class.

    ``` cpp 
    PushCompute<MyPushConstant, MyEnum> myCompute(device);
    myCompute.addResource(MyEnum::eObjectID, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    myCompute.addResource(MyEnum::eRGBAIImage, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    myCompute.addShader(shaderModuleCreateInfo);
    myCompute.createShaderObjectAndLayout();
    if(myCompute.isValid())
    {
        myCompute.setDescriptor(MyEnum::eObjectID, m_objectImage->descriptorInfo());
	    myCompute.setDescriptor(MyEnum::eRGBAIImage, m_finalImage->descriptorInfo());
	    myCompute.dispatch2D(cmd, {128, 128}); // Run on a 128x128 image
    }
    ``` 

@DOC_END */

template <typename TPushConstants, typename TBindingEnum>
class PushCompute
{
public:
  PushCompute(VkDevice device)
      : m_device(device)
  {
    m_dset = std::make_unique<nvvk::DescriptorSetContainer>(m_device);
  }
  ~PushCompute()
  {
    for(auto shader : m_shaders)
      vkDestroyShaderEXT(m_device, shader, nullptr);
  }

  // Add what the shader will need as resources
  void addResource(TBindingEnum idx, VkDescriptorType type)
  {
    m_dset->addBinding(uint32_t(idx), type, 1, VK_SHADER_STAGE_ALL);
  }

  // Add the shader code
  void addShader(const VkShaderModuleCreateInfo& shaderCreateInfo) { m_shaderCreateInfos.push_back(shaderCreateInfo); }

  // Add a descriptor to the list of descriptors
  void setDescriptor(TBindingEnum index, const VkDescriptorImageInfo& img)
  {
    m_descWrites[index] = m_dset->makeWrite(0, index, &img);
  }

  void setDescriptor(TBindingEnum index, const VkDescriptorBufferInfo& buf)
  {
    m_descWrites[index] = m_dset->makeWrite(0, index, &buf);
  }

  void setDescriptor(TBindingEnum index, const VkWriteDescriptorSetAccelerationStructureKHR& accel)
  {
    m_descWrites[index] = m_dset->makeWrite(0, index, &accel);
  }

  // Returns the number of workgroups needed for the number of elements
  inline uint32_t getGroupCounts(const uint32_t size, int workgroupSize = 256)
  {
    return (size + (workgroupSize - 1)) / workgroupSize;
  }

  // Dispatch the compute shader for a number of elements. Default group is 256
  void dispatch1D(VkCommandBuffer cmd, uint32_t numElem, uint32_t workgroupSize = 256)
  {
    VkExtent3D groupSize = {getGroupCounts(numElem, workgroupSize), 1, 1};
    dispatch(cmd, groupSize);
  }

  // Dispatch the compute shader for a 2D (i.e. image). Default group is 16*16 = 256
  void dispatch2D(VkCommandBuffer cmd, VkExtent2D numElem, uint32_t workgroupSize = 16)
  {
    VkExtent3D groupSize = {getGroupCounts(numElem.width, workgroupSize), getGroupCounts(numElem.height, workgroupSize), 1};
    dispatch(cmd, groupSize);
  }

  // Dispatch the compute shader for a 3D (i.e. volume). Default group is 8*8*8 = 512
  void dispatch3D(VkCommandBuffer cmd, VkExtent3D numElem, uint32_t workgroupSize = 8)
  {
    VkExtent3D groupSize = {getGroupCounts(numElem.width, workgroupSize), getGroupCounts(numElem.height, workgroupSize),
                            getGroupCounts(numElem.depth, workgroupSize)};
    dispatch(cmd, groupSize);
  }

  // Dispatch the compute shader generic
  void dispatch(VkCommandBuffer cmd, VkExtent3D groupSize)
  {
    // Push descriptor set
    vkCmdPushDescriptorSetKHR(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_dset->getPipeLayout(), 0,
                              static_cast<uint32_t>(m_descWrites.size()), m_descWrites.data());

    // Pushing constants
    vkCmdPushConstants(cmd, m_dset->getPipeLayout(), VK_SHADER_STAGE_ALL, 0, sizeof(TPushConstants), &m_pushConstant);

    // Bind compute shader
    // @TODO: support multiple shaders
    const VkShaderStageFlagBits stages[1] = {VK_SHADER_STAGE_COMPUTE_BIT};
    vkCmdBindShadersEXT(cmd, 1, stages, m_shaders.data());

    // Dispatch compute shader
    vkCmdDispatch(cmd, groupSize.width, groupSize.height, groupSize.depth);
    memoryBarrier(cmd);  // Post: producer -> consumer (safe)
  }

  // The shaders must have been compiled successfully
  bool isValid() { return m_valid; }

  // Access to the push constant
  TPushConstants& pushConstant() { return m_pushConstant; }

  // Creating the pipeline layout and shader object
  void createShaderObjectAndLayout()
  {
    VkPushConstantRange push_constant_ranges = {.stageFlags = VK_SHADER_STAGE_ALL, .offset = 0, .size = sizeof(TPushConstants)};

    // Create the layout used by the shader
    m_dset->initLayout(VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR);
    m_dset->initPipeLayout(1, &push_constant_ranges);

    // Holding the descriptor writes, see setDescriptor
    uint32_t numBindings = uint32_t(m_dset->getBindings().size());
    m_descWrites.resize(numBindings);

    // Creating as many shader object as provided
    std::vector<VkShaderCreateInfoEXT> shaderCreateInfos;
    for(auto& shaderModuleCreateInfo : m_shaderCreateInfos)
    {
      shaderCreateInfos.push_back(VkShaderCreateInfoEXT{
          .sType                  = VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT,
          .pNext                  = NULL,
          .flags                  = VK_SHADER_CREATE_DISPATCH_BASE_BIT_EXT,
          .stage                  = VK_SHADER_STAGE_COMPUTE_BIT,
          .nextStage              = 0,
          .codeType               = VK_SHADER_CODE_TYPE_SPIRV_EXT,
          .codeSize               = shaderModuleCreateInfo.codeSize,
          .pCode                  = shaderModuleCreateInfo.pCode,
          .pName                  = "main",
          .setLayoutCount         = 1,
          .pSetLayouts            = &m_dset->getLayout(),
          .pushConstantRangeCount = 1,
          .pPushConstantRanges    = &push_constant_ranges,
          .pSpecializationInfo    = NULL,
      });
    }
    // Create the shader objects
    m_shaders.resize(shaderCreateInfos.size());
    VkResult result =
        (vkCreateShadersEXT(m_device, uint32_t(shaderCreateInfos.size()), shaderCreateInfos.data(), nullptr, m_shaders.data()));
    m_valid = (result == VK_SUCCESS);
    NVVK_CHECK(result);
  }

  // Set up memory barrier
  void memoryBarrier(VkCommandBuffer cmd)
  {
    VkMemoryBarrier memoryBarrier = {.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                     .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                     .dstAccessMask = VK_ACCESS_SHADER_READ_BIT};

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1,
                         &memoryBarrier, 0, nullptr, 0, nullptr);
  }

protected:
  VkDevice                                      m_device{};             // Vulkan device
  std::unique_ptr<nvvk::DescriptorSetContainer> m_dset{};               // Descriptor set container
  std::vector<VkShaderEXT>                      m_shaders{};            // Shader objects
  std::vector<VkWriteDescriptorSet>             m_descWrites{};         // Descriptor writes
  std::vector<VkShaderModuleCreateInfo>         m_shaderCreateInfos{};  // Shader module create info
  bool                                          m_valid{false};         // Validity of the shader
  TPushConstants                                m_pushConstant{};       // Push constant
};

}  // namespace gltfr
