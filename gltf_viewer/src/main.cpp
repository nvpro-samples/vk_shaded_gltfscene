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

#define VMA_IMPLEMENTATION

#if defined(WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <thread>


#include "nvh/commandlineparser.hpp"
#include "nvh/fileoperations.hpp"
#include "nvvk/raypicker_vk.hpp"
#include "nvvk/sbtwrapper_vk.hpp"
#include "nvvkhl/element_camera.hpp"
#include "nvvkhl/element_gui.hpp"
#include "nvvkhl/element_logger.hpp"
#include "nvvkhl/element_nvml.hpp"
#include "nvvkhl/element_profiler.hpp"
#include "nvvkhl/gbuffer.hpp"
#include "nvvkhl/gltf_scene_rtx.hpp"
#include "nvvkhl/hdr_env.hpp"
#include "nvvkhl/hdr_env_dome.hpp"
#include "nvvkhl/sky.hpp"
#include "nvvkhl/tonemap_postprocess.hpp"

#include "gltf_viewer.hpp"

std::shared_ptr<nvvkhl::ElementCamera>   g_elem_camera;  // Is accessed elsewhere in the App
std::shared_ptr<nvvkhl::ElementProfiler> g_profiler;     // GPU profiler


//////////////////////////////////////////////////////////////////////////
///
///
///
auto main(int argc, char** argv) -> int
{
  static nvvkhl::SampleAppLog g_logger;
  nvprintSetCallback([](int level, const char* fmt) { g_logger.addLog(level, "%s", fmt); });
  g_logger.setLogLevel(LOGBITS_INFO);

  nvvkhl::ApplicationCreateInfo spec;
  spec.name             = PROJECT_NAME " Example";
  spec.vSync            = false;
  spec.vkSetup.apiMajor = 1;
  spec.vkSetup.apiMinor = 3;

  spec.vkSetup.addDeviceExtension(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
  // #VKRay: Activate the ray tracing extension
  VkPhysicalDeviceAccelerationStructureFeaturesKHR accel_feature{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
  spec.vkSetup.addDeviceExtension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, false,
                                  &accel_feature);  // To build acceleration structures
  VkPhysicalDeviceRayTracingPipelineFeaturesKHR rt_pipeline_feature{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
  spec.vkSetup.addDeviceExtension(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME, false,
                                  &rt_pipeline_feature);                            // To use vkCmdTraceRaysKHR
  spec.vkSetup.addDeviceExtension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);  // Required by ray tracing pipeline
  VkPhysicalDeviceRayQueryFeaturesKHR ray_query_features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
  spec.vkSetup.addDeviceExtension(VK_KHR_RAY_QUERY_EXTENSION_NAME, false, &ray_query_features);  // Used for picking
  spec.vkSetup.addDeviceExtension(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
  VkPhysicalDeviceFragmentShaderBarycentricFeaturesNV baryFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_BARYCENTRIC_FEATURES_NV};
  spec.vkSetup.addDeviceExtension(VK_NV_FRAGMENT_SHADER_BARYCENTRIC_EXTENSION_NAME, false, &baryFeatures);

  // Request for extra Queue for loading in parallel
  spec.vkSetup.addRequestedQueue(VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT, 1, 1.0F);

  // Create the application
  auto app = std::make_unique<nvvkhl::Application>(spec);

  // Create Elements of the application
  auto gltf_viewer = std::make_shared<GltfViewer>();
  g_elem_camera    = std::make_shared<nvvkhl::ElementCamera>();
  g_profiler       = std::make_shared<nvvkhl::ElementProfiler>(false);

  app->addElement(gltf_viewer);                                                // Our sample

  g_profiler->setLabelUsage(false);

  // Search paths
  const std::vector<std::string> default_search_paths = {NVPSystem::exePath() + PROJECT_DOWNLOAD_RELDIRECTORY};

  // Default scene elements
  std::string in_filename = nvh::findFile(R"(FlightHelmet/FlightHelmet.gltf)", default_search_paths, true);
  std::string in_hdr      = nvh::findFile("daytime.hdr", default_search_paths, true);

  // Parsing arguments
  bool                   print_help{false};
  nvh::CommandLineParser args("Test Parser");
  args.addArgument({"-f", "--filename"}, &in_filename, "Input filename");
  args.addArgument({"--hdr"}, &in_hdr, "Input HDR");
  args.addArgument({"-h", "--help"}, &print_help, "Print Help");
  const bool result = args.parse(argc, argv);
  if(!result || print_help)
  {
    args.printHelp();
    return 1;
  }

  // Loading HDR and scene; default or command line
  gltf_viewer->onFileDrop(in_hdr.c_str());


  app->addElement(std::make_shared<nvvkhl::ElementDefaultMenu>());             // Menu / Quit

  app->addElement(g_elem_camera);                                              // Controlling the camera movement
  app->addElement(std::make_unique<nvvkhl::ElementLogger>(&g_logger, false));  // Add logger window
  app->addElement(std::make_unique<nvvkhl::ElementNvml>(false));               // Add logger window
  app->addElement(g_profiler);                                                 // GPU Profiler


  while(gltf_viewer->isBusy())
  {  // Making sure the HDR is loaded before loading the scene
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  gltf_viewer->onFileDrop(in_filename.c_str());

  // Start Application: which will loop and call on"Functions" for all Elements
  app->run();

  // Cleanup
  gltf_viewer.reset();
  app.reset();

  return 0;
}
