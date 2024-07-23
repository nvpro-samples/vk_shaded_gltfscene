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

#include <thread>

#if defined(WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "slang.h"

// ImGui headers
#include "imgui/imgui_axis.hpp"
#include "imgui/imgui_camera_widget.h"
#include "imgui/imgui_helper.h"

// NV Vulkan headers
#include "nvvk/extensions_vk.hpp"
#include "nvvk/raypicker_vk.hpp"
#include "nvvkhl/element_benchmark_parameters.hpp"
#include "nvvkhl/element_camera.hpp"
#include "nvvkhl/element_dbgprintf.hpp"
#include "nvvkhl/element_logger.hpp"
#include "nvvkhl/element_nvml.hpp"
#include "nvvkhl/element_profiler.hpp"
#include "nvvkhl/tonemap_postprocess.hpp"

// Application specific headers
#include "busy_window.hpp"
#include "renderer.hpp"
#include "scene.hpp"
#include "settings.hpp"
#include "settings_handler.hpp"
#include "utilities.hpp"
#include "vk_context.hpp"

// #define USE_DGBPRINTF
// With USE_DGBPRINTF defined, the application will have the capability to use the debug printf extension.
// And it is possible to use the debug printf in the shaders, by including the following:
// if(useDebug)
//   debugPrintfEXT("Hello from shader\n");
// This will print "Hello from shader" in the debug printf window ONLY for the pixels under the mouse cursor, when the mouse button is pressed.


std::shared_ptr<nvvkhl::ElementCamera>              g_elemCamera;       // The camera element (UI and movement)
std::shared_ptr<nvvkhl::ElementProfiler>            g_profiler;         // GPU profiler
std::shared_ptr<nvvkhl::ElementBenchmarkParameters> g_benchmarkParams;  // Benchmark parameters
std::shared_ptr<nvvkhl::ElementDbgPrintf>           g_dbgPrintf;
std::shared_ptr<nvvkhl::SampleAppLog>               g_logger;                  // Log window
std::vector<std::string>                            g_applicationSearchPaths;  // Search paths for resources


// Default scene elements
std::string g_inFilename;
std::string g_inHdr;

namespace PE = ImGuiH::PropertyEditor;

namespace gltfr {


class GltfRendererElement : public nvvkhl::IAppElement
{
public:
  GltfRendererElement() { addSettingsHandler(); }

  //--------------------------------------------------------------------------------------------------
  // Called at the beginning of the application
  //
  void onAttach(nvvkhl::Application* app) override
  {
    m_app = app;

    // Getting all required resources
    gltfr::VulkanInfo ctx;
    ctx.device         = app->getDevice();
    ctx.physicalDevice = app->getPhysicalDevice();
    ctx.GCT0           = {app->getQueue(0).queue, app->getQueue(0).familyIndex};  // See creation of queues in main()
    ctx.GCT1           = {app->getQueue(1).queue, app->getQueue(1).familyIndex};
    ctx.compute        = {app->getQueue(2).queue, app->getQueue(2).familyIndex};
    ctx.transfer       = {app->getQueue(3).queue, app->getQueue(3).familyIndex};

    m_resources.init(ctx);
    m_scene.init(m_resources);

    nvvk::ResourceAllocator* alloc         = m_resources.m_allocator.get();
    uint32_t                 c_queue_index = ctx.compute.familyIndex;

    m_tonemapper = std::make_unique<nvvkhl::TonemapperPostProcess>(ctx.device, alloc);  // Tonemapper utility
    m_picker = std::make_unique<nvvk::RayPickerKHR>(ctx.device, ctx.physicalDevice, alloc, c_queue_index);  // RTX Picking utility

    m_emptyRenderer = makeRendererEmpty();
    m_tonemapper->createComputePipeline();

    if(!g_inHdr.empty())
    {
      // Wait for the HDR to be loaded
      m_scene.load(m_resources, g_inHdr);
      m_settings.envSystem    = Settings::eHdr;
      m_settings.maxLuminance = m_scene.m_hdrEnv->getIntegral();
    }
    if(!g_inFilename.empty())
    {
      // Load the glTF file in a separate thread
      onFileDrop(g_inFilename.c_str());
    }
  }

