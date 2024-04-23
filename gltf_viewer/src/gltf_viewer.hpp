/*
 * Copyright (c) 2022, NVIDIA CORPORATION.  All rights reserved.
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
 * SPDX-FileCopyrightText: Copyright (c) 2014-2022 NVIDIA CORPORATION
 * SPDX-License-Identifier: Apache-2.0
 */

#include "nvvk/resourceallocator_vk.hpp"
#include "nvvkhl/application.hpp"
#include "nvvkhl/pipeline_container.hpp"
#include "nvvkhl/shaders/dh_lighting.h"
#include "shaders/device_host.h"
#include "nvvkhl/alloc_vma.hpp"


namespace nvvk {
class DebugUtil;
class DescriptorSetContainer;
struct RayPickerKHR;
class SBTWrapper;
}  // namespace nvvk

namespace nvvkhl {
class AllocVma;
class GBuffer;
class HdrEnv;
class HdrEnvDome;
class Scene;
class SceneRtx;
class SceneVk;
class SkyDome;
struct TonemapperPostProcess;
}  // namespace nvvkhl

//////////////////////////////////////////////////////////////////////////
/*

 This sample can load GLTF scene and render using the raster or RTX (path tracer)

*/
//////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////
/// </summary> Ray trace multiple primitives
class GltfViewer : public nvvkhl::IAppElement
{
  enum GBufferType
  {
    eLdr,     // Tone mapped (display image)
    eResult,  // Result from Path tracer / raster
  };
  struct Settings
  {
    Settings()
    {
      // Default light
      lights.resize(1);
      lights[0] = nvvkhl_shaders::defaultLight();
    }

    enum EnvSystem
    {
      eSky,
      eHdr,
    };

    enum RenderSystem
    {
      ePathtracer,
      eRaster,
    };

    int          maxFrames     = 200000;
    int          maxSamples    = 1;
    int          maxDepth      = 5;
    bool         showAxis      = true;
    bool         showWireframe = false;
    EnvSystem    envSystem     = eHdr;
    RenderSystem renderSystem  = ePathtracer;
    float        envIntensity  = 1.F;
    float        envRotation   = 0.F;
    float        maxLuminance  = 1000.0F;  // For firefly


    std::vector<nvvkhl_shaders::Light> lights;
  } m_settings;

public:
  GltfViewer();
  ~GltfViewer() override = default;


  void onAttach(nvvkhl::Application* app) override;
  void onDetach() override;
  void onResize(uint32_t width, uint32_t height) override;
  void onUIMenu() override;
  void onFileDrop(const char* filename) override;
  void onUIRender() override;
  void onRender(VkCommandBuffer cmd) override;
  void createHdr(const std::string& filename);

  bool isBusy() const { return m_busy; }

private:
  enum PipelineType
  {
    eRasterSolid,
    eRasterSolidDoubleSided,
    eRasterBlend,
    eRasterWireframe
  };

  void                  createScene(const std::string& filename);
  void                  createGbuffers(const glm::vec2& size);
  void                  createVulkanBuffers();
  void                  createRtxSet();
  void                  createSceneSet();
  void                  createRasterPipeline();
  void                  createRtxPipeline();
  void                  writeRtxSet();
  void                  writeSceneSet();
  bool                  updateFrame();
  void                  resetFrame();
  void                  windowTitle();
  void                  screenPicking();
  void                  raytraceScene(VkCommandBuffer cmd);
  void                  createRecordCommandBuffer();
  void                  freeRecordCommandBuffer();
  void                  recordRasterScene();
  void                  renderNodes(VkCommandBuffer cmd, const std::vector<uint32_t>& nodeIDs);
  void                  renderRasterScene(VkCommandBuffer cmd);
  void                  rasterScene(VkCommandBuffer cmd);
  void                  destroyResources();
  std::vector<uint32_t> getShadedNodes(PipelineType type);

  //--------------------------------------------------------------------------------------------------
  //
  //
  nvvkhl::Application*              m_app{nullptr};
  std::unique_ptr<nvvk::DebugUtil>  m_dutil;
  std::unique_ptr<nvvkhl::AllocVma> m_alloc;
  VkCommandBuffer                   m_recordedSceneCmd{VK_NULL_HANDLE};

  glm::vec2                        m_viewSize{1, 1};
  VkClearColorValue                m_clearColor{{0.3F, 0.3F, 0.3F, 1.0F}};  // Clear color
  VkDevice                         m_device{VK_NULL_HANDLE};                // Convenient
  std::unique_ptr<nvvkhl::GBuffer> m_gBuffers;                              // G-Buffers: color + depth

  // Resources
  nvvk::Buffer         m_bFrameInfo;
  nvvk::Context::Queue m_qGCT1{};

  // Pipeline
  PushConstant              m_pushConst{};  // Information sent to the shader
  nvvkhl::PipelineContainer m_rasterPipe;
  nvvkhl::PipelineContainer m_rtxPipe;
  int                       m_frame{-1};
  FrameInfo                 m_frameInfo{};

  std::unique_ptr<nvvkhl::HdrEnv>                m_hdrEnv;
  std::unique_ptr<nvvkhl::HdrEnvDome>            m_hdrDome;
  std::unique_ptr<nvvk::DescriptorSetContainer>  m_rtxSet;    // Descriptor set
  std::unique_ptr<nvvk::DescriptorSetContainer>  m_sceneSet;  // Descriptor set
  std::unique_ptr<nvvk::RayPickerKHR>            m_picker;    // For ray picking info
  std::unique_ptr<nvvk::SBTWrapper>              m_sbt;       // Shading binding table wrapper
  std::unique_ptr<nvvkhl::Scene>                 m_scene;
  std::unique_ptr<nvvkhl::SceneRtx>              m_sceneRtx;
  std::unique_ptr<nvvkhl::SceneVk>               m_sceneVk;
  std::unique_ptr<nvvkhl::SkyDome>               m_sky;
  std::unique_ptr<nvvkhl::TonemapperPostProcess> m_tonemapper;

  bool m_busy{false};
};
