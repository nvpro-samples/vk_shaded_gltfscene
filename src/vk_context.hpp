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

#include <csignal>

#include <vulkan/vulkan_core.h>
#include <stdexcept>
#include <vector>
#include <cstring>
#include <unordered_set>

#include "nvh/nvprint.hpp"
#include "nvh/timesampler.hpp"
#include "nvvk/error_vk.hpp"
#include "nvvkhl/application.hpp"  // For QueueInfo

//--------------------------------------------------------------------------------------------------
// CATCHING VULKAN ERRORS
//--------------------------------------------------------------------------------------------------
static VKAPI_ATTR VkBool32 VKAPI_CALL VkContextDebugReport(VkDebugUtilsMessageSeverityFlagBitsEXT      messageSeverity,
                                                           VkDebugUtilsMessageTypeFlagsEXT             messageType,
                                                           const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
                                                           void*                                       userData)
{
  if(messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
  {
    auto ignoredMsg = reinterpret_cast<std::unordered_set<uint32_t>*>(userData);
    if(ignoredMsg->find(callbackData->messageIdNumber) != ignoredMsg->end())
      return VK_FALSE;
    LOGE("%s\n", callbackData->pMessage);
#if defined(_MSVC_LANG)
    __debugbreak();  // If you break here, there is a Vulkan error that needs to be fixed
                     // To ignore specific message, insert it to settings.ignoreDbgMessages
                     // ex: "MessageID = 0x30b6e267"  ->  settings.ignoreDbgMessages.push_back(0x30b6e267);
#elif defined(LINUX)
    raise(SIGTRAP);
#endif
  }
  return VK_FALSE;
}

// Vulkan Queue information
//struct QueueInfo
//{
//  uint32_t familyIndex = ~0U;
//  uint32_t queueIndex  = ~0U;
//  VkQueue  queue       = VK_NULL_HANDLE;
//};
using QueueInfo = nvvkhl::ApplicationQueue;

// Struct to hold an extension and its corresponding feature
struct ExtensionFeaturePair
{
  const char* extensionName = nullptr;
  void*       feature       = nullptr;  // [optional] Pointer to the feature structure for the extension
  bool        required      = true;     // If the extension is required
};


// Forward declarations
std::vector<VkExtensionProperties> getDeviceExtensions(VkPhysicalDevice physicalDevice);
std::string                        getVersionString(uint32_t version);
std::string                        getVendorName(uint32_t vendorID);
std::string                        getDeviceType(uint32_t deviceType);
void                               printVulkanVersion();
void                               printInstanceLayers();
void                               printInstanceExtensions(const std::vector<const char*> ext = {});
void printDeviceExtensions(VkPhysicalDevice physicalDevice, const std::vector<ExtensionFeaturePair> ext = {});
void printPhysicalDeviceProperties(const VkPhysicalDeviceProperties& properties);
void printGpus(VkInstance instance, VkPhysicalDevice usedGpu);

// Specific to the creation of Vulkan context
struct VkContextSettings
{
  std::vector<const char*> instanceExtensions = {};  // Instance extensions: VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME
  std::vector<ExtensionFeaturePair> deviceExtensions = {};  // Device extensions: {{VK_KHR_SWAPCHAIN_EXTENSION_NAME}, {VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, &accelFeature}, {OTHER}}
  std::vector<VkQueueFlags> queues = {VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_TRANSFER_BIT | VK_QUEUE_COMPUTE_BIT};  // All desired queues, first is always GTC
  std::unordered_set<uint32_t> ignoreDbgMessages;   // Ignore debug messages: 0x901f59ec
  void*       instanceCreateInfoExt = nullptr;      // Instance create info extension (ex: VkLayerSettingsCreateInfoEXT)
  const char* applicationName       = "No Engine";  // Application name
  uint32_t    apiVersion            = VK_API_VERSION_1_3;  // Vulkan API version
  VkAllocationCallbacks* alloc      = nullptr;             // Allocation callbacks
  bool    enableAllFeatures         = true;  // If true, pull all capability of `features` from the physical device
  int32_t forceGPU                  = -1;    // If != -1, use GPU index.
#if NDEBUG
  bool enableValidationLayers = false;  // Disable validation layers in release
  bool verbose                = false;
#else
  bool enableValidationLayers = true;  // Enable validation layers
  bool verbose                = true;
#endif
};


//--------------------------------------------------------------------------------------------------
// Simple class to handle the Vulkan context creation
class VkContext
{
public:
  VkContext() = default;
  VkContext(const VkContextSettings& settings) { init(settings); }
  ~VkContext() { deinit(); }

  VkInstance             getInstance() const { return m_instance; }
  VkDevice               getDevice() const { return m_device; }
  VkPhysicalDevice       getPhysicalDevice() const { return m_physicalDevice; }
  QueueInfo              getQueueInfo(uint32_t index) const { return m_queueInfos[index]; }
  std::vector<QueueInfo> getQueueInfos() const { return m_queueInfos; }
  bool                   isValid() const
  {
    return m_instance != VK_NULL_HANDLE && m_device != VK_NULL_HANDLE && m_physicalDevice != VK_NULL_HANDLE;
  }

  void init(const VkContextSettings& settings = VkContextSettings())
  {
    m_settings = settings;
    {
      nvh::ScopedTimer st("Creating Vulkan Context");
      createInstance();
      selectPhysicalDevice();
      createDevice();
    }
    if(m_settings.verbose)
    {
      printVulkanVersion();
      printInstanceLayers();
      printInstanceExtensions(m_settings.instanceExtensions);
      printDeviceExtensions(m_physicalDevice, m_settings.deviceExtensions);
      printGpus(m_instance, m_physicalDevice);
      LOGI("_________________________________________________\n");
    }
  }

  void deinit()
  {
    if(m_dbgMessenger)
    {
      auto vkDestroyDebugUtilsMessengerEXT =
          (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT");
      assert(vkDestroyDebugUtilsMessengerEXT != nullptr);
      vkDestroyDebugUtilsMessengerEXT(m_instance, m_dbgMessenger, m_settings.alloc);
      m_dbgMessenger = nullptr;
    }
    vkDestroyDevice(m_device, m_settings.alloc);
    vkDestroyInstance(m_instance, m_settings.alloc);
    m_device   = VK_NULL_HANDLE;
    m_instance = VK_NULL_HANDLE;
  }


private:
  VkContextSettings m_settings{};  // What was used to create the information

  VkInstance       m_instance{};
  VkDevice         m_device{};
  VkPhysicalDevice m_physicalDevice{};

  // For device creation
  VkPhysicalDeviceFeatures2        m_deviceFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
  VkPhysicalDeviceVulkan11Features features11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
  VkPhysicalDeviceVulkan12Features features12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
  VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};

  // For Queue creation
  std::vector<VkQueueFlags>            m_desiredQueues{};
  std::vector<VkDeviceQueueCreateInfo> m_queueCreateInfos{};
  std::vector<QueueInfo>               m_queueInfos{};
  std::vector<std::vector<float>>      m_queuePriorities{};  // Store priorities here

  // Callback for debug messages
  VkDebugUtilsMessengerEXT m_dbgMessenger = VK_NULL_HANDLE;

  void createInstance()
  {
    nvh::ScopedTimer st(__FUNCTION__);

    VkApplicationInfo appInfo{
        .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName   = m_settings.applicationName,
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName        = "My Engine",
        .engineVersion      = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion         = m_settings.apiVersion,
    };

    std::vector<const char*> layers;
    if(m_settings.enableValidationLayers)
    {
      layers.push_back("VK_LAYER_KHRONOS_validation");
    }

    VkInstanceCreateInfo createInfo{
        .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext                   = m_settings.instanceCreateInfoExt,
        .pApplicationInfo        = &appInfo,
        .enabledLayerCount       = uint32_t(layers.size()),
        .ppEnabledLayerNames     = layers.data(),
        .enabledExtensionCount   = uint32_t(m_settings.instanceExtensions.size()),
        .ppEnabledExtensionNames = m_settings.instanceExtensions.data(),
    };

    VkResult result = vkCreateInstance(&createInfo, m_settings.alloc, &m_instance);
    if(result != VK_SUCCESS)
    {
      assert(!"failed to create instance!");
      return;
    }

    if(m_settings.enableValidationLayers)
    {
      auto vkCreateDebugUtilsMessengerEXT =
          (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT");
      assert(vkCreateDebugUtilsMessengerEXT != nullptr);
      VkDebugUtilsMessengerCreateInfoEXT dbg_messenger_create_info{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
      dbg_messenger_create_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT       // For debug printf
                                                  | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT  // GPU info, bug
                                                  | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;   // Invalid usage
      dbg_messenger_create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT      // Violation of spec
                                              | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;  // Non-optimal use
      dbg_messenger_create_info.pfnUserCallback = VkContextDebugReport;
      dbg_messenger_create_info.pUserData       = &m_settings.ignoreDbgMessages;
      NVVK_CHECK(vkCreateDebugUtilsMessengerEXT(m_instance, &dbg_messenger_create_info, nullptr, &m_dbgMessenger));
    }
  }

  void selectPhysicalDevice()
  {
    if(m_instance == VK_NULL_HANDLE)
      return;

    // nvh::ScopedTimer st(std::string(__FUNCTION__) + "\n");
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
    if(deviceCount == 0)
      assert(!"Failed to find GPUs with Vulkan support!");
    std::vector<VkPhysicalDevice> gpus(deviceCount);
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, gpus.data());

    // Find the discrete GPU if present, or use first one available.
    if((m_settings.forceGPU == -1) || (m_settings.forceGPU >= int(deviceCount)))
    {
      m_physicalDevice = gpus[0];
      for(VkPhysicalDevice& device : gpus)
      {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(device, &properties);
        if(properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        {
          m_physicalDevice = device;
        }
      }
    }
    else
    {
      // Using specified GPU
      m_physicalDevice = gpus[m_settings.forceGPU];
    }

    {  // Check for available Vulkan version
      VkPhysicalDeviceProperties properties;
      vkGetPhysicalDeviceProperties(m_physicalDevice, &properties);
      uint32_t apiVersion = properties.apiVersion;
      if((VK_VERSION_MAJOR(apiVersion) < VK_VERSION_MAJOR(m_settings.apiVersion))
         || (VK_VERSION_MINOR(apiVersion) < VK_VERSION_MINOR(m_settings.apiVersion)))
      {
        LOGE("Requested Vulkan version (%d.%d) is higher than available version (%d.%d).\n", VK_VERSION_MAJOR(m_settings.apiVersion),
             VK_VERSION_MINOR(m_settings.apiVersion), VK_VERSION_MAJOR(apiVersion), VK_VERSION_MINOR(apiVersion));
        m_physicalDevice = {};
        return;
      }
    }

    // Query the physical device features
    m_deviceFeatures.pNext = &features11;
    if(m_settings.apiVersion >= VK_API_VERSION_1_2)
      features11.pNext = &features12;
    if(m_settings.apiVersion >= VK_API_VERSION_1_3)
      features12.pNext = &features13;
    vkGetPhysicalDeviceFeatures2(m_physicalDevice, &m_deviceFeatures);

    // Find the queues that we need
    m_desiredQueues = m_settings.queues;
    if(!findQueueFamilies())
    {
      m_physicalDevice = {};
      return;
    }

    // Filter the available extensions otherwise the device creation will fail
    std::vector<ExtensionFeaturePair> filteredExtensions;
    bool allFound = filterAvailableExtensions(getDeviceExtensions(m_physicalDevice), m_settings.deviceExtensions, filteredExtensions);
    if(!allFound)
    {
      m_physicalDevice = {};
      return;
    }
    m_settings.deviceExtensions = filteredExtensions;
  }

  void createDevice()
  {
    if(m_physicalDevice == VK_NULL_HANDLE)
      return;

    // nvh::ScopedTimer st(__FUNCTION__);
    // Chain all custom features to the pNext chain of m_deviceFeatures
    for(const auto& extension : m_settings.deviceExtensions)
    {
      if(extension.feature)
        prependFeatures(reinterpret_cast<VkBaseOutStructure*>(&m_deviceFeatures),
                        reinterpret_cast<VkBaseOutStructure*>(extension.feature));
    }
    // Activate features on request
    if(m_settings.enableAllFeatures)
      vkGetPhysicalDeviceFeatures2(m_physicalDevice, &m_deviceFeatures);

    // List of extensions to enable
    std::vector<const char*> enabledExtensions;
    for(const auto& ext : m_settings.deviceExtensions)
      enabledExtensions.push_back(ext.extensionName);
    VkDeviceCreateInfo createInfo{
        .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext                   = &m_deviceFeatures,
        .queueCreateInfoCount    = uint32_t(m_queueCreateInfos.size()),
        .pQueueCreateInfos       = m_queueCreateInfos.data(),
        .enabledExtensionCount   = static_cast<uint32_t>(enabledExtensions.size()),
        .ppEnabledExtensionNames = enabledExtensions.data(),
    };

    if(vkCreateDevice(m_physicalDevice, &createInfo, m_settings.alloc, &m_device) != VK_SUCCESS)
      assert(!"failed to create logical device!");

    for(auto& queue : m_queueInfos)
      vkGetDeviceQueue(m_device, queue.familyIndex, queue.queueIndex, &queue.queue);
  }

  void prependFeatures(VkBaseOutStructure* baseStruct, VkBaseOutStructure* prependStruct)
  {
    // Traverse the pNext chain of prependStruct to find the last element
    VkBaseOutStructure* lastPrepend = prependStruct;
    while(lastPrepend->pNext)
    {
      lastPrepend = reinterpret_cast<VkBaseOutStructure*>(lastPrepend->pNext);
    }
    lastPrepend->pNext = baseStruct->pNext;  // Append the original pNext chain of baseStruct to the end of prependStruct's chain
    baseStruct->pNext = prependStruct;  // Prepend the prependStruct to the baseStruct
  }

  bool findQueueFamilies()
  {
    uint32_t queueFamilyCount;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, queueFamilies.data());

    std::unordered_map<uint32_t, uint32_t> queueFamilyUsage;
    for(uint32_t i = 0; i < queueFamilyCount; ++i)
    {
      queueFamilyUsage[i] = 0;
    }

    for(size_t i = 0; i < m_desiredQueues.size(); ++i)
    {
      bool found = false;
      for(uint32_t j = 0; j < queueFamilyCount; ++j)
      {
        // Check for an exact match and unused queue family
        // Avoid queue family with VK_QUEUE_GRAPHICS_BIT if not needed
        if((queueFamilies[j].queueFlags & m_desiredQueues[i]) == m_desiredQueues[i] && queueFamilyUsage[j] == 0
           && ((m_desiredQueues[i] & VK_QUEUE_GRAPHICS_BIT) || !(queueFamilies[j].queueFlags & VK_QUEUE_GRAPHICS_BIT)))
        {
          m_queueInfos.push_back({j, queueFamilyUsage[j]});
          queueFamilyUsage[j]++;
          found = true;
          break;
        }
      }

      if(!found)
      {
        for(uint32_t j = 0; j < queueFamilyCount; ++j)
        {
          // Check for an exact match and allow reuse if queue count not exceeded
          // Avoid queue family with VK_QUEUE_GRAPHICS_BIT if not needed
          if((queueFamilies[j].queueFlags & m_desiredQueues[i]) == m_desiredQueues[i]
             && queueFamilyUsage[j] < queueFamilies[j].queueCount
             && ((m_desiredQueues[i] & VK_QUEUE_GRAPHICS_BIT) || !(queueFamilies[j].queueFlags & VK_QUEUE_GRAPHICS_BIT)))
          {
            m_queueInfos.push_back({j, queueFamilyUsage[j]});
            queueFamilyUsage[j]++;
            found = true;
            break;
          }
        }
      }

      if(!found)
      {
        for(uint32_t j = 0; j < queueFamilyCount; ++j)
        {
          // Check for a partial match and allow reuse if queue count not exceeded
          // Avoid queue family with VK_QUEUE_GRAPHICS_BIT if not needed
          if((queueFamilies[j].queueFlags & m_desiredQueues[i]) && queueFamilyUsage[j] < queueFamilies[j].queueCount
             && ((m_desiredQueues[i] & VK_QUEUE_GRAPHICS_BIT) || !(queueFamilies[j].queueFlags & VK_QUEUE_GRAPHICS_BIT)))
          {
            m_queueInfos.push_back({j, queueFamilyUsage[j]});
            queueFamilyUsage[j]++;
            found = true;
            break;
          }
        }
      }

      if(!found)
      {
        for(uint32_t j = 0; j < queueFamilyCount; ++j)
        {
          // Check for a partial match and allow reuse if queue count not exceeded
          if((queueFamilies[j].queueFlags & m_desiredQueues[i]) && queueFamilyUsage[j] < queueFamilies[j].queueCount)
          {
            m_queueInfos.push_back({j, queueFamilyUsage[j]});
            queueFamilyUsage[j]++;
            found = true;
            break;
          }
        }
      }

      if(!found)
      {
        // If no suitable queue family is found, assert a failure
        LOGE("Failed to find a suitable queue family!\n");
        return false;
      }
    }

    for(const auto& usage : queueFamilyUsage)
    {
      if(usage.second > 0)
      {
        m_queuePriorities.emplace_back(usage.second, 1.0f);  // Same priority for all queues in a family
        m_queueCreateInfos.push_back({VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, nullptr, 0, usage.first, usage.second,
                                      m_queuePriorities.back().data()});
      }
    }
    return true;
  }

  bool filterAvailableExtensions(const std::vector<VkExtensionProperties>& availableExtensions,
                                 const std::vector<ExtensionFeaturePair>&  desiredExtensions,
                                 std::vector<ExtensionFeaturePair>&        filteredExtensions)
  {
    bool allFound = true;

    std::unordered_map<std::string, bool> availableExtensionsMap;
    for(const auto& ext : availableExtensions)
      availableExtensionsMap[ext.extensionName] = true;

    for(const auto& desiredExtension : desiredExtensions)
    {
      if(availableExtensionsMap.find(desiredExtension.extensionName) != availableExtensionsMap.end())
      {
        filteredExtensions.push_back(desiredExtension);
      }
      else
      {
        if(desiredExtension.required)
        {
          LOGE("Extension not available: %s\n", desiredExtension.extensionName);
          allFound = false;
        }
        else
          LOGW("Extension not available: %s\n", desiredExtension.extensionName);
      }
    }

    return allFound;
  }
};

inline void printVulkanVersion()
{
  uint32_t version;
  vkEnumerateInstanceVersion(&version);
  LOGI("\n_________________________________________________\n");
  LOGI("Vulkan Version:  %d.%d.%d\n", VK_VERSION_MAJOR(version), VK_VERSION_MINOR(version), VK_VERSION_PATCH(version));
}

inline std::vector<VkExtensionProperties> getDeviceExtensions(VkPhysicalDevice physicalDevice)
{
  uint32_t                           count;
  std::vector<VkExtensionProperties> extensionProperties;
  NVVK_CHECK(vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, nullptr));
  extensionProperties.resize(count);
  NVVK_CHECK(vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, extensionProperties.data()));
  extensionProperties.resize(std::min(extensionProperties.size(), size_t(count)));
  return extensionProperties;
}

