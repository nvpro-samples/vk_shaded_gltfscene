

// Forwarded declarations
void traceRay(Ray r, inout uint seed);
bool traceShadow(Ray r, float maxDist, inout uint seed);

// --------------------------------------------------------------------
// Sampling the Sun or the HDR
// - Returns
//      The contribution divided by PDF
//      The direction to the light source
//      The PDF
//
vec3 sampleLights(in vec3 pos, vec3 normal, in vec3 worldRayDirection, inout uint seed, out vec3 dirToLight, out float lightPdf)
{
  vec3 radiance = vec3(0);
  lightPdf      = 1.0;

  if(frameInfo.useSky == 1)
  {
    Light        sunLight     = frameInfo.light[0];
    vec2         rand_val     = vec2(rand(seed), rand(seed));
    LightContrib lightContrib = singleLightContribution(sunLight, pos, normal, -worldRayDirection, rand_val);

    radiance   = lightContrib.intensity;
    dirToLight = normalize(-lightContrib.incidentVector);
    lightPdf   = DIRAC;
    // Return radiance over pdf
    return radiance;
  }
  else
  {
    vec3 rand_val     = vec3(rand(seed), rand(seed), rand(seed));
    vec4 radiance_pdf = environmentSample(hdrTexture, rand_val, dirToLight);
    radiance          = radiance_pdf.xyz;
    lightPdf          = radiance_pdf.w;

    // Apply rotation and environment intensity
    dirToLight = rotate(dirToLight, vec3(0, 1, 0), frameInfo.envRotation);
    radiance *= frameInfo.envIntensity.xyz;

    // Return radiance over pdf
    return radiance / lightPdf;
  }

  // Return radiance over pdf
  return radiance / lightPdf;
}


//----------------------------------------------------------
// Testing if the hit is opaque or alpha-transparent
// Return true is opaque
//----------------------------------------------------------
float getOpacity(RenderNode renderNode, RenderPrimitive renderPrim, int triangleID, vec3 barycentrics)
{
  // Scene materials
  uint              matIndex = max(0, renderNode.materialID);
  GltfShadeMaterial mat      = GltfMaterialBuf(sceneDesc.materialAddress).m[matIndex];

  float baseColorAlpha = mat.pbrBaseColorFactor.a;
  if(mat.pbrBaseColorTexture > -1)
  {

    // Getting the 3 indices of the triangle (local)
    uvec3 triangleIndex = getTriangleIndices(renderPrim, triangleID);

    // Retrieve the interpolated texture coordinate from the vertex
    vec2 uv = getInterpolatedVertexTexCoord0(renderPrim, triangleIndex, barycentrics);

    baseColorAlpha *= texture(texturesMap[nonuniformEXT(mat.pbrBaseColorTexture)], uv).a;
  }

  float opacity;
  if(mat.alphaMode == ALPHA_MASK)
  {
    opacity = baseColorAlpha > mat.alphaCutoff ? 1.0 : 0.0;
  }
  else
  {
    opacity = baseColorAlpha;
  }

  return opacity;
}

Ray getRay(vec2 samplePos, vec2 offset, vec2 imageSize, mat4 projMatrixI, mat4 viewMatrixI)
{
  const vec2 pixelCenter = vec2(samplePos.xy) + offset;
  const vec2 inUV        = pixelCenter / vec2(imageSize.xy);
  const vec2 d           = inUV * 2.0 - 1.0;
  const vec4 origin      = viewMatrixI * vec4(0.0F, 0.0F, 0.0F, 1.0F);
  const vec4 target      = projMatrixI * vec4(d.x, d.y, 0.01F, 1.0F);
  const vec4 direction   = viewMatrixI * vec4(normalize(target.xyz), 0.0F);

  return Ray(origin.xyz, direction.xyz);
}


