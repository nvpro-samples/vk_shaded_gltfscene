
// Forward declarations
float getOpacity(RenderNode renderNode, RenderPrimitive renderPrim, int triangleID, vec3 barycentrics);
Ray   getRay(vec2 samplePos, vec2 offset, vec2 imageSize, mat4 projMatrixI, mat4 viewMatrixI);


//-----------------------------------------------------------------------
// Shoot a ray an return the information of the closest hit, in the
// PtPayload structure (PRD)
//
void traceRay(Ray ray, inout uint seed)
{
  rayQueryEXT rayQuery;

  hitPayload.hitT = 0.0F;
  uint rayFlags   = gl_RayFlagsNoneEXT | gl_RayFlagsCullBackFacingTrianglesEXT;
  rayQueryInitializeEXT(rayQuery, topLevelAS, rayFlags, 0xFF, ray.origin, 0.0, ray.direction, INFINITE);

  while(rayQueryProceedEXT(rayQuery))
  {
    if(rayQueryGetIntersectionTypeEXT(rayQuery, false) == gl_RayQueryCandidateIntersectionTriangleEXT)
    {
      int  instanceID   = rayQueryGetIntersectionInstanceIdEXT(rayQuery, false);
      int  renderPrimID = rayQueryGetIntersectionInstanceCustomIndexEXT(rayQuery, false);
      int  triangleID   = rayQueryGetIntersectionPrimitiveIndexEXT(rayQuery, false);
      vec2 bary         = rayQueryGetIntersectionBarycentricsEXT(rayQuery, false);

      // Barycentric coordinate on the triangle
      const vec3 barycentrics = vec3(1.0 - bary.x - bary.y, bary.x, bary.y);

      RenderNode      renderNode = RenderNodeBuf(sceneDesc.renderNodeAddress)._[instanceID];
      RenderPrimitive renderPrim = RenderPrimitiveBuf(sceneDesc.renderPrimitiveAddress)._[renderPrimID];

      float opacity = getOpacity(renderNode, renderPrim, triangleID, barycentrics);

      // do alpha blending the stochastically way
      if(rand(seed) <= opacity)
        rayQueryConfirmIntersectionEXT(rayQuery);
    }
  }

  if(rayQueryGetIntersectionTypeEXT(rayQuery, true) != gl_RayQueryCommittedIntersectionNoneEXT)
  {
    vec2   bary              = rayQueryGetIntersectionBarycentricsEXT(rayQuery, true);
    int    instanceID        = rayQueryGetIntersectionInstanceIdEXT(rayQuery, true);
    int    renderPrimID      = rayQueryGetIntersectionInstanceCustomIndexEXT(rayQuery, true);
    int    triangleID        = rayQueryGetIntersectionPrimitiveIndexEXT(rayQuery, true);
    mat4x3 worldToObject     = rayQueryGetIntersectionWorldToObjectEXT(rayQuery, true);
    mat4x3 objectToWorld     = rayQueryGetIntersectionObjectToWorldEXT(rayQuery, true);
    float  hitT              = rayQueryGetIntersectionTEXT(rayQuery, true);
    vec3   worldRayDirection = rayQueryGetWorldRayDirectionEXT(rayQuery);
    vec3   worldRayOrigin    = rayQueryGetWorldRayOriginEXT(rayQuery);

    // Retrieve the Primitive mesh buffer information
    RenderNode      renderNode = RenderNodeBuf(sceneDesc.renderNodeAddress)._[instanceID];
    RenderPrimitive renderPrim = RenderPrimitiveBuf(sceneDesc.renderPrimitiveAddress)._[renderPrimID];

    hitPayload.hitT    = hitT;
    hitPayload.rnodeID = instanceID;
    hitPayload.rprimID = renderPrimID;  // Should be equal to renderNode.rprimID

    const vec3 barycentrics = vec3(1.0 - bary.x - bary.y, bary.x, bary.y);

    hitPayload.hit = getHitState(renderPrim, barycentrics, triangleID, worldRayOrigin, objectToWorld, worldToObject);
  }
  else
  {
    hitPayload.hitT = INFINITE;
  }
}

//-----------------------------------------------------------------------
// Shadow ray - return true if a ray hits anything
//
bool traceShadow(Ray ray, float maxDist, inout uint seed)
{
  rayQueryEXT rayQuery;

  uint rayFlags = gl_RayFlagsNoneEXT | gl_RayFlagsCullBackFacingTrianglesEXT;
  rayQueryInitializeEXT(rayQuery, topLevelAS, rayFlags, 0xFF, ray.origin, 0.0, ray.direction, maxDist);

  while(rayQueryProceedEXT(rayQuery))
  {  // Force opaque, therefore, no intersection confirmation needed
    int  instanceID   = rayQueryGetIntersectionInstanceIdEXT(rayQuery, false);
    int  renderPrimID = rayQueryGetIntersectionInstanceCustomIndexEXT(rayQuery, false);
    int  triangleID   = rayQueryGetIntersectionPrimitiveIndexEXT(rayQuery, false);
    vec2 bary         = rayQueryGetIntersectionBarycentricsEXT(rayQuery, false);

    // Barycentric coordinate on the triangle
    const vec3 barycentrics = vec3(1.0 - bary.x - bary.y, bary.x, bary.y);

    RenderNode      renderNode = RenderNodeBuf(sceneDesc.renderNodeAddress)._[instanceID];
    RenderPrimitive renderPrim = RenderPrimitiveBuf(sceneDesc.renderPrimitiveAddress)._[renderPrimID];

    float opacity = getOpacity(renderNode, renderPrim, triangleID, barycentrics);
    if(rand(seed) <= opacity)
    {
      rayQueryConfirmIntersectionEXT(rayQuery);
    }
  }

  return (rayQueryGetIntersectionTypeEXT(rayQuery, true) != gl_RayQueryCommittedIntersectionNoneEXT);  // Is Hit ?
}


//-----------------------------------------------------------------------
// Shoot a ray and store 1 if the ray hits the selected object
void selectObject(vec2 samplePos, vec2 imageSize)
{
  float g_selectedObject = 0.0;
  if(pc.selectedRenderNode > -1)
  {
    Ray ray            = getRay(samplePos, vec2(0, 0), imageSize, frameInfo.projMatrixI, frameInfo.viewMatrixI);
    hitPayload.rnodeID = -1;
    uint rayFlags      = gl_RayFlagsOpaqueEXT;
    {
      rayQueryEXT rayQuery;
      rayQueryInitializeEXT(rayQuery, topLevelAS, rayFlags, 0xFF, ray.origin, 0.0, ray.direction, INFINITE);
      while(rayQueryProceedEXT(rayQuery))
      {
        if(rayQueryGetIntersectionTypeEXT(rayQuery, false) == gl_RayQueryCandidateIntersectionTriangleEXT)
        {
          rayQueryConfirmIntersectionEXT(rayQuery);
        }
      }
      if(rayQueryGetIntersectionTypeEXT(rayQuery, true) != gl_RayQueryCommittedIntersectionNoneEXT)
      {
        hitPayload.rnodeID = rayQueryGetIntersectionInstanceIdEXT(rayQuery, true);
      }
    }

    if(hitPayload.rnodeID != -1 && hitPayload.rnodeID == pc.selectedRenderNode)
      g_selectedObject = 1.0f;
  }

  imageStore(selectImage, ivec2(samplePos.xy), vec4(g_selectedObject, 0, 0, 1));
}