inline std::string getVersionString(uint32_t version)
{
  return fmt::format("{}.{}.{}", VK_VERSION_MAJOR(version), VK_VERSION_MINOR(version), VK_VERSION_PATCH(version));
}

inline void printInstanceLayers()
{
  uint32_t                       count;
  std::vector<VkLayerProperties> layerProperties;
  NVVK_CHECK(vkEnumerateInstanceLayerProperties(&count, nullptr));
  layerProperties.resize(count);
  NVVK_CHECK(vkEnumerateInstanceLayerProperties(&count, layerProperties.data()));
  LOGI("_________________________________________________\n");
  LOGI("Available Instance Layers :\n");
  for(auto& it : layerProperties)
  {
    LOGI("%s (v. %d.%d.%d %x) : %s\n", it.layerName, VK_VERSION_MAJOR(it.specVersion), VK_VERSION_MINOR(it.specVersion),
         VK_VERSION_PATCH(it.specVersion), it.implementationVersion, it.description);
  }
}

inline void printInstanceExtensions(const std::vector<const char*> ext)
{
  std::unordered_set<std::string> exist;
  for(auto& e : ext)
    exist.insert(e);

  uint32_t                           count;
  std::vector<VkExtensionProperties> extensionProperties;
  NVVK_CHECK(vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr));
  extensionProperties.resize(count);
  NVVK_CHECK(vkEnumerateInstanceExtensionProperties(nullptr, &count, extensionProperties.data()));
  LOGI("_________________________________________________\n");
  LOGI("Available Instance Extensions :\n");
  for(const VkExtensionProperties& it : extensionProperties)
  {
    LOGI("[%s] ", (exist.find(it.extensionName) != exist.end()) ? "x" : " ");
    LOGI("%s (v. %d)\n", it.extensionName, it.specVersion);
  }
}