  //--------------------------------------------------------------------------------------------------
  void onDetach() override
  {
    vkDeviceWaitIdle(m_resources.ctx.device);
    m_scene.deinit(m_resources);
    m_emptyRenderer->deinit(m_resources);
    if(m_renderer != nullptr)
      m_renderer->deinit(m_resources);
    m_renderer.reset();
    m_picker->destroy();
  }

  //--------------------------------------------------------------------------------------------------
  void onResize(uint32_t width, uint32_t height) override { m_resources.resizeGbuffers({width, height}); }

  //--------------------------------------------------------------------------------------------------
  void onRender(VkCommandBuffer cmdBuf) override
  {
    if(m_busy)
      return;

    // Handle changes that have happened since last frame
    handleChanges();

    if(m_renderer && m_scene.isValid())
    {
      // Animate, update Vulkan buffers: scene, frame, acceleration structures
      // It could stop rendering if the scene is not ready or reached max frames
      if(m_scene.processFrame(cmdBuf, m_settings))
      {
        m_renderer->render(cmdBuf, m_resources, m_scene, m_settings, *g_profiler.get());
      }
    }
    else
    {
      m_emptyRenderer->render(cmdBuf, m_resources, m_scene, m_settings, *g_profiler.get());
    }


    // Apply tone mapper to the final image
    {
      auto sec = g_profiler->timeRecurring("Tonemapper", cmdBuf);
      setTonemapperInputOutput();
      m_tonemapper->runCompute(cmdBuf, m_resources.m_finalImage->getSize());
    }
  }

  //--------------------------------------------------------------------------------------------------
  // Set the input and output of the tonemapper
  // Input - the result of the renderer
  // Output - the final image (resources finalImage)
  void setTonemapperInputOutput()
  {
    VkDescriptorImageInfo inputImage  = m_resources.m_finalImage->getDescriptorImageInfo();
    VkDescriptorImageInfo outputImage = inputImage;
    if(m_renderer != nullptr)
    {
      inputImage = m_renderer->getOutputImage();
    }
    m_tonemapper->updateComputeDescriptorSets(inputImage, outputImage);  // To
  }

  //--------------------------------------------------------------------------------------------------
  // Main UI of the application
  // Will display the settings, the viewport and the busy window
  void onUIRender() override
  {
    ImGui::Begin("Settings");
    {
      PE::begin();
      {
        if(PE::Combo("Renderer", (int*)&m_settings.renderSystem, Settings::rendererNames, IM_ARRAYSIZE(Settings::rendererNames)))
        {
          createRenderers();
        }
      }
      PE::end();
      if(m_renderer != nullptr && m_renderer->onUI())
      {
        m_scene.resetFrameCount();
      }
      if(ImGui::CollapsingHeader("Camera"))
      {
        ImGuiH::CameraWidget();
      }
      m_settings.onUI();
      if(ImGui::CollapsingHeader("Tonemapper"))
      {
        m_tonemapper->onUI();
      }

      m_scene.onUI(m_resources, m_settings, m_app->getWindowHandle());
    }
    ImGui::End();  // Settings


    {  // Rendering Viewport
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
      ImGui::Begin("Viewport");

      // Pick under mouse cursor
      if(ImGui::IsWindowHovered(ImGuiFocusedFlags_RootWindow) && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)
         || ImGui::IsKeyPressed(ImGuiKey_Space))
      {
        screenPicking();
      }

      // Display the G-Buffer image
      ImVec2 imgSize = ImGui::GetContentRegionAvail();
      ImGui::Image(m_resources.m_finalImage->getDescriptorSet(), imgSize);

      // Adding Axis at the bottom left corner of the viewport
      if(m_settings.showAxis)
      {
        float  size        = 25.F;
        ImVec2 window_pos  = ImGui::GetWindowPos();
        ImVec2 window_size = ImGui::GetWindowSize();
        ImVec2 offset      = ImVec2(size * 1.1F, -size * 1.1F) * ImGui::GetWindowDpiScale();
        ImVec2 pos         = ImVec2(window_pos.x, window_pos.y + window_size.y) + offset;
        ImGuiH::Axis(pos, CameraManip.getMatrix(), size);
      }

      ImGui::End();
      ImGui::PopStyleVar();
    }

