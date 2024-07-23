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

#include <slang.h>
#include <vector>
#include <string>
#include <cstring>

// SlangCompiler class is used to compile Slang source code to SPIR-V code.

/* 

Usage example:

SlangCompiler slangC;
slangC.newSession(); // Create a new session for each batch of compilation. If a file change, create a new session.
auto compileRequest = slangC.createCompileRequest("path/to/file.slang", "main", SLANG_STAGE_COMPUTE);
SlangResult result = compileRequest->compile();
if (result != SLANG_OK)
{
	LOGE("Error compiling Slang source code: %s\n", compileRequest->getDiagnosticOutput());
    return false;
}
std::vector<uint32_t> spirvCode;
slangC.getSpirvCode(compileRequest, spirvCode);
VkShaderModuleCreateInfo shaderModuleCreateInfo{
    .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .codeSize = spirvCode.size() * sizeof(uint32_t),
    .pCode    = spirvCode.data(),
};

*/


class SlangCompiler
{
private:
  slang::IGlobalSession* m_globalSession{};
  slang::ISession*       m_session{};

public:
  SlangCompiler()
  {
    slang::createGlobalSession(&m_globalSession);
    newSession();
  }

  ~SlangCompiler()
  {
    if(m_session)
      m_session->release();
    if(m_globalSession)
      m_globalSession->release();
  }

  void newSession()
  {
    if(m_session)
      m_session->release();
    // Next we create a compilation session to generate SPIRV code from Slang source.
    slang::SessionDesc sessionDesc = {};
    slang::TargetDesc  targetDesc  = {};
    targetDesc.format              = SLANG_SPIRV;
    targetDesc.profile             = m_globalSession->findProfile("spirv_1_5");
    targetDesc.flags               = SLANG_TARGET_FLAG_GENERATE_SPIRV_DIRECTLY;
    sessionDesc.targets            = &targetDesc;
    sessionDesc.targetCount        = 1;

    m_globalSession->createSession(sessionDesc, &m_session);
  }

  slang::ICompileRequest* createCompileRequest(const std::string& filePath,
                                               const std::string& entryPointName = "main",
                                               SlangStage         stage          = SLANG_STAGE_COMPUTE)
  {
    slang::ICompileRequest* compileRequest{};
    m_session->createCompileRequest(&compileRequest);

    // Add source file
    compileRequest->addTranslationUnit(SLANG_SOURCE_LANGUAGE_SLANG, nullptr);
    compileRequest->addTranslationUnitSourceFile(0, filePath.c_str());

    compileRequest->addEntryPoint(0, entryPointName.c_str(), stage);
    compileRequest->setTargetForceGLSLScalarBufferLayout(0, true);

    return compileRequest;
  }

  // Compile Slang source code to SPIR-V code
  bool getSpirvCode(slang::ICompileRequest* compileRequest, std::vector<uint32_t>& spirvCode)
  {
    // Get SPIR-V code
    size_t      codeSize = 0;
    const void* codePtr  = compileRequest->getEntryPointCode(0, &codeSize);

    spirvCode.resize(codeSize / sizeof(uint32_t));
    memcpy(spirvCode.data(), codePtr, codeSize);

    compileRequest->release();
    return true;
  }
};
