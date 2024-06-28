
#include "nvvkhl/shaders/dh_sky.h"
#include "nvvkhl/shaders/dh_scn_desc.h"
#include "nvvkhl/shaders/dh_hdr.h"
#include "nvvkhl/shaders/bsdf_functions.h"
#include "nvvkhl/shaders/random.glsl"
#include "nvvkhl/shaders/ray_util.glsl"
#include "nvvkhl/shaders/light_contrib.glsl"
#include "nvvkhl/shaders/vertex_accessor.h"

// clang-format off
layout(push_constant, scalar)                   uniform                             RtxPushConstant_ { PushConstantPathtracer pc; };

layout(buffer_reference, scalar)                readonly buffer                     GltfMaterialBuf  { GltfShadeMaterial m[]; };

layout(set = 0, binding = eTlas)                uniform accelerationStructureEXT    topLevelAS;
layout(set = 0, binding = eOutImage, rgba32f)   uniform image2D                     image;
layout(set = 0, binding = eSelect)              uniform image2D                     selectImage;
layout(set = 1, binding = eFrameInfo, scalar)   uniform                             FrameInfo_      { SceneFrameInfo frameInfo; };
layout(set = 1, binding = eSceneDesc, scalar)   readonly buffer                     SceneDesc_      { SceneDescription sceneDesc; };
layout(set = 1, binding = eTextures)            uniform sampler2D                   texturesMap[]; // all textures
layout(set = 2, binding = eSkyParam, scalar)    uniform                             SkyInfo_        { ProceduralSkyShaderParameters skyInfo; };
layout(set = 3, binding = eImpSamples, scalar)  readonly buffer                     EnvAccel_       { EnvAccel envSamplingData[]; };
layout(set = 3, binding = eHdr)                 uniform sampler2D                   hdrTexture;
// clang-format on
