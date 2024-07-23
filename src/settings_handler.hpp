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


// This is a helper class to manage settings in ImGui. It allows to easily add settings to the ImGui settings handler

#pragma once


#include <unordered_map>
#include <string>
#include <sstream>
#include <imgui.h>
#include <glm/glm.hpp>
#include <functional>

class SettingsHandler
{
private:
  struct SettingEntry
  {
    void*                                   ptr{};
    std::function<void(const std::string&)> fromString;
    std::function<std::string()>            toString;
  };

  std::string                                   handlerName;
  std::unordered_map<std::string, SettingEntry> settings;

  // Helper functions for conversion
  template <typename T>
  static std::string defaultToString(const T& value)
  {
    std::ostringstream oss;
    oss << value;
    return oss.str();
  }

  template <typename T>
  static void defaultFromString(const std::string& str, T& value)
  {
    std::istringstream iss(str);
    iss >> value;
  }

  // Specializations for specific types
  static std::string vec2ToString(const glm::vec2& value)
  {
    return std::to_string(value.x) + "," + std::to_string(value.y);
  }

  static void vec2FromString(const std::string& str, glm::vec2& value)
  {
    int ret = sscanf(str.c_str(), "%f,%f", &value.x, &value.y);
    assert(ret == 2 && "Failed to parse vec2");
  }

  static std::string vec3ToString(const glm::vec3& value)
  {
    return std::to_string(value.x) + "," + std::to_string(value.y) + "," + std::to_string(value.z);
  }

  static void vec3FromString(const std::string& str, glm::vec3& value)
  {
    int ret = sscanf(str.c_str(), "%f,%f,%f", &value.x, &value.y, &value.z);
    assert(ret == 3 && "Failed to parse vec3");
  }

  static std::string boolToString(const bool& value) { return value ? "true" : "false"; }

  static void boolFromString(const std::string& str, bool& value) { value = (str == "true"); }

public:
  explicit SettingsHandler(const std::string& name)
      : handlerName(name)
  {
  }

  template <typename T>
  void setSetting(const std::string& key, T* value)
  {
    SettingEntry entry;
    entry.ptr        = value;
    entry.fromString = [value](const std::string& str) { defaultFromString(str, *value); };
    entry.toString   = [value]() { return defaultToString(*value); };
    settings[key]    = entry;
  }

  // Specializations for specific types
  void setSetting(const std::string& key, glm::vec2* value)
  {
    SettingEntry entry;
    entry.ptr        = value;
    entry.fromString = [value](const std::string& str) { vec2FromString(str, *value); };
    entry.toString   = [value]() { return vec2ToString(*value); };
    settings[key]    = entry;
  }

  void setSetting(const std::string& key, glm::vec3* value)
  {
    SettingEntry entry;
    entry.ptr        = value;
    entry.fromString = [value](const std::string& str) { vec3FromString(str, *value); };
    entry.toString   = [value]() { return vec3ToString(*value); };
    settings[key]    = entry;
  }

  void setSetting(const std::string& key, bool* value)
  {
    SettingEntry entry;
    entry.ptr        = value;
    entry.fromString = [value](const std::string& str) { boolFromString(str, *value); };
    entry.toString   = [value]() { return boolToString(*value); };
    settings[key]    = entry;
  }

  void addImGuiHandler()
  {
    ImGuiSettingsHandler ini_handler{};
    ini_handler.TypeName   = handlerName.c_str();
    ini_handler.TypeHash   = ImHashStr(handlerName.c_str());
    ini_handler.ReadOpenFn = [](ImGuiContext*, ImGuiSettingsHandler*, const char*) -> void* { return (void*)1; };
    ini_handler.ReadLineFn = [](ImGuiContext*, ImGuiSettingsHandler* handler, void*, const char* line) {
      SettingsHandler* s = static_cast<SettingsHandler*>(handler->UserData);
      char             key[64], value[256];
      key[63]    = 0;  // zero terminate, protection
      value[255] = 0;  // zero terminate, protection
      if(sscanf(line, "%63[^=]=%255[^\n]", key, value) == 2)
      {
        auto it = s->settings.find(key);
        if(it != s->settings.end())
        {
          it->second.fromString(value);
        }
      }
    };
    ini_handler.WriteAllFn = [](ImGuiContext* ctx, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buf) {
      SettingsHandler* s = static_cast<SettingsHandler*>(handler->UserData);
      buf->appendf("[%s][State]\n", handler->TypeName);
      for(const auto& [key, entry] : s->settings)
      {
        buf->appendf("%s=%s\n", key.c_str(), entry.toString().c_str());
      }
      buf->appendf("\n");
    };
    ini_handler.UserData = this;
    ImGui::AddSettingsHandler(&ini_handler);
  }
};
