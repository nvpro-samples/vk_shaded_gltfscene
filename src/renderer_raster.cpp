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

#include <glm/glm.hpp>

// Purpose: Raster renderer implementation
#include "nvvkhl/shaders/dh_lighting.h"
namespace DH {
#include "shaders/device_host.h"  // Include the device/host structures
}  // namespace DH


#include "imgui.h"
#include "nvh/cameramanipulator.hpp"
#include "nvvk/debug_util_vk.hpp"
#include "nvvk/descriptorsets_vk.hpp"
#include "nvvk/dynamicrendering_vk.hpp"
#include "nvvk/error_vk.hpp"
#include "nvvk/sbtwrapper_vk.hpp"
#include "nvvkhl/shaders/dh_tonemap.h"
#include "renderer.hpp"
#include "shaders/dh_bindings.h"
#include "nvvk/pipeline_vk.hpp"
#include "nvvk/renderpasses_vk.hpp"
#include "nvh/timesampler.hpp"
#include "silhouette.hpp"


constexpr auto RASTER_SS_SIZE = 2;  // Change this for the default Super-Sampling resolution multiplier for raster;

namespace PE = ImGuiH::PropertyEditor;

namespace gltfr {

struct RasterSettings
{
  bool showWireframe{false};
  bool useSuperSample{true};
  int  dbgMethod{0};
} g_rasterSettings;

// This shows path tracing using ray tracing
class RendererRaster : public Renderer
{


public:
  RendererRaster() = default;
  ~RendererRaster() { deinit(); };

  bool init(Resources& res, Scene& scene) override;
  void deinit(Resources& /*res*/) override { deinit(); }
  void render(VkCommandBuffer cmd, Resources& res, Scene& scene, Settings& settings, nvvk::ProfilerVK& profiler) override;


  bool onUI() override;
  void handleChange(Resources& res, Scene& scene) override;

  VkDescriptorImageInfo getOutputImage() const override { return m_gSimpleBuffers->getDescriptorImageInfo(); }

private:
  void createRasterPipeline(Resources& res, Scene& scene);
  void createRecordCommandBuffer();
  void freeRecordCommandBuffer();
  void recordRasterScene(Scene& scene);
  void renderNodes(VkCommandBuffer cmd, Scene& scene, const std::vector<uint32_t>& nodeIDs);
  void renderRasterScene(VkCommandBuffer cmd, Scene& scene);
  bool initShaders(Resources& res);
  void deinit();
  void createGBuffer(Resources& res, Scene& scene);

  enum PipelineType
  {
    eRasterSolid,
    eRasterSolidDoubleSided,
    eRasterBlend,
    eRasterWireframe
  };

  enum GBufferType
  {
    eSuperSample,
    eSilhouette
  };

  DH::PushConstantRaster m_pushConst{};

  std::unique_ptr<nvvkhl::PipelineContainer> m_rasterPipepline{};      // Raster scene pipeline
  std::unique_ptr<nvvkhl::GBuffer>           m_gSuperSampleBuffers{};  // G-Buffers: RGBA32F, R8, Depth32F
  std::unique_ptr<nvvkhl::GBuffer>           m_gSimpleBuffers{};       // G-Buffers: RGBA32F
  std::unique_ptr<nvvk::DebugUtil>           m_dbgUtil{};
  std::unique_ptr<Silhouette>                m_silhouette{};

  enum ShaderStages
  {
    eVertex,
    eFragment,
    eFragmentOverlay,
    // Last entry is the number of shaders
    eShaderGroupCount
  };
  std::array<shaderc::SpvCompilationResult, eShaderGroupCount> m_spvShader;

