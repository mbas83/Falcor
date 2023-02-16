RWTexture2D<float> gShadowMap;

cbuffer ShadowMapCB
{
    float2 iResolution;
};


float4 mainImage(float2 pixelCoord)
{
    return float4(gShadowMap[pixelCoord], 0.f, 0.f, 1.f);
}

float4 main(float2 texC : TEXCOORD) : SV_TARGET
{
    return mainImage(texC * iResolution);
}
