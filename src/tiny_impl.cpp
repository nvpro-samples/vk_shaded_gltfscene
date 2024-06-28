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

/*
 
 This file is a compilation unit for the tinygltf library.
 It is used to compile the library into the application.
       
*/


#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define TINYGLTF_NO_EXTERNAL_IMAGE  // Images loaded by SceneVk
#define TINYGLTF_USE_RAPIDJSON
#define TINYGLTF_USE_RAPIDJSON_CRTALLOCATOR
#pragma warning(push)
#pragma warning(disable : 4018)  // signed/unsigned mismatch
#pragma warning(disable : 4267)  // conversion from 'size_t' to 'uint32_t', possible loss of data
#pragma warning(disable : 4996)  //
#include "tiny_gltf.h"
#pragma warning(pop)
#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"
