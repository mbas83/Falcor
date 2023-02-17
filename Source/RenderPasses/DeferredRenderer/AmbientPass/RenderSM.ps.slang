RWTexture2D<float> gShadowMap;


float4 mainImage(float2 texC)
{
    uint2 resolution;
    gShadowMap.GetDimensions(resolution.x,resolution.y);

    float2 pixelCoord = texC*resolution;

    return float4(gShadowMap[pixelCoord], 0.f, 0.f, 1.f);
}

float4 main(float2 texC : TEXCOORD) : SV_TARGET
{
    return mainImage(texC);
}
