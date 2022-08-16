import Scene.RaytracingInline;
import Utils.Math.Ray;
import Utils.Math.MathHelpers;
import Rendering.Materials.TexLODHelpers;

import RenderPasses.ShadowRayCache.Helper;


cbuffer PerFrameCB
{
    float gDepthBias;
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

    //billboard tangent space
    float3 n = normalize(gScene.camera.data.posW - lightPosW);
    float3 t = normalize(cross(float3(0,1,0), n));
    float3 b = normalize(cross(n,t));

    // move billboard center along view direction
    float4 billboard_center  = float4(lightPosW - (n * radius),1.f);

    //float4 center_pos = mul(float4(billboard_center,1.f), gScene.camera.data.viewMat);
      
    // vertex 0
    float4 newPos = billboard_center - radius*float4(t,0) - radius*float4(b,0) ;
    gsOut.posH = mul(newPos,gScene.camera.data.viewProjMat);
    outStream.Append(gsOut);

    // vertex 1
    newPos = billboard_center + radius*float4(t,0) - radius*float4(b,0);
    gsOut.posH = mul(newPos,gScene.camera.data.viewProjMat);
    outStream.Append(gsOut);

    // vertex 2
    newPos = billboard_center - radius*float4(t,0) + radius*float4(b,0);
    gsOut.posH = mul(newPos,gScene.camera.data.viewProjMat);
    outStream.Append(gsOut);

    // vertex 3
    newPos = billboard_center + radius*float4(t,0) + radius*float4(b,0);
    gsOut.posH = mul(newPos,gScene.camera.data.viewProjMat);
    outStream.Append(gsOut);

    outStream.RestartStrip();

}



// Virtual Shadow Maps
RWTexture2D<float> gVirtualShadowMaps[(uint)SHADOWTEXTURECOUNT];
// Feedback Maps
RWTexture2D<float> gFeedbackMaps[(uint)FEEDBACKTEXTURECOUNT];

// GBuffer
Texture2D<float4> gWorldPos;
Texture2D<float4> gWorldNormal;
Texture2D<float4> gDiffuse;
Texture2D<float4> gSpecular;
Texture2D<float4> gTexGrad;

// LOD level visualisation
Texture1D<float4> gMipColor;

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

    // calc mipLevel
    float3 eyePos = gScene.camera.data.posW;
    float3 eyeRay = worldPosW - eyePos;


    // Using usual RayDiffs (TODO: or use raygems ch.20 g-buffer version?)
    float3 dDdx, dDdy;
    // calculate intial raydiff (eq. 8)

    float2 p = (pixelCoord.xy + float2(0.5f, 0.5f)) / viewportDims.xy; // Pixel center on image plane in [0,1] where (0,0) is top-left
    float2 ndc = float2(2, -2) * p + float2(-1, 1);
    float3 nonNormalizedRayDir =  ndc.x * gScene.camera.data.cameraU + ndc.y * gScene.camera.data.cameraV + gScene.camera.data.cameraW; // rayDir = world-space direction to point on image plane (unnormalized)

    computeRayDirectionDifferentials(nonNormalizedRayDir, gScene.camera.data.cameraU, gScene.camera.data.cameraV, pixelCoord, dDdx, dDdy);

    RayDiff rayDiff = RayDiff(float3(0.f), float3(0.f), dDdx, dDdy);

    float3 rayOrg = eyePos;
    float3 rayDir = normalize(eyeRay);
    float primaryHitT = length(eyeRay);

    RayDiff newRayDiff = rayDiff.propagate(rayOrg, rayDir, primaryHitT, normal);

    uint2 resolution;
    // get dimensions of mip0
    gVirtualShadowMaps[0].GetDimensions(resolution.x,resolution.y);

    // calc the two positions at hit point
    float3 hitPointDx1 = worldPosW  + newRayDiff.getdOdx() / 2;
    float3 hitPointDx2 = worldPosW  - newRayDiff.getdOdx() / 2;
    // for dy
    float3 hitPointDy1 = worldPosW  + newRayDiff.getdOdy() / 2;
    float3 hitPointDy2 = worldPosW  - newRayDiff.getdOdy() / 2;

    float2 shadowCoordsDx1 = world_to_latlong_map(normalize(hitPointDx1 - lightPosW)) * resolution;
    float2 shadowCoordsDx2 = world_to_latlong_map(normalize(hitPointDx2 - lightPosW)) * resolution;
    // for dy
    float2 shadowCoordsDy1 = world_to_latlong_map(normalize(hitPointDy1 - lightPosW)) * resolution;
    float2 shadowCoordsDy2 = world_to_latlong_map(normalize(hitPointDy2 - lightPosW)) * resolution;

    float2 ShadowCoordDx = abs(shadowCoordsDx1 - shadowCoordsDx2);
    float2 ShadowCoordDy = abs(shadowCoordsDy1 - shadowCoordsDy2);


    uint mipLevel = (uint) clamp( calcLod(ShadowCoordDx, ShadowCoordDy), 0, MIPCOUNT);
       
    
    // Update Resolution to chosen mipmap level
    gVirtualShadowMaps[NonUniformResourceIndex(MIPCOUNT*lightIndex+mipLevel)].GetDimensions(resolution.x,resolution.y);
    float2 shadowMapCoord = world_to_latlong_map(-direction) * resolution;
    // ray should always go through center of the shadow map pixel
    float3 pixel_centered_dir = latlong_map_to_world( (floor(shadowMapCoord)+float2(0.5f)) / resolution);
        

    float shadowFactor = 1.0f;
    float bias = max(gDepthBias * (1.0f - dot(normal, pixel_centered_dir)), 0.01f);

    float currentDepth = dist;

    const uint shadowTexArrayIndex = MIPCOUNT * lightIndex + mipLevel;
    float closestDepth = gVirtualShadowMaps[NonUniformResourceIndex(shadowTexArrayIndex)][shadowMapCoord];


    if(closestDepth > 0.0f)
    {
        if(currentDepth - bias > closestDepth){
            shadowFactor = 0.0f;
        }
    }
    else
    {
        // create shadow depth ray from light pos to fragment world pos
        // Ray(origin,  dir,  tMin = 0.f, float tMax = FLT_MAX)
        float3 origin = lightPosW;
        float3 dir = pixel_centered_dir;
        Ray ray = Ray(origin, dir, 1.0e-4);

        SceneRayQuery<0> sceneRayQuery;
        HitInfo hit;
        float hitT;
        float closestDepth = 0.f;
        uint flags = RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES;

        if (sceneRayQuery.traceRay(ray, hit, hitT, flags, 0xff))
        {
            // hitT == depth from light
            closestDepth = hitT;
        }
        else
        {
            closestDepth = float(1e+38f);
        }

        // save to texture
        gVirtualShadowMaps[NonUniformResourceIndex(shadowTexArrayIndex)][shadowMapCoord] = closestDepth;

        if(currentDepth - bias > closestDepth){
            shadowFactor = 0.0f;
        }
    }

    out_color *= shadowFactor;

    // assumes no packed mips
    uint feedbackTexArrayIndex = lightIndex * MIPCOUNT + mipLevel;
    gFeedbackMaps[NonUniformResourceIndex(feedbackTexArrayIndex)][shadowMapCoord] = 0xFF;


    gDebug[pixelCoord] = float4(gMipColor[mipLevel]);

    return out_color;
}
