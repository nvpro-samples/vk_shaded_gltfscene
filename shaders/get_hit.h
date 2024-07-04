// This function returns the geometric information at hit point
// Note: depends on the buffer layout PrimMeshInfo

#ifndef GETHIT_GLSL
#define GETHIT_GLSL

#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

#include "nvvkhl/shaders/ray_util.h"
#include "nvvkhl/shaders/vertex_accessor.h"
#include "nvvkhl/shaders/func.h"
#include "hit_state.h"

precision highp float;


//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
HitState getHitState(in RenderPrimitive renderPrim,      // Buffer containing all the mesh information
                     in vec3            barycentrics,    // Barycentics of the triangle
                     in int             triangleID,      // Triangle ID
                     in vec3            worldRayOrigin,  // Origin of the ray
                     in mat4x3          objectToWorld,   // Matrix
                     in mat4x3          worldToObject    // Matrix
)
{
  HitState hit;

  // Getting the 3 indices of the triangle (local)
  uvec3 triangleIndex = getTriangleIndices(renderPrim, triangleID);

  // Position
  vec3 pos[3];
  pos[0]  = getVertexPosition(renderPrim, triangleIndex.x);
  pos[1]  = getVertexPosition(renderPrim, triangleIndex.y);
  pos[2]  = getVertexPosition(renderPrim, triangleIndex.z);
  vec3 P  = mixBary(pos[0], pos[1], pos[2], barycentrics);
  hit.pos = vec3(objectToWorld * vec4(P, 1.0));
  //hit.shadowpos = pointOffset(objpos, pos[0], pos[1], pos[2], nrm[0], nrm[1], nrm[2], barycentrics);  // Shadow offset position - hacking shadow terminator
  //hit.shadowpos = vec3(objectToWorld * vec4(hit.shadowpos, 1.0));

  // Geometric Normal
  vec3 Ng    = normalize(cross(pos[1] - pos[0], pos[2] - pos[0]));
  hit.geonrm = normalize(vec3(Ng * worldToObject));

  // Normal
  hit.nrm = hit.geonrm;
  if(hasVertexNormal(renderPrim))
  {
    vec3 N  = getInterpolatedVertexNormal(renderPrim, triangleIndex, barycentrics);
    hit.nrm = normalize(vec3(N * worldToObject));
  }

  // TexCoord
  hit.uv = getInterpolatedVertexTexCoord0(renderPrim, triangleIndex, barycentrics);

  // Color
  hit.color = getInterpolatedVertexColor(renderPrim, triangleIndex, barycentrics);

  // Tangent - Bitangent
  vec4 tng[3];
  if(hasVertexTangent(renderPrim))
  {
    tng[0] = getVertexTangent(renderPrim, triangleIndex.x);
    tng[1] = getVertexTangent(renderPrim, triangleIndex.y);
    tng[2] = getVertexTangent(renderPrim, triangleIndex.z);
  }
  else
  {
    vec4 t = makeFastTangent(hit.nrm);
    tng[0] = t;
    tng[1] = t;
    tng[2] = t;
  }

  {
    hit.tangent   = normalize(mixBary(tng[0].xyz, tng[1].xyz, tng[2].xyz, barycentrics));
    hit.tangent   = vec3(objectToWorld * vec4(hit.tangent, 0.0));
    hit.tangent   = normalize(hit.tangent - hit.nrm * dot(hit.nrm, hit.tangent));
    hit.bitangent = cross(hit.nrm, hit.tangent) * tng[0].w;
  }

  // Adjusting normal
  const vec3 V = (worldRayOrigin - hit.pos);
  if(dot(hit.geonrm, V) < 0)  // Flip if back facing
    hit.geonrm = -hit.geonrm;

  // If backface
  if(dot(hit.geonrm, hit.nrm) < 0)  // Make Normal and GeoNormal on the same side
  {
    hit.nrm       = -hit.nrm;
    hit.tangent   = -hit.tangent;
    hit.bitangent = -hit.bitangent;
  }

  // handle low tessellated meshes with smooth normals
  vec3 k2 = reflect(-V, hit.nrm);
  if(dot(hit.geonrm, k2) < 0.0f)
    hit.nrm = hit.geonrm;

  return hit;
}


#endif
