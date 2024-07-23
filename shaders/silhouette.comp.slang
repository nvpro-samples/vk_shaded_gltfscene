// shader.slang

struct PushConstantSilhouette
{
  float3 color;
};

[[vk::binding(0)]]
Texture2D<float> u_R8Buffer;
[[vk::binding(1)]]
RWTexture2D<float4> u_RgbaImage;
[[vk::push_constant]]
ConstantBuffer<PushConstantSilhouette> pushConst;

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadID: SV_DispatchThreadID)
{
  uint2 texSize;
  u_RgbaImage.GetDimensions(texSize.x, texSize.y); // DispatchRaysDimensions();

  const uint2 pixelCoord = uint2(dispatchThreadID.xy);

  // Define the Sobel kernel in the x and y directions
  const float kernelX[9] = float[](-1, 0, 1, -2, 0, 2, -1, 0, 1);
  const float kernelY[9] = float[](-1, -2, -1, 0, 0, 0, 1, 2, 1);

  float sumX = 0.0;
  float sumY = 0.0;

  // Check boundaries
  if (pixelCoord.x > 0 && pixelCoord.x < texSize.x - 1 && pixelCoord.y > 0 &&
      pixelCoord.y < texSize.y - 1)
  {
    [unroll]
    for (int i = -1; i <= 1; i++)
    {
      [unroll]
      for (int j = -1; j <= 1; j++)
      {
        const uint2 offset = uint2(i, j);
        const float val = u_R8Buffer[pixelCoord + offset].r;
        sumX += val * kernelX[(i + 1) * 3 + (j + 1)];
        sumY += val * kernelY[(i + 1) * 3 + (j + 1)];
      }
    }
  }

  // Compute the magnitude of the gradient
  const float magnitude = (sumX * sumX + sumY * sumY);

  // If the magnitude is above a certain threshold, we consider it an edge
  const float min_threshold = 2.0;
  const float max_threshold = 20.0;

  // Write the result to the output image
  u_RgbaImage[pixelCoord].xyz =
      lerp(u_RgbaImage[pixelCoord].xyz, pushConst.color,
           smoothstep(min_threshold, max_threshold, magnitude));
}
