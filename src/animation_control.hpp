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

/*

    This structure is used to hold the animation controls.
    It shows the play, pause, step forward, and step backward buttons.
    It also shows the speed slider.

    This is used in the Scene::onUI() function to show the animation controls.

*/


#include "imgui.h"
#include "imgui/imgui_helper.h"
#include "imgui/imgui_icon.h"


// Simple structure to hold the animation controls
struct AnimationControl
{
  void onUI()
  {
    ImGui::SeparatorText("Animation Controls");
    ImGui::PushFont(ImGuiH::getIconicFont());
    if(ImGui::Button(play ? ImGuiH::icon_media_pause : ImGuiH::icon_media_play))
      play = !play;
    ImGui::SameLine();
    if(ImGui::Button(ImGuiH::icon_media_step_forward))
    {
      runOnce = true;
      play    = false;
    }
    ImGui::SameLine();
    if(ImGui::Button(ImGuiH::icon_media_skip_backward))
    {
      reset = true;
    }

    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::SliderFloat("##speed", &speed, 0.0F, 2.0F, "Speed: %.2f");
    ImGui::Separator();
  }

  bool  doAnimation() const { return play || runOnce || reset; }
  float deltaTime() const { return runOnce ? speed / 60.f : ImGui::GetIO().DeltaTime * speed; }
  bool  isReset() const { return reset; }
  void  clearStates() { runOnce = reset = false; }

  bool  play    = true;   // Simulation running?
  bool  runOnce = false;  // Simulation step-once
  bool  reset   = false;  // Reset the animation
  float speed   = 1.0F;   // Simulation speed
};
