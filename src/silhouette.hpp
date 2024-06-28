#pragma once

#include <glm/glm.hpp>

#include <vulkan/vulkan_core.h>
#include <array>
#include <memory>

#include "nvvk/descriptorsets_vk.hpp"
#include "nvvk/error_vk.hpp"
#include "resources.hpp"

namespace DH {
#include "shaders/device_host.h"
#include "nvvkhl/shaders/dh_comp.h"
}  // namespace DH


// This file contains the definition of the Silhouette class, which is used to
// extract the outline of a 3D object.
// There are two images, one with the information of the silhouette and the other
// which will be composed of the silhouette and the object itself.
namespace gltfr {


class Silhouette
{
public:
  Silhouette(Resources& res)
      : m_device(res.ctx.device)
  {
    m_dset = std::make_unique<nvvk::DescriptorSetContainer>(m_device);
    createShaders(res);
    createShaderObjectAndLayout();
  }
  ~Silhouette()
  {
    for(auto shader : m_shaders)
      vkDestroyShaderEXT(m_device, shader, NULL);
  }

  void render(VkCommandBuffer cmd, VkDescriptorImageInfo silhouette, VkDescriptorImageInfo rgbaImage, VkExtent2D imgSize)
  {
    // Push descriptor set
    std::vector<VkWriteDescriptorSet> writes;
    writes.push_back(m_dset->makeWrite(0, 0, &silhouette));
    writes.push_back(m_dset->makeWrite(0, 1, &rgbaImage));
    vkCmdPushDescriptorSetKHR(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_dset->getPipeLayout(), 0,
                              static_cast<uint32_t>(writes.size()), writes.data());

    // Bind compute shader
    const VkShaderStageFlagBits stages[1] = {VK_SHADER_STAGE_COMPUTE_BIT};
    vkCmdBindShadersEXT(cmd, 1, stages, m_shaders.data());

    // Pushing constants
    vkCmdPushConstants(cmd, m_dset->getPipeLayout(), VK_SHADER_STAGE_ALL, 0, sizeof(DH::PushConstantSilhouette), &m_pushConstant);

    // Dispatch compute shader
    VkExtent2D group_counts = DH::getGroupCounts(imgSize);
    vkCmdDispatch(cmd, group_counts.width, group_counts.height, 1);
  }
  void setColor(glm::vec3 color) { m_pushConstant.color = color; }
  bool isValid() { return m_spvShader.GetCompilationStatus() == shaderc_compilation_status_success; }

private:
  //-------------------------------------------------------------------------------------------------
  // Creating the pipeline layout and shader object
  void createShaderObjectAndLayout()
  {
    VkPushConstantRange push_constant_ranges = {.stageFlags = VK_SHADER_STAGE_ALL, .offset = 0, .size = sizeof(DH::PushConstantSilhouette)};

    // Create the layout used by the shader
    m_dset->addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_ALL);  // Silhouette
    m_dset->addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_ALL);  // Image
    m_dset->initLayout(VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR);
    m_dset->initPipeLayout(1, &push_constant_ranges);

    VkShaderModuleCreateInfo shaderModuleCreateInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    shaderModuleCreateInfo.codeSize = (m_spvShader.end() - m_spvShader.begin()) * sizeof(uint32_t);
    shaderModuleCreateInfo.pCode    = reinterpret_cast<const uint32_t*>(m_spvShader.begin());

    // Compute shader description
    std::vector<VkShaderCreateInfoEXT> shaderCreateInfos;
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
    // Create the shader
    NVVK_CHECK(vkCreateShadersEXT(m_device, 1, shaderCreateInfos.data(), NULL, m_shaders.data()));
  }

  void createShaders(Resources& res)
  {
    m_spvShader = res.compileGlslShader("silhouette.comp.glsl", shaderc_shader_kind::shaderc_compute_shader);
    if(m_spvShader.GetCompilationStatus() != shaderc_compilation_status_success)
    {
      LOGE("Error in compiling the shader: %s", m_spvShader.GetErrorMessage().c_str());
    }
  }


  VkDevice                                      m_device{};
  std::unique_ptr<nvvk::DescriptorSetContainer> m_dset;
  DH::PushConstantSilhouette                    m_pushConstant{.color = glm::vec3(1, 0, 0)};
  std::array<VkShaderEXT, 1>                    m_shaders = {};
  shaderc::SpvCompilationResult                 m_spvShader;
};

}  // namespace gltfr