    if(m_busy)
      showBusyWindow("Loading");
  }

  //--------------------------------------------------------------------------------------------------
  // This is the toolbar at the top of the window
  // Adding menu items for loading, saving, and changing the view
  void onUIMenu() override
  {
    bool v_sync = m_app->isVsync();

    auto getSaveImage = [&]() {
      std::string filename =
          NVPSystem::windowSaveFileDialog(m_app->getWindowHandle(), "Save Image", "PNG(.png),JPG(.jpg)|*.png;*.jpg");
      if(!filename.empty())
      {
        std::filesystem::path ext = std::filesystem::path(filename).extension();
      }
      return filename;
    };

    windowTitle();
    bool loadFile       = ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_O);
    bool saveFile       = ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S);
    bool saveScreenFile = ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiMod_Alt | ImGuiKey_S);
    bool saveImageFile  = ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S);
    bool closeApp       = ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Q);
    if(ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_V))
    {
      v_sync = !v_sync;
    }

    if(ImGui::BeginMenu("File"))
    {
      loadFile |= ImGui::MenuItem("Load", "Ctrl+O");
      saveFile |= ImGui::MenuItem("Save As", "Ctrl+S");
      ImGui::Separator();
      saveImageFile |= ImGui::MenuItem("Save Image", "Ctrl+Shift+S");
      saveScreenFile |= ImGui::MenuItem("Save Screen", "Ctrl+Alt+Shift+S");
      ImGui::Separator();
      closeApp |= ImGui::MenuItem("Exit", "Ctrl+Q");
      ImGui::EndMenu();
    }

    // De-selecting the object
    if(ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
      m_scene.selectRenderNode(-1);
    }

    bool fitScene      = ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_F);
    bool fitObject     = ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F);
    bool reloadShaders = ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_R);
    if(ImGui::BeginMenu("View"))
    {
      fitScene |= ImGui::MenuItem("Fit Scene", "Ctrl+Shift+F");
      fitObject |= ImGui::MenuItem("Fit Object", "Ctrl+F");
      reloadShaders |= ImGui::MenuItem("Reload Shaders", "Ctrl+R");
      ImGui::Separator();
      ImGui::MenuItem("V-Sync", "Ctrl+Shift+V", &v_sync);
      ImGui::EndMenu();
    }

    if(loadFile)
    {
      std::string filename = NVPSystem::windowOpenFileDialog(m_app->getWindowHandle(), "Load glTF | HDR",
                                                             "glTF(.gltf, .glb), OBJ(.obj), HDR(.hdr)|*.gltf;*.glb;*.obj;*.hdr");
      onFileDrop(filename.c_str());
    }

    if(saveFile)
    {
      m_scene.save(NVPSystem::windowSaveFileDialog(m_app->getWindowHandle(), "Save glTF", "glTF(.gltf)|*.gltf"));
    }

    if(saveScreenFile)
    {
      std::string filename = getSaveImage();
      if(!filename.empty())
      {
        m_app->screenShot(filename, 100);
      }
    }

    if(saveImageFile)
    {
      std::string filename = getSaveImage();
      if(!filename.empty())
      {
        saveRenderedImage(filename);
      }
    }

    if(fitScene)
    {
      m_scene.fitSceneToView();
    }

    if(fitObject)
    {
      m_scene.fitObjectToView();
    }

    if(reloadShaders)
    {
      createRenderers();
    }

    if(m_app->isVsync() != v_sync)
    {
      m_app->setVsync(v_sync);
    }

    if(closeApp)
    {
      m_app->close();
    }