  VkCommandBuffer m_recordedSceneCmd{VK_NULL_HANDLE};
  VkDevice        m_device{VK_NULL_HANDLE};
  VkCommandPool   m_commandPool{VK_NULL_HANDLE};
};

//--------------------------------------------------------------------------------------------------
// Compile all shaders used by this renderer
//
bool RendererRaster::initShaders(Resources& res)
{
  nvh::ScopedTimer st(__FUNCTION__);

  // Loading the shaders
  m_spvShader[eVertex]   = res.compileGlslShader("raster.vert.glsl", shaderc_shader_kind::shaderc_vertex_shader);
  m_spvShader[eFragment] = res.compileGlslShader("raster.frag.glsl", shaderc_shader_kind::shaderc_fragment_shader);
  m_spvShader[eFragmentOverlay] = res.compileGlslShader("raster_overlay.frag.glsl", shaderc_shader_kind::shaderc_fragment_shader);

  for(auto& s : m_spvShader)
  {
    if(s.GetCompilationStatus() != shaderc_compilation_status_success)
    {
      LOGE("Error when loading shaders\n");
      LOGE("Error %s\n", s.GetErrorMessage().c_str());
      return false;
    }
  }


  return true;
}

//--------------------------------------------------------------------------------------------------
// Initialize the rasterizer
//
bool RendererRaster::init(Resources& res, Scene& scene)
{
  m_device      = res.ctx.device;
  m_commandPool = res.m_tempCommandPool->getCommandPool();
  m_dbgUtil     = std::make_unique<nvvk::DebugUtil>(m_device);

  if(!initShaders(res))
  {
    return false;
  }

  m_silhouette = std::make_unique<Silhouette>(res);

  m_gSuperSampleBuffers = std::make_unique<nvvkhl::GBuffer>(m_device, res.m_allocator.get());
  m_gSimpleBuffers      = std::make_unique<nvvkhl::GBuffer>(m_device, res.m_allocator.get());
  createGBuffer(res, scene);
  createRasterPipeline(res, scene);

  return true;
}

//--------------------------------------------------------------------------------------------------
// Destroying all allocated resources
//
void RendererRaster::deinit()
{
  if(m_rasterPipepline)
    m_rasterPipepline->destroy(m_device);
  m_rasterPipepline.reset();
}

//--------------------------------------------------------------------------------------------------
// Create two G-Buffers, one for the super-sampled and one for the simple
// The rendering happens in the super-sampled and then blit to the simple
// The super-sampled is used for the rasterization and the simple for the UI
//
void RendererRaster::createGBuffer(Resources& res, Scene& scene)
{
  nvh::ScopedTimer st(std::string(__FUNCTION__));

  static VkFormat depthFormat = nvvk::findDepthFormat(res.ctx.physicalDevice);  // Not all depth are supported

  // Normal size G-Buffer in which the super-sampling will be blitzed
  m_gSimpleBuffers->destroy();
  m_gSimpleBuffers->create(res.m_finalImage->getSize(), {VK_FORMAT_R32G32B32A32_SFLOAT}, VK_FORMAT_UNDEFINED);

  // Super-Sampled G-Buffer: larger size to accommodate the super-sampling
  VkExtent2D superSampleSize = res.m_finalImage->getSize();
  if(g_rasterSettings.useSuperSample)
  {
    superSampleSize.width *= RASTER_SS_SIZE;
    superSampleSize.height *= RASTER_SS_SIZE;
  }

  m_gSuperSampleBuffers->destroy();
  m_gSuperSampleBuffers->create(superSampleSize, {VK_FORMAT_R32G32B32A32_SFLOAT, VK_FORMAT_R8_UNORM}, depthFormat);

  LOGI(":%dx%d", superSampleSize.width, superSampleSize.height);
  scene.m_sky->setOutImage(m_gSuperSampleBuffers->getDescriptorImageInfo());
  scene.m_hdrDome->setOutImage(m_gSuperSampleBuffers->getDescriptorImageInfo());
}

//--------------------------------------------------------------------------------------------------
// Rendering the scene
// - Draw first the sky or HDR dome
// - Record the scene rendering (if not already done)
// - Execute the scene rendering
// - Draw the bounding box of the selected node (if any)
// - Blit the super-sampled G-Buffer to the simple G-Buffer
//
void RendererRaster::render(VkCommandBuffer cmd, Resources& /*res*/, Scene& scene, Settings& settings, nvvk::ProfilerVK& profiler)
{
  auto scopeDbg = m_dbgUtil->DBG_SCOPE(cmd);
  auto sec      = profiler.timeRecurring("Raster", cmd);

  // Push constant
  m_pushConst.dbgMethod = g_rasterSettings.dbgMethod;

  auto scope_dbg = m_dbgUtil->DBG_SCOPE(cmd);

  // Rendering dome or sky in the background, it is covering the entire screen
  {
    const float      aspect_ratio = m_gSuperSampleBuffers->getAspectRatio();
    const glm::mat4& view         = CameraManip.getMatrix();
    auto&            clipPlanes   = CameraManip.getClipPlanes();
    glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(CameraManip.getFov()), aspect_ratio, clipPlanes.x, clipPlanes.y);
    proj[1][1] *= -1;

    // Update the sky
    const VkExtent2D img_size = m_gSuperSampleBuffers->getSize();
    if(settings.envSystem == Settings::eSky)
    {
      scene.m_sky->skyParams().directionUp = CameraManip.getUp();
      scene.m_sky->updateParameterBuffer(cmd);

      auto skysec = profiler.timeRecurring("Sky", cmd);
      scene.m_sky->draw(cmd, view, proj, img_size);
    }
    else
    {
      auto hdrsec = profiler.timeRecurring("HDR Dome", cmd);

      std::array<float, 4> color{settings.hdrEnvIntensity, settings.hdrEnvIntensity, settings.hdrEnvIntensity, 1.0F};
      scene.m_hdrDome->draw(cmd, view, proj, img_size, color.data(), settings.hdrEnvRotation);
    }
  }