//-----------------------------------------------------------------------
//
//-----------------------------------------------------------------------
vec3 pathTrace(Ray r, inout uint seed)
{
  vec3 radiance   = vec3(0.0F);
  vec3 throughput = vec3(1.0F);
  bool isInside   = false;


  GltfMaterialBuf materials = GltfMaterialBuf(sceneDesc.materialAddress);

  for(int depth = 0; depth < pc.maxDepth; depth++)
  {
    traceRay(r, seed);

    HitState hit = hitPayload.hit;

    // Hitting the environment, then exit
    if(hitPayload.hitT == INFINITE)
    {
      if(frameInfo.useSky == 1)
      {
        radiance += proceduralSky(skyInfo, r.direction, 0);
      }
      else
      {
        // Adding HDR lookup
        vec3 dir = rotate(r.direction, vec3(0, 1, 0), -frameInfo.envRotation);
        vec2 uv  = getSphericalUv(dir);  // See sampling.glsl
        vec3 env = texture(hdrTexture, uv).rgb;
        radiance += env * frameInfo.envIntensity.xyz;
      }

      radiance *= throughput;
      return radiance;
    }

    // Retrieve the Instance buffer information
    RenderNode      renderNode = RenderNodeBuf(sceneDesc.renderNodeAddress)._[hitPayload.rnodeID];
    RenderPrimitive renderPrim = RenderPrimitiveBuf(sceneDesc.renderPrimitiveAddress)._[hitPayload.rprimID];

    // Setting up the material
    GltfShadeMaterial material = materials.m[renderNode.materialID];  // Material of the hit object
    material.pbrBaseColorFactor *= hit.color;                         // Color at vertices
    PbrMaterial pbrMat = evaluateMaterial(material, hit.nrm, hit.tangent, hit.bitangent, hit.uv, isInside);

    // Adding emissive
    radiance += pbrMat.emissive * throughput;

    // Apply volume attenuation
    bool thin_walled = pbrMat.thicknessFactor == 0;
    if(isInside && !thin_walled)
    {
      const vec3 abs_coeff = absorptionCoefficient(pbrMat);
      throughput.x *= abs_coeff.x > 0.0 ? exp(-abs_coeff.x * hitPayload.hitT) : 1.0;
      throughput.y *= abs_coeff.y > 0.0 ? exp(-abs_coeff.y * hitPayload.hitT) : 1.0;
      throughput.z *= abs_coeff.z > 0.0 ? exp(-abs_coeff.z * hitPayload.hitT) : 1.0;
    }

    // Light contribution; can be environment or punctual lights
    vec3  contribution         = vec3(0);
    vec3  dirToLight           = vec3(0);
    float lightPdf             = 0.F;
    vec3  lightRadianceOverPdf = sampleLights(hit.pos, pbrMat.normal, r.direction, seed, dirToLight, lightPdf);

    // do not next event estimation (but delay the adding of contribution)
    const bool nextEventValid = ((dot(dirToLight, hit.geonrm) > 0.0f) != isInside) && lightPdf != 0.0f;

    // Evaluate BSDF
    if(nextEventValid)
    {
      BsdfEvaluateData evalData;
      evalData.k1 = -r.direction;
      evalData.k2 = dirToLight;

      bsdfEvaluate(evalData, pbrMat);

      if(evalData.pdf > 0.0)
      {
        const float mis_weight = (lightPdf == DIRAC) ? 1.0f : lightPdf / (lightPdf + evalData.pdf);

        // sample weight
        const vec3 w = throughput * lightRadianceOverPdf * mis_weight;
        contribution += w * evalData.bsdf_diffuse;
        contribution += w * evalData.bsdf_glossy;
      }
    }

    // Sample BSDF
    {
      BsdfSampleData sampleData;
      sampleData.k1            = -r.direction;  // outgoing direction
      sampleData.xi            = vec4(rand(seed), rand(seed), rand(seed), rand(seed));
      sampleData.event_type    = 0;                    ///< output: the type of event for the generated sample
      sampleData.pdf           = 0.0;                  // output: pdf (non-projected hemisphere)
      sampleData.bsdf_over_pdf = vec3(0.0, 0.0, 0.0);  ///< output: bsdf * dot(normal, k2) / pdf

      bsdfSample(sampleData, pbrMat);

      throughput *= sampleData.bsdf_over_pdf;
      r.direction = sampleData.k2;

      if(sampleData.event_type == BSDF_EVENT_ABSORB)
      {
        break;  // Need to add the contribution ?
      }
      else
      {
        // Continue path
        bool isSpecular     = (sampleData.event_type & BSDF_EVENT_SPECULAR) != 0;
        bool isTransmission = (sampleData.event_type & BSDF_EVENT_TRANSMISSION) != 0;

        vec3 offsetDir = dot(r.direction, hit.geonrm) > 0 ? hit.geonrm : -hit.geonrm;
        r.origin       = offsetRay(hit.pos, offsetDir);

        if(isTransmission)
        {
          isInside = !isInside;
        }
      }
    }

    // We are adding the contribution to the radiance only if the ray is not occluded by an object.
    if(nextEventValid)
    {
      Ray  shadowRay = Ray(r.origin, dirToLight);
      bool inShadow  = traceShadow(shadowRay, INFINITE, seed);
      // We are adding the contribution to the radiance only if the ray is not occluded by an object.
      if(!inShadow)
      {
        radiance += contribution;
      }
    }

#if USE_RUSIAN_ROULETTE
    // Russian-Roulette (minimizing live state)
    float rrPcont = min(max(throughput.x, max(throughput.y, throughput.z)) + 0.001F, 0.95F);
    if(rand(seed) >= rrPcont)
      break;                // paths with low throughput that won't contribute
    throughput /= rrPcont;  // boost the energy of the non-terminated paths
#endif
  }

  return radiance;
}