#ifndef NDEBUG
    static bool s_showDemo{false};
    static bool s_showDemoPlot{false};
    if(ImGui::BeginMenu("Debug"))
    {
      ImGui::MenuItem("Show ImGui Demo", nullptr, &s_showDemo);
      ImGui::MenuItem("Show ImPlot Demo", nullptr, &s_showDemoPlot);
      ImGui::EndMenu();
    }
    if(s_showDemo)
      ImGui::ShowDemoWindow(&s_showDemo);
    if(s_showDemoPlot)
      ImPlot::ShowDemoWindow(&s_showDemoPlot);
#endif  // !NDEBUG
  }

  void saveRenderedImage(const std::string& filename)
  {
    VkDevice         device         = m_app->getDevice();
    VkPhysicalDevice physicalDevice = m_app->getPhysicalDevice();
    VkImage          srcImage       = m_resources.m_finalImage->getColorImage();
    VkExtent2D       size           = m_resources.m_finalImage->getSize();
    VkImage          dstImage       = {};
    VkDeviceMemory   dstImageMemory = {};

    VkCommandBuffer cmd = m_app->createTempCmdBuffer();
    imageToRgba8Linear(cmd, device, physicalDevice, srcImage, size, dstImage, dstImageMemory);
    m_app->submitAndWaitTempCmdBuffer(cmd);

    saveImageToFile(device, dstImage, dstImageMemory, size, filename);

    // Clean up resources
    vkUnmapMemory(device, dstImageMemory);
    vkFreeMemory(device, dstImageMemory, nullptr);
    vkDestroyImage(device, dstImage, nullptr);
  }

  //--------------------------------------------------------------------------------------------------
  // Handle the file drop event: glTF, HDR, OBJ
  //
  void onFileDrop(const char* filename) override
  {
    if(m_busy)
      return;
    m_busy = true;
    vkDeviceWaitIdle(m_resources.ctx.device);

    // Loading file in a separate thread
    std::string loadFile = filename;
    std::thread([&, loadFile]() {
      m_scene.load(m_resources, loadFile);
      m_busy = false;
    }).detach();

    // Visualize the HDR if it is a HDR file
    std::filesystem::path ext = std::filesystem::path(filename).extension();
    if(ext == ".hdr")
    {
      m_settings.envSystem    = Settings::eHdr;
      m_settings.maxLuminance = m_scene.m_hdrEnv->getIntegral();
    }
  }

  //--------------------------------------------------------------------------------------------------
  // Change the window title to display real-time informations
  //
  void windowTitle()
  {
    // Window Title
    static float dirty_timer = 0.0F;
    dirty_timer += ImGui::GetIO().DeltaTime;
    if(dirty_timer > 1.0F)  // Refresh every seconds
    {
      const VkExtent2D&     size = m_app->getViewportSize();
      std::filesystem::path p    = m_scene.getFilename();
      std::string           text =
          fmt::format("{} - {}x{} | {:.0f} FPS / {:.3f}ms | Frame {}", p.filename().string(), size.width, size.height,
                      ImGui::GetIO().Framerate, 1000.F / ImGui::GetIO().Framerate, m_scene.m_sceneFrameInfo.frameCount);

      glfwSetWindowTitle(m_app->getWindowHandle(), text.c_str());
      dirty_timer = 0;
    }
  }