  // Scene is recorded to avoid CPU overhead
  if(m_recordedSceneCmd == VK_NULL_HANDLE)
  {
    recordRasterScene(scene);
  }

  // Execute recorded command buffer - the scene graph traversal is already in the secondary command buffer,
  // but stil need to execute it
  {
    auto         rastersec = profiler.timeRecurring("Raster", cmd);
    VkClearValue colorClear{.color = {0.0F, 0.0F, 0.0F, 1.0F}};
    VkClearValue depthClear{.depthStencil = {1.0F, 0}};


    // There are two color attachments, one for the super-sampled final image and one for the selection (silhouette)
    // The depth is shared between the two
    // The first color attachment is loaded because we don't want to erase the dome/sky, the second is cleared.
    std::vector<VkRenderingAttachmentInfo> colorAttachments = {
        VkRenderingAttachmentInfo{
            .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView   = m_gSuperSampleBuffers->getColorImageView(GBufferType::eSuperSample),
            .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            .loadOp      = VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue  = colorClear,
        },
        {
            VkRenderingAttachmentInfo{
                .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView   = m_gSuperSampleBuffers->getColorImageView(GBufferType::eSilhouette),
                .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue  = colorClear,
            },
        }};

    // Shared depth attachment
    VkRenderingAttachmentInfo depthStencilAttachment{
        .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView   = m_gSuperSampleBuffers->getDepthImageView(),
        .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue  = depthClear,
    };

    // Dynamic rendering information: color and depth attachments
    VkRenderingInfo renderingInfo{
        .sType                = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .flags                = VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT,
        .renderArea           = {{0, 0}, m_gSuperSampleBuffers->getSize()},
        .layerCount           = 1,
        .colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size()),
        .pColorAttachments    = colorAttachments.data(),
        .pDepthAttachment     = &depthStencilAttachment,
    };

