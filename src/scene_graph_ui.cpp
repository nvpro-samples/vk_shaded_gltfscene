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

#include <fmt/format.h>

#include "scene_graph_ui.hpp"
#include "imgui/imgui_helper.h"

namespace PE = ImGuiH::PropertyEditor;

//--------------------------------------------------------------------------------------------------
// Entry point for rendering the scene graph
// Loop over all scenes
// - Loop over all nodes in the scene
//Following, in the second part, is the details:
// - Display the node details (transform)
//   OR Display the material details
//
void GltfModelUI::render()
{
  if(ImGui::BeginChild("SceneGraph", ImVec2(0, ImGui::GetContentRegionAvail().y * 0.5f), true))
  {
    for(size_t sceneID = 0; sceneID < m_model.scenes.size(); sceneID++)
    {
      auto& scene = m_model.scenes[sceneID];
      ImGui::SetNextItemOpen(true);  // Scene is always open
      if(ImGui::TreeNode("Scene", "%s (Scene %ld)", scene.name.c_str(), sceneID))
      {
        for(int node : scene.nodes)
        {
          renderNode(node);
        }
        ImGui::TreePop();
      }
    }
  }
  ImGui::EndChild();

  ImGui::Separator();

  if(ImGui::BeginChild("Details", ImVec2(0, 0), true))
  {
    if(selectedNodeIndex > -1)
    {
      renderNodeDetails(selectedNodeIndex);
    }
    if(selectedMaterialIndex > -1)
    {
      renderMaterial(selectedMaterialIndex);
    }
  }
  ImGui::EndChild();
}

//--------------------------------------------------------------------------------------------------
// This function is called when a node is selected
// It will open all the parents of the selected node
//
void GltfModelUI::selectNode(int nodeIndex)
{
  selectedNodeIndex     = nodeIndex;
  selectedMaterialIndex = -1;
  openNodes.clear();
  if(nodeIndex >= 0)
    preprocessOpenNodes();
  m_doScroll = true;
}

