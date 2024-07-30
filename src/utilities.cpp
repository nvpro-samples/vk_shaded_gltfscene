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


#include "utilities.hpp"
#include "vulkan/vulkan_core.h"
#include <filesystem>
#include "stb_image_write.h"
#include "nvh/nvprint.hpp"
#include "nvvk/images_vk.hpp"
#include "nvvk/error_vk.hpp"


// Convert a tiled image to RGBA8 linear
void gltfr::imageToRgba8Linear(VkCommandBuffer  cmd,
                               VkDevice         device,
                               VkPhysicalDevice physicalDevice,
                               VkImage          srcImage,
                               VkExtent2D       size,
                               VkImage&         dstImage,
                               VkDeviceMemory&  dstImageMemory)
{
  // Find the memory type index for the memory
  auto getMemoryType = [&](uint32_t typeBits, const VkMemoryPropertyFlags& properties) {
    VkPhysicalDeviceMemoryProperties prop;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &prop);
    for(uint32_t i = 0; i < prop.memoryTypeCount; i++)
    {
      if(((typeBits & (1 << i)) > 0) && (prop.memoryTypes[i].propertyFlags & properties) == properties)
        return i;
    }
    return ~0u;  // Unable to find memoryType
  };


  // Create the linear tiled destination image to copy to and to read the memory from
  VkImageCreateInfo imageCreateCI = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  imageCreateCI.imageType         = VK_IMAGE_TYPE_2D;
  imageCreateCI.format            = VK_FORMAT_R8G8B8A8_UNORM;
  imageCreateCI.extent.width      = size.width;
  imageCreateCI.extent.height     = size.height;
  imageCreateCI.extent.depth      = 1;
  imageCreateCI.arrayLayers       = 1;
  imageCreateCI.mipLevels         = 1;
  imageCreateCI.initialLayout     = VK_IMAGE_LAYOUT_UNDEFINED;
  imageCreateCI.samples           = VK_SAMPLE_COUNT_1_BIT;
  imageCreateCI.tiling            = VK_IMAGE_TILING_LINEAR;
  imageCreateCI.usage             = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  NVVK_CHECK(vkCreateImage(device, &imageCreateCI, nullptr, &dstImage));

  // Create memory for the image
  // We want host visible and coherent memory to be able to map it and write to it directly
  VkMemoryRequirements memRequirements;
  vkGetImageMemoryRequirements(device, dstImage, &memRequirements);
  VkMemoryAllocateInfo memAllocInfo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  memAllocInfo.allocationSize       = memRequirements.size;
  memAllocInfo.memoryTypeIndex =
      getMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  NVVK_CHECK(vkAllocateMemory(device, &memAllocInfo, nullptr, &dstImageMemory));
  NVVK_CHECK(vkBindImageMemory(device, dstImage, dstImageMemory, 0));

  nvvk::cmdBarrierImageLayout(cmd, srcImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  nvvk::cmdBarrierImageLayout(cmd, dstImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

  // Do the actual blit from the swapchain image to our host visible destination image
  // The Blit allow to convert the image from VK_FORMAT_B8G8R8A8_UNORM to VK_FORMAT_R8G8B8A8_UNORM automatically
  VkOffset3D  blitSize = {int32_t(size.width), int32_t(size.height), 1};
  VkImageBlit imageBlitRegion{};
  imageBlitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  imageBlitRegion.srcSubresource.layerCount = 1;
  imageBlitRegion.srcOffsets[1]             = blitSize;
  imageBlitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  imageBlitRegion.dstSubresource.layerCount = 1;
  imageBlitRegion.dstOffsets[1]             = blitSize;
  vkCmdBlitImage(cmd, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                 &imageBlitRegion, VK_FILTER_NEAREST);

  nvvk::cmdBarrierImageLayout(cmd, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
  nvvk::cmdBarrierImageLayout(cmd, dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
}


void gltfr::saveImageToFile(VkDevice device, VkImage dstImage, VkDeviceMemory dstImageMemory, VkExtent2D size, const std::string& filename, int quality)
{
  // Get layout of the image (including offset and row pitch)
  VkImageSubresource  subResource{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0};
  VkSubresourceLayout subResourceLayout;
  vkGetImageSubresourceLayout(device, dstImage, &subResource, &subResourceLayout);

  // Map image memory so we can start copying from it
  const char* data = nullptr;
  vkMapMemory(device, dstImageMemory, 0, VK_WHOLE_SIZE, 0, (void**)&data);
  data += subResourceLayout.offset;

  // Copy the data and adjust for the row pitch
  std::vector<uint8_t> pixels(size.width * size.height * 4);
  for(uint32_t y = 0; y < size.height; y++)
  {
    memcpy(pixels.data() + y * size.width * 4, data, static_cast<size_t>(size.width) * 4);
    data += subResourceLayout.rowPitch;
  }

  std::filesystem::path path      = filename;
  std::string           extension = path.extension().string();

  // Check the extension and perform actions accordingly
  if(extension == ".png")
  {
    stbi_write_png(filename.c_str(), size.width, size.height, 4, pixels.data(), size.width * 4);
  }
  else if(extension == ".jpg" || extension == ".jpeg")
  {
    stbi_write_jpg(filename.c_str(), size.width, size.height, 4, pixels.data(), quality);
  }
  else if(extension == ".bmp")
  {
    stbi_write_bmp(filename.c_str(), size.width, size.height, 4, pixels.data());
  }
  else
  {
    LOGW("Screenshot: unknown file extension, saving as PNG\n");
    stbi_write_png(filename.c_str(), size.width, size.height, 4, data, size.width * 4);
  }

  LOGI("Screenshot saved to %s\n", filename.c_str());
}