    vkCmdBeginRendering(cmd, &renderingInfo);
    vkCmdExecuteCommands(cmd, 1, &m_recordedSceneCmd);
    vkCmdEndRendering(cmd);
  }

  {
    // Silhouette: rendering the selected node in the second color attachment
    if(m_silhouette->isValid())
    {
      m_silhouette->setColor(nvvkhl_shaders::toLinear(settings.silhouetteColor));
      m_silhouette->render(cmd, m_gSuperSampleBuffers->getDescriptorImageInfo(GBufferType::eSilhouette),
                           m_gSuperSampleBuffers->getDescriptorImageInfo(GBufferType::eSuperSample),
                           m_gSuperSampleBuffers->getSize());
    }
  }

  {
    // Blit the super-sampled G-Buffer to the simple G-Buffer
    VkImageBlit blitRegions{};
    blitRegions.srcOffsets[0] = {0, 0, 0};
    blitRegions.srcOffsets[1] = {int(m_gSuperSampleBuffers->getSize().width), int(m_gSuperSampleBuffers->getSize().height), 1};
    blitRegions.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blitRegions.srcSubresource.layerCount = 1;
    blitRegions.dstOffsets[0]             = {0, 0, 0};
    blitRegions.dstOffsets[1] = {int(m_gSimpleBuffers->getSize().width), int(m_gSimpleBuffers->getSize().height), 1};
    blitRegions.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blitRegions.dstSubresource.layerCount = 1;
    vkCmdBlitImage(cmd, m_gSuperSampleBuffers->getColorImage(), VK_IMAGE_LAYOUT_GENERAL,
                   m_gSimpleBuffers->getColorImage(), VK_IMAGE_LAYOUT_GENERAL, 1, &blitRegions, VK_FILTER_LINEAR);
  }
}

//--------------------------------------------------------------------------------------------------
// Render the UI of the rasterizer
//
bool RendererRaster::onUI()
{
  bool changed{false};

  if(ImGui::CollapsingHeader("RendererRaster"))
  {
    ImGui::PushID("RendererRaster");
    PE::begin();
    changed |= PE::Checkbox("Show Wireframe", &g_rasterSettings.showWireframe);
    changed |= PE::Checkbox("Use Super Sample", &g_rasterSettings.useSuperSample);
    changed |= PE::Combo("Debug Method", &g_rasterSettings.dbgMethod,
                         "None\0Metallic\0Roughness\0Normal\0Tangent\0Bitangent\0BaseColor\0Emissive\0Opacity\0\0");
    PE::end();
    ImGui::PopID();
  }
  if(changed)
  {
    vkDeviceWaitIdle(m_device);
    freeRecordCommandBuffer();
  }
  return changed;
}

//--------------------------------------------------------------------------------------------------
// If the scene has changed, or the resolution changed, we need to re-record the command buffer
//
void RendererRaster::handleChange(Resources& res, Scene& scene)
{
  static int  lastSelection       = -1;
  static bool lastUseSuperSample  = g_rasterSettings.useSuperSample;
  bool        resetRecorededScene = (lastSelection != scene.getSelectedRenderNode());
  bool        gbufferChanged      = res.hasGBuffersChanged() || (lastUseSuperSample != g_rasterSettings.useSuperSample);
  bool        updateHdrDome       = scene.hasHdrChanged();

  if(gbufferChanged || resetRecorededScene || updateHdrDome)
  {
    vkDeviceWaitIdle(m_device);
    lastSelection = scene.getSelectedRenderNode();
    freeRecordCommandBuffer();
  }
  if(gbufferChanged)
  {
    // Need to recreate the output G-Buffers with the new size
    createGBuffer(res, scene);
    updateHdrDome      = true;
    lastUseSuperSample = g_rasterSettings.useSuperSample;
  }
  if(updateHdrDome)
  {
    scene.m_hdrDome->setOutImage(m_gSuperSampleBuffers->getDescriptorImageInfo());
  }
}