//--------------------------------------------------------------------------------------------------
// This is rendering the node and its children
// If it has the command to open the node, it will open it,
// when it finds the selected node, it will highlight it and will scroll to it. (done once)
//
void GltfModelUI::renderNode(int nodeIndex)
{
  const auto& node = m_model.nodes[nodeIndex];

  ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;

  // This is to make sure the selected node will be visible
  if(openNodes.find(nodeIndex) != openNodes.end())
  {
    ImGui::SetNextItemOpen(true);
  }

  // If the node is selected, we want to highlight it
  if(selectedNodeIndex == nodeIndex)
  {
    flags |= ImGuiTreeNodeFlags_Selected;
    // Scroll to the selected node, done once and no need to open all the parents any more
    openNodes.clear();
    if(m_doScroll)
    {
      ImGui::SetScrollHereY();
      m_doScroll = false;
    }
  }

  // Handling the selection of the node
  bool nodeOpen = ImGui::TreeNodeEx((void*)(intptr_t)nodeIndex, flags, "%s (Node %d)", node.name.c_str(), nodeIndex);
  if(ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
  {
    selectedNodeIndex     = (selectedNodeIndex == nodeIndex) ? -1 /*toggle off*/ : nodeIndex;
    selectedMaterialIndex = -1;
  }

  // If the node is open, we want to render the mesh and the children
  if(nodeOpen)
  {
    if(node.mesh >= 0)
    {
      auto& mesh = m_model.meshes[node.mesh];
      if(ImGui::TreeNode("Mesh", "%s (Mesh %d)", mesh.name.c_str(), node.mesh))
      {
        renderMesh(node.mesh);
        ImGui::TreePop();
      }
    }

    for(int child : node.children)
    {
      renderNode(child);
    }
    ImGui::TreePop();
  }
}

//--------------------------------------------------------------------------------------------------
// Mesh rendering, shows the primitives and the material
//
void GltfModelUI::renderMesh(int meshIndex)
{
  const auto& mesh = m_model.meshes[meshIndex];

  for(size_t i = 0; i < mesh.primitives.size(); ++i)
  {
    if(ImGui::TreeNode("Primitive", "Primitive %s", std::to_string(i).c_str()))
    {
      int         materialID   = mesh.primitives[i].material;
      std::string materialName = m_model.materials[materialID].name;
      if(ImGui::Selectable(materialName.c_str(), selectedMaterialIndex == materialID))
      {
        selectedMaterialIndex = materialID;
        selectedNodeIndex     = -1;
      }
      ImGui::TreePop();
    }
  }
}

//--------------------------------------------------------------------------------------------------
// Node details is the transform of the node
// It will show the translation, rotation and scale
//
void GltfModelUI::renderNodeDetails(int nodeIndex)
{
  auto&     node = m_model.nodes[nodeIndex];
  glm::vec3 translation, scale;
  glm::quat rotation;

  getNodeTransform(node, translation, rotation, scale);

  ImGui::Text("Node: %s", node.name.c_str());

  glm::vec3 euler = glm::degrees(glm::eulerAngles(rotation));

  PE::begin();
  {
    bool modif = false;
    modif |= PE::DragFloat3("Translation", glm::value_ptr(translation), 0.01f * m_bbox.radius());
    modif |= PE::DragFloat3("Rotation", glm::value_ptr(euler), 0.1f);
    modif |= PE::DragFloat3("Scale", glm::value_ptr(scale), 0.01f);
    if(modif)
    {
      m_changes.set(eNodeTransformDirty);
      node.translation = {translation.x, translation.y, translation.z};
      rotation         = glm::quat(glm::radians(euler));
      node.rotation    = {rotation.x, rotation.y, rotation.z, rotation.w};
      node.scale       = {scale.x, scale.y, scale.z};
      node.matrix.clear();  // Clear the matrix, has its been converted to translation, rotation and scale
    }
  }
  PE::end();
}

//--------------------------------------------------------------------------------------------------
// Returning the translation, rotation and scale of a node
// If the node has a matrix, it will decompose it
void GltfModelUI::getNodeTransform(const tinygltf::Node& node, glm::vec3& translation, glm::quat& rotation, glm::vec3& scale)
{
  translation = glm::vec3(0.0f);
  rotation    = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
  scale       = glm::vec3(1.0f);

  if(node.matrix.size() == 16)
  {
    glm::mat4 matrix = glm::make_mat4(node.matrix.data());
    glm::vec3 skew;
    glm::vec4 perspective;
    glm::decompose(matrix, scale, rotation, translation, skew, perspective);

    return;
  }
  if(node.translation.size() == 3)
  {
    translation = glm::make_vec3(node.translation.data());
  }
  if(node.rotation.size() == 4)
  {
    rotation = glm::make_quat(node.rotation.data());
  }
  if(node.scale.size() == 3)
  {
    scale = glm::make_vec3(node.scale.data());
  }
}

// Utility struct to handle material UI
struct MaterialUI
{
  glm::vec4 baseColorFactor;
  glm::vec3 emissiveFactor;
  int       alphaMode;

  static constexpr const char* alphaModes[] = {"OPAQUE", "MASK", "BLEND"};

  void toUI(tinygltf::Material& material)
  {
    baseColorFactor = glm::make_vec4(material.pbrMetallicRoughness.baseColorFactor.data());
    emissiveFactor  = glm::make_vec3(material.emissiveFactor.data());
    alphaMode       = material.alphaMode == "OPAQUE" ? 0 : material.alphaMode == "MASK" ? 1 : 2;
  }
  void fromUI(tinygltf::Material& material) const
  {
    material.pbrMetallicRoughness.baseColorFactor = {baseColorFactor.x, baseColorFactor.y, baseColorFactor.z,
                                                     baseColorFactor.w};
    material.emissiveFactor                       = {emissiveFactor.x, emissiveFactor.y, emissiveFactor.z};
    material.alphaMode                            = alphaModes[alphaMode];
  }
};

//--------------------------------------------------------------------------------------------------
// Rendering the material properties
// - Base color
// - Metallic
// - Roughness
// - Emissive
void GltfModelUI::renderMaterial(int materialIndex)
{
  auto& material = m_model.materials[materialIndex];

  ImGui::Text("Material: %s", material.name.c_str());

  // Example: Basic PBR properties
  PE::begin();
  {
    const double f64_zero = 0., f64_one = 1.;

    bool       modif      = false;
    MaterialUI materialUI = {};
    materialUI.toUI(material);
    modif |= PE::ColorEdit4("Base Color", glm::value_ptr(materialUI.baseColorFactor));
    modif |= PE::DragScalar("Metallic", ImGuiDataType_Double, &material.pbrMetallicRoughness.metallicFactor, 0.01f,
                            &f64_zero, &f64_one);
    modif |= PE::DragScalar("Roughness", ImGuiDataType_Double, &material.pbrMetallicRoughness.roughnessFactor, 0.01f,
                            &f64_zero, &f64_one);
    modif |= PE::ColorEdit3("Emissive", glm::value_ptr(materialUI.emissiveFactor));
    modif |= PE::DragScalar("Alpha Cutoff", ImGuiDataType_Double, &material.alphaCutoff, 0.01f, &f64_zero, &f64_one);
    modif |= PE::Combo("Alpha Mode", &materialUI.alphaMode, MaterialUI::alphaModes, IM_ARRAYSIZE(MaterialUI::alphaModes));
    modif |= PE::Checkbox("Double Sided", &material.doubleSided);
    if(modif)
    {
      materialUI.fromUI(material);
      m_changes.set(eMaterialDirty);
    }
  }
  PE::end();
}

// This function is called when a node is selected
// It will open all the parents of the selected node
void GltfModelUI::preprocessOpenNodes()
{
  openNodes.clear();
  if(selectedNodeIndex < 0)
  {
    return;
  }
  for(int rootIndex : m_model.scenes[0].nodes)  // Assuming sceneNodes contains root node indices
  {
    if(markOpenNodes(rootIndex, selectedNodeIndex, openNodes))
    {
      break;
    }
  }
}

// Recursive function to mark all the nodes that are in the path to the target node
bool GltfModelUI::markOpenNodes(int nodeIndex, int targetNodeIndex, std::unordered_set<int>& openNodes)
{
  if(nodeIndex == targetNodeIndex)
  {
    return true;
  }

  const auto& node = m_model.nodes[nodeIndex];
  for(int child : node.children)
  {
    if(markOpenNodes(child, targetNodeIndex, openNodes))
    {
      openNodes.insert(nodeIndex);  // Mark the current node as open if any child path leads to the target
      return true;
    }
  }
  return false;
}