inline void printDeviceExtensions(VkPhysicalDevice physicalDevice, const std::vector<ExtensionFeaturePair> ext)
{
  if(physicalDevice == VK_NULL_HANDLE)
    return;

  std::unordered_set<std::string> exist;
  for(auto& e : ext)
    exist.insert(e.extensionName);

  uint32_t                           count;
  std::vector<VkExtensionProperties> extensionProperties;
  NVVK_CHECK(vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, nullptr));
  extensionProperties.resize(count);
  NVVK_CHECK(vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, extensionProperties.data()));
  LOGI("_________________________________________________\n");
  LOGI("Available Device Extensions :\n");
  for(const VkExtensionProperties& it : extensionProperties)
  {
    LOGI("[%s] ", (exist.find(it.extensionName) != exist.end()) ? "x" : " ");
    LOGI("%s (v. %d)\n", it.extensionName, it.specVersion);
  }
}

inline std::string getVendorName(uint32_t vendorID)
{
  static const std::unordered_map<uint32_t, std::string> vendorMap = {{0x1002, "AMD"},      {0x1010, "ImgTec"},
                                                                      {0x10DE, "NVIDIA"},   {0x13B5, "ARM"},
                                                                      {0x5143, "Qualcomm"}, {0x8086, "INTEL"}};

  auto it = vendorMap.find(vendorID);
  return it != vendorMap.end() ? it->second : "Unknown Vendor";
}