//-----------------------------------------------------------------------
// Sampling the pixel
//-----------------------------------------------------------------------
vec3 samplePixel(inout uint seed, vec2 samplePos, vec2 subpixelJitter, vec2 imageSize, mat4 projMatrixI, mat4 viewMatrixI)
{
  Ray ray = getRay(samplePos, subpixelJitter, imageSize, projMatrixI, viewMatrixI);

  vec3 radiance = pathTrace(ray, seed);

// Removing fireflies
#if USE_FIREFLY_FILTER
  float lum = dot(radiance, vec3(0.212671F, 0.715160F, 0.072169F));
  if(lum > pc.maxLuminance)
  {
    radiance *= pc.maxLuminance / lum;
  }
#endif

  return radiance;
}


//---
vec3 debugRendering(vec2 samplePos, vec2 imageSize)
{
  uint seed = 0;

  Ray ray = getRay(samplePos, vec2(0, 0), imageSize, frameInfo.projMatrixI, frameInfo.viewMatrixI);
  traceRay(ray, seed);

  HitState hit = hitPayload.hit;

  if(hitPayload.hitT == INFINITE)
    return vec3(0);

  // Retrieve the Instance buffer information
  RenderNode      renderNode = RenderNodeBuf(sceneDesc.renderNodeAddress)._[hitPayload.rnodeID];
  RenderPrimitive renderPrim = RenderPrimitiveBuf(sceneDesc.renderPrimitiveAddress)._[hitPayload.rprimID];

  // Setting up the material
  GltfMaterialBuf   materials = GltfMaterialBuf(sceneDesc.materialAddress);  // Buffer of materials
  GltfShadeMaterial material  = materials.m[renderNode.materialID];          // Material of the hit object
  PbrMaterial       pbrMat    = evaluateMaterial(material, hit.nrm, hit.tangent, hit.bitangent, hit.uv);

  switch(pc.dbgMethod)
  {
    case eDbgMethod_metallic:
      return vec3(pbrMat.metallic);
    case eDbgMethod_roughness:
      return vec3(pbrMat.roughness);
    case eDbgMethod_normal:
      return vec3(pbrMat.normal * .5 + .5);
    case eDbgMethod_basecolor:
      return vec3(pbrMat.albedo);
    case eDbgMethod_emissive:
      return vec3(pbrMat.emissive);
  }

  return vec3(0);
}