private:
  //--------------------------------------------------------------------------------------------------
  // Invoked when the user double-clicks on the viewport
  // It uses the picking utility to find the object under the mouse cursor
  //
  void screenPicking()
  {
    // Pick under mouse cursor
    if(!m_scene.isValid() || !m_scene.m_gltfSceneRtx && m_scene.m_gltfSceneRtx->tlas() == VK_NULL_HANDLE)
      return;

    // This need to called withing ImGui::Begin("Viewport");
    ImVec2       mouse_pos       = ImGui::GetMousePos();
    const ImVec2 main_size       = ImGui::GetContentRegionAvail();
    const ImVec2 corner          = ImGui::GetCursorScreenPos();  // Corner of the viewport
    const float  aspect_ratio    = main_size.x / main_size.y;
    mouse_pos                    = mouse_pos - corner;
    const ImVec2 local_mouse_pos = mouse_pos / main_size;

    // Finding current camera matrices
    const glm::mat4& view = CameraManip.getMatrix();
    glm::mat4        proj = glm::perspectiveRH_ZO(glm::radians(CameraManip.getFov()), aspect_ratio, 0.1F, 1000.0F);
    proj[1][1] *= -1;

    // Setting up the data to do picking
    nvvk::RayPickerKHR::PickInfo pickInfo{
        .modelViewInv   = glm::inverse(view),
        .perspectiveInv = glm::inverse(proj),
        .pickX          = local_mouse_pos.x,
        .pickY          = local_mouse_pos.y,
    };

    // Run and wait for result
    VkCommandBuffer cmd = m_app->createTempCmdBuffer();
    m_picker->run(cmd, pickInfo);
    m_app->submitAndWaitTempCmdBuffer(cmd);


    // Retrieving picking information
    const nvvk::RayPickerKHR::PickResult pr = m_picker->getResult();
    if(pr.instanceID == ~0)
    {
      LOGI("Nothing Hit\n");
      m_scene.selectRenderNode(-1);
      return;
    }

    if(pr.hitT <= 0.F)
    {
      LOGI("Hit Distance == 0.0\n");
      return;
    }

    // Find where the hit point is and set the interest position
    const glm::vec3 world_pos = glm::vec3(pr.worldRayOrigin + pr.worldRayDirection * pr.hitT);
    glm::vec3       eye;
    glm::vec3       center;
    glm::vec3       up;
    CameraManip.getLookat(eye, center, up);
    CameraManip.setLookat(eye, world_pos, up, false);

    // Logging picking info.
    const nvh::gltf::RenderNode& renderNode = m_scene.m_gltfScene->getRenderNodes()[pr.instanceID];
    const tinygltf::Node&        node       = m_scene.m_gltfScene->getModel().nodes[renderNode.refNodeID];
    m_scene.selectRenderNode(pr.instanceID);

    LOGI("Node Name: %s\n", node.name.c_str());
    LOGI(" - GLTF: NodeID: %d, MeshID: %d, TriangleId: %d\n", renderNode.refNodeID, node.mesh, pr.primitiveID);
    LOGI(" - Render: RenderNode: %d, RenderPrim: %d\n", pr.instanceID, pr.instanceCustomIndex);
    LOGI("{%3.2f, %3.2f, %3.2f}, Dist: %3.2f\n", world_pos.x, world_pos.y, world_pos.z, pr.hitT);
  }

  //--------------------------------------------------------------------------------------------------
  // Create the renderer based on the settings
  // Note: the tonemapper input/output should be re-adjusted after creating the renderer
  //
  void createRenderers()
  {
    nvh::ScopedTimer st(__FUNCTION__);

    vkDeviceWaitIdle(m_resources.ctx.device);
    m_resources.resetSlangCompiler();  // Resetting the Slang session
    m_renderer.reset();
    if(!m_scene.isValid())
      return;
    switch(m_settings.renderSystem)
    {
      case Settings::ePathtracer:
        m_renderer = makeRendererPathtracer();
        break;
      case Settings::eRaster:
        m_renderer = makeRendererRaster();
        break;
    }
    if(m_renderer)
    {
      bool result = m_renderer->init(m_resources, m_scene);
      if(result == false)
      {
        LOGE("Failed to create renderer: %s\n", Settings::rendererNames[m_settings.renderSystem]);
        m_renderer.reset();
      }
    }
    m_scene.resetFrameCount();
  }

  //--------------------------------------------------------------------------------------------------
  // Handle changes that have happened since last frame
  // - Scene changes
  // - Resolution changes
  //
  void handleChanges()
  {
    if(m_scene.hasHdrChanged())
    {
      m_settings.maxLuminance = m_scene.m_hdrEnv->getIntegral();
    }

    // Scene changed (new scene)
    if(m_scene.hasSceneChanged())
    {
      vkDeviceWaitIdle(m_resources.ctx.device);
      createRenderers();
      m_picker->setTlas(m_scene.m_gltfSceneRtx->tlas());  // The screen picker is using the new TLAS
    }

    // Letting the renderer handle any changes
    if(m_renderer)
    {
      m_renderer->handleChange(m_resources, m_scene);
    }

    // Clearing flags changed
    m_scene.setSceneChanged(false);
    m_scene.setHdrChanged(false);
    m_resources.setGBuffersChanged(false);
  }

  // This goes in the .ini file and remember the settings of the application
  void addSettingsHandler()
  {
    m_settingsHandler = std::make_unique<SettingsHandler>("GLTFRenderer");
    m_settingsHandler->setSetting("Renderer", reinterpret_cast<int*>(&m_settings.renderSystem));
    m_settingsHandler->setSetting("MaxFrames", &m_settings.maxFrames);
    m_settingsHandler->setSetting("ShowAxis", &m_settings.showAxis);
    m_settingsHandler->setSetting("SilhouetteColor", &m_settings.silhouetteColor);
    m_settingsHandler->addImGuiHandler();
  }


  bool                                           m_busy = false;
  nvvkhl::Application*                           m_app  = nullptr;
  Resources                                      m_resources;
  Settings                                       m_settings;
  Scene                                          m_scene;
  std::unique_ptr<gltfr::Renderer>               m_emptyRenderer{};
  std::unique_ptr<gltfr::Renderer>               m_renderer{};
  std::unique_ptr<nvvkhl::TonemapperPostProcess> m_tonemapper{};
  std::unique_ptr<nvvk::RayPickerKHR>            m_picker{};
  std::unique_ptr<SettingsHandler>               m_settingsHandler;
};


}  // namespace gltfr

