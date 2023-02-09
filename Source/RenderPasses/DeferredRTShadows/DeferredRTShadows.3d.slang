import Scene.RaytracingInline;
import Utils.Math.Ray;
import Utils.Math.MathHelpers;
import Rendering.Materials.TexLODHelpers;

import RenderPasses.ShadowRayCache.Helper;


cbuffer PerFrameCB
{
    float2 viewportDims;
};

struct VsIn{
    float radius : RADIUSSIZE;
    uint index : LIGHTINDEX;
};

struct VsOut{
    float4 posH : SV_POSITION;

    float radius : RADIUSSIZE;
    uint index: LIGHTINDEX;
};


VsOut vsMain(VsIn vIn)
{
    VsOut vsOut;

    // pass through
    vsOut.radius = vIn.radius;
    vsOut.index = vIn.index;
        
    vsOut.posH = mul(float4(gScene.getLight(vsOut.index).posW,1.f), gScene.camera.data.viewMat);
    vsOut.posH = mul(vsOut.posH, gScene.camera.data.projMat);
    
    return vsOut;
}


struct GsOut
{
    float4 posH : SV_POSITION;

    float radius : RADIUSSIZE;
    uint index : LIGHTINDEX;
};

[maxvertexcount(4)]
void gsMain(point VsOut input[1], inout TriangleStream<GsOut> outStream)
{
    GsOut gsOut;
    gsOut.radius = input[0].radius;
    gsOut.index = input[0].index;

    float radius = input[0].radius;

    const LightData light = gScene.getLight(gsOut.index);

    float3 lightPosW = light.posW;

    // billboard center 
    float4 billboard_center  = float4(lightPosW ,1.f);

    float4 center_pos = mul(billboard_center, gScene.camera.data.viewMat);

    if(center_pos.z - radius > -gScene.camera.data.nearZ){
        return;
    }

    center_pos.z = min(center_pos.z, -gScene.camera.data.nearZ);

    // vertex 0
    float4 newPos = center_pos + float4(-radius, -radius , 0  , 0);
    gsOut.posH = mul(newPos,gScene.camera.data.projMat);
    outStream.Append(gsOut);

    // vertex 1
    newPos = center_pos + float4(+radius, -radius , 0  , 0);
    gsOut.posH = mul(newPos,gScene.camera.data.projMat);
    outStream.Append(gsOut);

    // vertex 2
    newPos = center_pos + float4(-radius, +radius , 0  , 0);
    gsOut.posH = mul(newPos,gScene.camera.data.projMat);
    outStream.Append(gsOut);

    // vertex 3
    newPos = center_pos + float4(+radius, +radius , 0  , 0);
    gsOut.posH = mul(newPos,gScene.camera.data.projMat);
    outStream.Append(gsOut);

    outStream.RestartStrip();

}

// GBuffer
Texture2D<float4> gWorldPos;
Texture2D<float4> gWorldNormal;
Texture2D<float4> gDiffuse;
Texture2D<float4> gSpecular;
Texture2D<float4> gTexGrad;

// Output
RWTexture2D<float4> gDebug;


float4 psMain(GsOut gsOut) : SV_TARGET0
{
    float4 out_color;

    int2 pixelCoord = gsOut.posH.xy;
    uint lightIndex = gsOut.index;

    const LightData light = gScene.getLight(lightIndex);
    float3 lightPosW = light.posW;

    float3 diffuseColor = gDiffuse[pixelCoord].rgb;
    float3 specularColor = gSpecular[pixelCoord].rgb;
    float3 worldPosW = gWorldPos[pixelCoord].xyz;
    float3 normal = normalize(gWorldNormal[pixelCoord].xyz);

    float3 direction = lightPosW - worldPosW;
    float dist = length(direction);
   
    if (dist >= gsOut.radius) discard; // skip lights that are too far away
    if (dot(worldPosW,worldPosW)==0) discard;
    // normalize direction
    direction = direction / dist;
    // physically incorrect attenuation (but this gives the point light an actual radius)
    float attenuation = pow(1.0 - pow(dist / gsOut.radius, 2.0), 2.0);

    // use correct attenuation
    attenuation = 1.f / (dist*dist);
    float3 lightColor = max(dot(direction, normal), 0.0) * light.intensity * attenuation;

    
    float NdotL = saturate(dot(normal, direction));
    const float shininess = 5.0;
    out_color = float4(lightColor * (diffuseColor + pow(NdotL, shininess) * specularColor), 1);

    // calc shadow
    float3 shadowRay = lightPosW - worldPosW;
    float3 rayDir = normalize(shadowRay);
    float shadowTMax = length(shadowRay);
    
    float shadowFactor = 1.0f;
    
    // create shadow ray from eye to light
    // Ray(origin,  dir,  tMin = 0.f, float tMax = FLT_MAX)
    Ray ray = Ray(worldPosW, rayDir, 1.0e-4, shadowTMax);

    SceneRayQuery<0> sceneRayQuery;
    HitInfo hit;
    float hitT;
    uint flags = RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER;

    if (sceneRayQuery.traceRay(ray, hit, hitT, flags, 0xff))
    {
        shadowFactor = 0.0f;
    }


    out_color *= shadowFactor;

    return out_color;
}
