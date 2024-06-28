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

This is the ImGui UI for the glTF model.
It is used to render the scene graph and the details of the selected node, such as the transform and the materials.

*/


#include <string>
#include <vector>
#include <bitset>
#include <unordered_set>

#include <imgui.h>
#include <tiny_gltf.h>
#include <glm/glm.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "nvh/boundingbox.hpp"

class GltfModelUI
{
public:
  explicit GltfModelUI(tinygltf::Model& model, nvh::Bbox bbox)
      : m_model(model)
      , m_bbox(bbox)
  {
  }

  void render();
  bool hasTransfromChanged() { return m_changes.test(eNodeTransformDirty); }
  bool hasMaterialChanged() { return m_changes.test(eMaterialDirty); }
  void resetChanges() { m_changes.reset(); }
  void selectNode(int nodeIndex);
  int  selectedNode() const { return selectedNodeIndex; }
  int  selectedMaterial() const { return selectedMaterialIndex; }

private:
  void preprocessOpenNodes();
  bool markOpenNodes(int nodeIndex, int targetNodeIndex, std::unordered_set<int>& openNodes);

  std::unordered_set<int> openNodes;  // To keep track of nodes that should be open

  tinygltf::Model& m_model;
  int              selectedNodeIndex     = -1;
  int              selectedMaterialIndex = -1;
  std::bitset<32>  m_changes;
  nvh::Bbox        m_bbox;

  enum DirtyFlags
  {
    eNodeTransformDirty,
    eMaterialDirty,
    // Add more flags as needed
  };

  void renderNode(int nodeIndex);
  void renderMesh(int meshIndex);
  void renderMaterial(int materialIndex);
  void renderNodeDetails(int nodeIndex);

  void getNodeTransform(const tinygltf::Node& node, glm::vec3& translation, glm::quat& rotation, glm::vec3& scale);
  bool m_doScroll = false;
};