//////////////////////////////////////////////////////////////////////////
///
///
///
auto main(int argc, char** argv) -> int
{
#ifdef USE_DGBPRINTF
  g_dbgPrintf = std::make_shared<nvvkhl::ElementDbgPrintf>();
#endif

  // Search paths
  g_applicationSearchPaths.push_back(NVPSystem::exePath() + PROJECT_DOWNLOAD_RELDIRECTORY);
  g_applicationSearchPaths.push_back(std::string("GLSL_" PROJECT_NAME));
  // Search path for shaders within the project
  g_applicationSearchPaths.push_back(NVPSystem::exePath() + std::string(PROJECT_RELDIRECTORY) + "shaders");
  g_applicationSearchPaths.push_back(NVPRO_CORE_DIR);
  // INSTALL_RELDIRECTORY is defined in CMakeLists.txt
  g_applicationSearchPaths.push_back(NVPSystem::exePath() + std::string("GLSL_" PROJECT_NAME));
  g_applicationSearchPaths.push_back(NVPSystem::exePath() + "media");


  // Create the logger, and redirect the printf to the logger
  g_logger = std::make_shared<nvvkhl::SampleAppLog>();
  nvprintSetCallback([](int level, const char* fmt) { g_logger->addLog(level, "%s", fmt); });
  g_logger->setLogLevel(LOGBITS_INFO);


  // Vulkan Context creation information
  VkContextSettings vkSetup;
  nvvkhl::addSurfaceExtensions(vkSetup.instanceExtensions);
  vkSetup.instanceExtensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  vkSetup.deviceExtensions.emplace_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
  // All Vulkan extensions required by the sample
  VkPhysicalDeviceAccelerationStructureFeaturesKHR accel_feature{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
  VkPhysicalDeviceRayTracingPipelineFeaturesKHR rt_pipeline_feature{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
  VkPhysicalDeviceRayQueryFeaturesKHR ray_query_features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
  VkPhysicalDeviceFragmentShaderBarycentricFeaturesNV baryFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_BARYCENTRIC_FEATURES_NV};
  VkPhysicalDeviceShaderObjectFeaturesEXT shaderObjFeature{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT};
  vkSetup.deviceExtensions.emplace_back(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
  vkSetup.deviceExtensions.emplace_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, &accel_feature);
  vkSetup.deviceExtensions.emplace_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME, &rt_pipeline_feature);
  vkSetup.deviceExtensions.emplace_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
  vkSetup.deviceExtensions.emplace_back(VK_KHR_RAY_QUERY_EXTENSION_NAME, &ray_query_features);
  vkSetup.deviceExtensions.emplace_back(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
  vkSetup.deviceExtensions.emplace_back(VK_NV_FRAGMENT_SHADER_BARYCENTRIC_EXTENSION_NAME, &baryFeatures);
  vkSetup.deviceExtensions.emplace_back(VK_EXT_SHADER_OBJECT_EXTENSION_NAME, &shaderObjFeature);

  // Request the creation of all needed queues
  vkSetup.queues = {VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_TRANSFER_BIT | VK_QUEUE_COMPUTE_BIT,  // GTC for rendering
                    VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_TRANSFER_BIT | VK_QUEUE_COMPUTE_BIT,  // GTC for loading in parallel
                    VK_QUEUE_COMPUTE_BIT,                                                  // Compute
                    VK_QUEUE_TRANSFER_BIT};                                                // Transfer

#ifdef USE_DGBPRINTF
  vkSetup.instanceCreateInfoExt = g_dbgPrintf->getFeatures();  // Adding the debug printf extension
  vkSetup.ignoreDbgMessages.insert(0x76589099);                // Truncate the message when too long
#endif                                                         // USE_DGBPRINTF

  // Creating the Vulkan context
  auto vkctx = std::make_unique<VkContext>(vkSetup);

  // Loading Vulkan extension pointers
  load_VK_EXTENSIONS(vkctx->getInstance(), vkGetInstanceProcAddr, vkctx->getDevice(), vkGetDeviceProcAddr);

  // Setup the application information
  nvvkhl::ApplicationCreateInfo spec;
  spec.name           = PROJECT_NAME " Sample";
  spec.vSync          = false;
  spec.instance       = vkctx->getInstance();
  spec.device         = vkctx->getDevice();
  spec.physicalDevice = vkctx->getPhysicalDevice();
  // Adding all queues
  for(auto& q : vkctx->getQueueInfos())
    spec.queues.emplace_back(q.queue, q.familyIndex, q.queueIndex);

  // Create the application
  auto app = std::make_unique<nvvkhl::Application>(spec);

  // Create Elements of the application
  g_elemCamera      = std::make_shared<nvvkhl::ElementCamera>();
  g_profiler        = std::make_shared<nvvkhl::ElementProfiler>(false);
  g_benchmarkParams = std::make_shared<nvvkhl::ElementBenchmarkParameters>(argc, argv);
  auto gltfRenderer = std::make_shared<gltfr::GltfRendererElement>();  // This is the main element of the application

  // Parsing arguments
  g_benchmarkParams->parameterLists().addFilename(".gltf|load a file", &g_inFilename);
  g_benchmarkParams->parameterLists().add("hdr|load a HDR", &g_inHdr);
  g_benchmarkParams->setProfiler(g_profiler);  // Linking the profiler to the benchmark parameters

  app->addElement(g_benchmarkParams);  // Benchmark/tests and parameters
  app->addElement(gltfRenderer);       // Rendering the glTF scene
  app->addElement(g_elemCamera);       // Controlling the camera movement
  app->addElement(g_profiler);         // GPU Profiler
#ifdef USE_DGBPRINTF
  app->addElement(g_dbgPrintf);                                                     // Debug printf
#endif                                                                              // USE_DGBPRINTF
  app->addElement(std::make_unique<nvvkhl::ElementLogger>(g_logger.get(), false));  // Add logger window
  app->addElement(std::make_unique<nvvkhl::ElementNvml>(false));                    // Add GPU monitor

  g_profiler->setLabelUsage(false);  // Do not use labels for the profiler

  // Start Application: which will loop and call on"Functions" for all Elements
  app->run();

  // Cleanup
  gltfRenderer.reset();
  app.reset();
  vkctx.reset();

  return 0;
}