//--------------------------------------------------------------------------------------------------
// Create the all pipelines for rendering the scene.
// It uses the same layout for all pipelines
// but the piplines are different for solid, blend, wireframe, etc.
//
void RendererRaster::createRasterPipeline(Resources& res, Scene& scene)
{
  nvh::ScopedTimer st(__FUNCTION__);

  std::unique_ptr<nvvk::DebugUtil> dutil = std::make_unique<nvvk::DebugUtil>(m_device);
  m_rasterPipepline                      = std::make_unique<nvvkhl::PipelineContainer>();

  VkDescriptorSetLayout sceneSet   = scene.m_sceneSet->getLayout();
  VkDescriptorSetLayout hdrDomeSet = scene.m_hdrDome->getDescLayout();
  VkDescriptorSetLayout skySet     = scene.m_sky->getDescriptorSetLayout();

  // Creating the Pipeline Layout
  std::vector<VkDescriptorSetLayout> layouts{sceneSet, hdrDomeSet, skySet};
  const VkPushConstantRange pushConstantRanges = {.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                                  .offset = 0,
                                                  .size   = sizeof(DH::PushConstantRaster)};
  VkPipelineLayoutCreateInfo create_info{
      .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount         = static_cast<uint32_t>(layouts.size()),
      .pSetLayouts            = layouts.data(),
      .pushConstantRangeCount = 1,
      .pPushConstantRanges    = &pushConstantRanges,
  };
  vkCreatePipelineLayout(m_device, &create_info, nullptr, &m_rasterPipepline->layout);

  // Shader source (Spir-V)
  std::array<VkShaderModule, eShaderGroupCount> shaderModules{};
  shaderModules[eVertex]          = res.createShaderModule(m_spvShader[eVertex]);
  shaderModules[eFragment]        = res.createShaderModule(m_spvShader[eFragment]);
  shaderModules[eFragmentOverlay] = res.createShaderModule(m_spvShader[eFragmentOverlay]);

  std::vector<VkFormat>         color_format = {m_gSuperSampleBuffers->getColorFormat(GBufferType::eSuperSample),
                                                m_gSuperSampleBuffers->getColorFormat(GBufferType::eSilhouette)};
  VkPipelineRenderingCreateInfo renderingInfo{
      .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
      .colorAttachmentCount    = uint32_t(color_format.size()),
      .pColorAttachmentFormats = color_format.data(),
      .depthAttachmentFormat   = m_gSuperSampleBuffers->getDepthFormat(),
  };

  // Creating the Pipeline
  nvvk::GraphicsPipelineGeneratorCombined gpb(m_device, m_rasterPipepline->layout, {} /*m_offscreenRenderPass*/);
  gpb.createInfo.pNext = &renderingInfo;
  gpb.addBindingDescriptions({{0, sizeof(glm::vec3)}});
  gpb.addAttributeDescriptions({
      {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},  // Position
  });

  {
    // Solid
    gpb.rasterizationState.depthBiasEnable         = VK_TRUE;
    gpb.rasterizationState.depthBiasConstantFactor = -1;
    gpb.rasterizationState.depthBiasSlopeFactor    = 1;
    gpb.rasterizationState.cullMode                = VK_CULL_MODE_BACK_BIT;
    gpb.setBlendAttachmentCount(uint32_t(color_format.size()));  // 2 color attachments
    {
      VkPipelineColorBlendAttachmentState blend_state{};
      blend_state.colorWriteMask =
          VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
      blend_state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
      blend_state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
      blend_state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
      gpb.setBlendAttachmentState(1, blend_state);
    }

    gpb.addShader(shaderModules[eVertex], VK_SHADER_STAGE_VERTEX_BIT);
    gpb.addShader(shaderModules[eFragment], VK_SHADER_STAGE_FRAGMENT_BIT);
    m_rasterPipepline->plines.push_back(gpb.createPipeline());
    m_dbgUtil->DBG_NAME(m_rasterPipepline->plines[eRasterSolid]);
    // Double Sided
    gpb.rasterizationState.cullMode = VK_CULL_MODE_NONE;
    m_rasterPipepline->plines.push_back(gpb.createPipeline());
    m_dbgUtil->DBG_NAME(m_rasterPipepline->plines[eRasterSolidDoubleSided]);

    // Blend
    gpb.rasterizationState.cullMode = VK_CULL_MODE_NONE;
    VkPipelineColorBlendAttachmentState blend_state{};
    blend_state.blendEnable = VK_TRUE;
    blend_state.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blend_state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend_state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend_state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    gpb.setBlendAttachmentState(0, blend_state);
    m_rasterPipepline->plines.push_back(gpb.createPipeline());
    m_dbgUtil->DBG_NAME(m_rasterPipepline->plines[eRasterBlend]);

    // Revert Blend Mode
    blend_state.blendEnable = VK_FALSE;
    gpb.setBlendAttachmentState(0, blend_state);
  }

  // Wireframe
  {
    gpb.clearShaders();
    gpb.addShader(shaderModules[eVertex], VK_SHADER_STAGE_VERTEX_BIT);
    gpb.addShader(shaderModules[eFragmentOverlay], VK_SHADER_STAGE_FRAGMENT_BIT);
    gpb.rasterizationState.depthBiasEnable = VK_FALSE;
    gpb.rasterizationState.polygonMode     = VK_POLYGON_MODE_LINE;
    gpb.rasterizationState.lineWidth       = 1.0F;
    gpb.depthStencilState.depthWriteEnable = VK_FALSE;
    m_rasterPipepline->plines.push_back(gpb.createPipeline());
    m_dbgUtil->DBG_NAME(m_rasterPipepline->plines[eRasterWireframe]);
  }

  // Cleanup
  vkDestroyShaderModule(m_device, shaderModules[eVertex], nullptr);
  vkDestroyShaderModule(m_device, shaderModules[eFragment], nullptr);
  vkDestroyShaderModule(m_device, shaderModules[eFragmentOverlay], nullptr);
}


