import RenderPasses.DeferredRenderer.ShadingHelper;

// GBuffer
Texture2D<float4> gWorldPos;
Texture2D<float4> gDiffuse;
Texture2D<float4> gSpecular;



cbuffer AmbientCB
{
    float2 iResolution;
};


float4 mainImage(float2 pixelCoord)
{
    float3 worldPosW = gWorldPos[pixelCoord].xyz;
    if (dot(worldPosW,worldPosW)==0) discard;


    float4 diffuseColor = gDiffuse[pixelCoord];
    float4 specularColor = gSpecular[pixelCoord];

    float4 color = (diffuseColor+specularColor) * kAmbientValue;

    return color;
}

float4 main(float2 texC : TEXCOORD) : SV_TARGET
{
    return mainImage(texC * iResolution);
}
