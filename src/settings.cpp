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

#include "imgui.h"
#include "imgui/imgui_helper.h"

#include "settings.hpp"

//-----------------------------------------------------------------------------
// Rendering the UI for the settings
//
void gltfr::Settings::onUI()
{
  namespace PE = ImGuiH::PropertyEditor;
  if(ImGui::CollapsingHeader("Settings"))
  {
    PE::begin("gltfr::Settings::onUI");
    PE::SliderInt("Max Frames", &maxFrames, 1, 1000000);
    PE::Checkbox("Show Axis", &showAxis);
    PE::SliderFloat("Max Luminance", &maxLuminance, 0.0f, 10000.0f);
    PE::end();
  }
}
