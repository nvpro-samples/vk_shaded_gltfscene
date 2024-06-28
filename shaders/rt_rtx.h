
// Forward declarations
float getOpacity(RenderNode renderNode, RenderPrimitive renderPrim, int triangleID, vec3 barycentrics);
Ray   getRay(vec2 samplePos, vec2 offset, vec2 imageSize, mat4 projMatrixI, mat4 viewMatrixI);


//-----------------------------------------------------------------------
// Shoot a ray an return the information of the closest hit, in the
// PtPayload structure (PRD)
//
void traceRay(Ray r, inout uint seed)
{
  hitPayload.hitT    = 0.0F;
  hitPayload.seed    = seed;
  hitPayload.rnodeID = -1;
  // If we want to cull back facing triangles, we need to set the flag. But if we want double sided,
  // the cull flag is set on the TLAS instance flag
  uint rayFlags = gl_RayFlagsNoneEXT | gl_RayFlagsCullBackFacingTrianglesEXT;
  traceRayEXT(topLevelAS, rayFlags, 0xFF, 0, 0, 0, r.origin, EPSILON, r.direction, INFINITE, 0);
  seed = hitPayload.seed;
}

//-----------------------------------------------------------------------
// Shadow ray - return true if a ray hits anything
//
bool traceShadow(Ray r, float maxDist, inout uint seed)
{
  hitPayload.hitT = 0.0F;
  hitPayload.seed = seed;
  uint rayFlags = gl_RayFlagsNoneEXT | gl_RayFlagsCullBackFacingTrianglesEXT;  //TerminateOnFirstHitEXT | gl_RayFlagsSkipClosestHitShaderEXT;
  traceRayEXT(topLevelAS, rayFlags, 0xFF, 0, 0, 0, r.origin, EPSILON, r.direction, maxDist, 0);
  bool isHit = (hitPayload.hitT != INFINITE);  // payload will change if miss shader is invoked
  seed       = hitPayload.seed;
  return isHit;
}

//-----------------------------------------------------------------------
// Shoot a ray and store 1 if the ray hits the selected object
void selectObject(vec2 samplePos, vec2 imageSize)
{
  float g_selectedObject = 0.0;
  if(pc.selectedRenderNode > -1)
  {
    Ray r              = getRay(samplePos, vec2(0, 0), imageSize, frameInfo.projMatrixI, frameInfo.viewMatrixI);
    hitPayload.rnodeID = -1;
    uint rayFlags      = gl_RayFlagsOpaqueEXT;
    traceRayEXT(topLevelAS, rayFlags, 0xFF, 0, 0, 0, r.origin, EPSILON, r.direction, INFINITE, 0);

    if(hitPayload.rnodeID != -1 && hitPayload.rnodeID == pc.selectedRenderNode)
      g_selectedObject = 1.0f;
  }

  imageStore(selectImage, ivec2(samplePos.xy), vec4(g_selectedObject, 0, 0, 1));
}