inline std::string getDeviceType(uint32_t deviceType)
{
  static const std::unordered_map<uint32_t, std::string> deviceTypeMap = {{VK_PHYSICAL_DEVICE_TYPE_OTHER, "Other"},
                                                                          {VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU, "Integrated GPU"},
                                                                          {VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU, "Discrete GPU"},
                                                                          {VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU, "Virtual GPU"},
                                                                          {VK_PHYSICAL_DEVICE_TYPE_CPU, "CPU"}};

  auto it = deviceTypeMap.find(deviceType);
  return it != deviceTypeMap.end() ? it->second : "Unknown";
}

inline void printGpus(VkInstance instance, VkPhysicalDevice usedGpu)
{
  uint32_t deviceCount = 0;
  vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
  std::vector<VkPhysicalDevice> gpus(deviceCount);
  vkEnumeratePhysicalDevices(instance, &deviceCount, gpus.data());
  LOGI("_________________________________________________\n");
  LOGI("Available GPUS: %d\n", deviceCount);

  VkPhysicalDeviceProperties properties;
  for(uint32_t d = 0; d < deviceCount; d++)
  {
    vkGetPhysicalDeviceProperties(gpus[d], &properties);
    LOGI(" - %d) %s\n", d, properties.deviceName);
  }
  if(usedGpu == VK_NULL_HANDLE)
  {
    LOGE("No compatible GPU\n");
    return;
  }

  LOGI("Using GPU:\n");
  vkGetPhysicalDeviceProperties(usedGpu, &properties);
  printPhysicalDeviceProperties(properties);
}

inline void printPhysicalDeviceProperties(const VkPhysicalDeviceProperties& properties)
{
  LOGI(" - Device Name    : %s\n", properties.deviceName);
  LOGI(" - Vendor         : %s\n", getVendorName(properties.vendorID).c_str());
  LOGI(" - Driver Version : %s\n", getVersionString(properties.driverVersion).c_str());
  LOGI(" - API Version    : %s\n", getVersionString(properties.apiVersion).c_str());
  LOGI(" - Device Type    : %s\n", getDeviceType(properties.deviceType).c_str());
}