//--------------------------------------------------------------------------------------------------
// Raster commands are recorded to be replayed, this allocates that command buffer
//
void RendererRaster::createRecordCommandBuffer()
{
  VkCommandBufferAllocateInfo alloc_info{
      .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool        = m_commandPool,
      .level              = VK_COMMAND_BUFFER_LEVEL_SECONDARY,
      .commandBufferCount = 1,
  };
  vkAllocateCommandBuffers(m_device, &alloc_info, &m_recordedSceneCmd);
}

//--------------------------------------------------------------------------------------------------
// Freeing the raster recoded command buffer
//
void RendererRaster::freeRecordCommandBuffer()
{
  vkFreeCommandBuffers(m_device, m_commandPool, 1, &m_recordedSceneCmd);
  m_recordedSceneCmd = VK_NULL_HANDLE;
}

//--------------------------------------------------------------------------------------------------
// Recording in a secondary command buffer, the raster rendering of the scene.
//
void RendererRaster::recordRasterScene(Scene& scene)
{
  nvh::ScopedTimer st(__FUNCTION__);

  createRecordCommandBuffer();

  std::vector<VkFormat> colorFormat = {m_gSuperSampleBuffers->getColorFormat(GBufferType::eSuperSample),
                                       m_gSuperSampleBuffers->getColorFormat(GBufferType::eSilhouette)};

  VkCommandBufferInheritanceRenderingInfo inheritanceRenderingInfo{
      .sType                   = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_RENDERING_INFO,
      .colorAttachmentCount    = uint32_t(colorFormat.size()),
      .pColorAttachmentFormats = colorFormat.data(),
      .depthAttachmentFormat   = m_gSuperSampleBuffers->getDepthFormat(),
      .rasterizationSamples    = VK_SAMPLE_COUNT_1_BIT,
  };

  VkCommandBufferInheritanceInfo inheritInfo{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
                                             .pNext = &inheritanceRenderingInfo};

  VkCommandBufferBeginInfo beginInfo{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
      .pInheritanceInfo = &inheritInfo,
  };

  vkBeginCommandBuffer(m_recordedSceneCmd, &beginInfo);
  renderRasterScene(m_recordedSceneCmd, scene);
  vkEndCommandBuffer(m_recordedSceneCmd);
}

