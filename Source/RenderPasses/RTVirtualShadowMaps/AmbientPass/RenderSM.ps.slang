Texture2D<float> gShadowMap;

SamplerState gSampler;

float4 mainImage(float2 texC)
{
    float3 out_color = float3(0.f);

    uint status = 0;
    float shadowMapValue = gShadowMap.Sample(gSampler, texC, 0, 0, status);
    bool mapped = CheckAccessFullyMapped(status);

    if(shadowMapValue == 0.f && mapped){
        // tile is mapped, but not used -> yellow
        out_color = float3(1.0f,1.0f,0.0f);
    }
    else{
        out_color = float3(shadowMapValue/100,0.f,0.f);
    }


    return float4(out_color, 1.f);
}

float4 main(float2 texC : TEXCOORD) : SV_TARGET
{
    return mainImage(texC);
}