//--------------------------------------------------------------------------------------------------
// Rendering the GLTF nodes (instances) contained in the list
// The list should be: solid, blendable, all
void RendererRaster::renderNodes(VkCommandBuffer cmd, Scene& scene, const std::vector<uint32_t>& nodeIDs)
{
  auto scope_dbg = m_dbgUtil->DBG_SCOPE(cmd);

  const VkDeviceSize                             offsets{0};
  const std::vector<nvh::gltf::RenderNode>&      renderNodes = scene.m_gltfScene->getRenderNodes();
  const std::vector<nvh::gltf::RenderPrimitive>& subMeshes   = scene.m_gltfScene->getRenderPrimitives();

  for(const uint32_t& nodeID : nodeIDs)
  {
    const nvh::gltf::RenderNode&      renderNode = renderNodes[nodeID];
    const nvh::gltf::RenderPrimitive& subMesh = subMeshes[renderNode.renderPrimID];  // Mesh referred by the draw object

    m_pushConst.materialID         = renderNode.materialID;
    m_pushConst.renderNodeID       = static_cast<int>(nodeID);
    m_pushConst.renderPrimID       = renderNode.renderPrimID;
    m_pushConst.selectedRenderNode = scene.getSelectedRenderNode();
    vkCmdPushConstants(cmd, m_rasterPipepline->layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(DH::PushConstantRaster), &m_pushConst);

    vkCmdBindVertexBuffers(cmd, 0, 1, &scene.m_gltfSceneVk->vertexBuffers()[renderNode.renderPrimID].position.buffer, &offsets);
    vkCmdBindIndexBuffer(cmd, scene.m_gltfSceneVk->indices()[renderNode.renderPrimID].buffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, subMesh.indexCount, 1, 0, 0, 0);
  }
}

//--------------------------------------------------------------------------------------------------
// Render the entire scene for raster. Splitting the solid and blend-able element and rendering
// on top, the wireframe if active.
// This is done in a recoded command buffer to be replay
void RendererRaster::renderRasterScene(VkCommandBuffer cmd, Scene& scene)
{
  auto scope_dbg = m_dbgUtil->DBG_SCOPE(cmd);

  const VkExtent2D& render_size = m_gSuperSampleBuffers->getSize();

  const VkViewport viewport{0.0F, 0.0F, static_cast<float>(render_size.width), static_cast<float>(render_size.height),
                            0.0F, 1.0F};
  vkCmdSetViewport(cmd, 0, 1, &viewport);

  const VkRect2D scissor{{0, 0}, {render_size.width, render_size.height}};
  vkCmdSetScissor(cmd, 0, 1, &scissor);


  std::vector dset = {scene.m_sceneSet->getSet(), scene.m_hdrDome->getDescSet(), scene.m_sky->getDescriptorSet()};
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_rasterPipepline->layout, 0,
                          static_cast<uint32_t>(dset.size()), dset.data(), 0, nullptr);
  // Draw solid
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_rasterPipepline->plines[eRasterSolid]);
  renderNodes(cmd, scene, scene.m_gltfScene->getShadedNodes(nvh::gltf::Scene::eRasterSolid));
  //
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_rasterPipepline->plines[eRasterSolidDoubleSided]);
  renderNodes(cmd, scene, scene.m_gltfScene->getShadedNodes(nvh::gltf::Scene::eRasterSolidDoubleSided));
  // Draw blend-able
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_rasterPipepline->plines[eRasterBlend]);
  renderNodes(cmd, scene, scene.m_gltfScene->getShadedNodes(nvh::gltf::Scene::eRasterBlend));

  if(g_rasterSettings.showWireframe)
  {
    // Draw wireframe
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_rasterPipepline->plines[eRasterWireframe]);
    renderNodes(cmd, scene, scene.m_gltfScene->getShadedNodes(nvh::gltf::Scene::eRasterAll));
  }
}

//--------------------------------------------------------------------------------------------------
// Create the raster renderer
//
std::unique_ptr<Renderer> makeRendererRaster()
{
  return std::make_unique<RendererRaster>();
}

}  // namespace gltfr
