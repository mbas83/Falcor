import Scene.RaytracingInline;
import Utils.Math.Ray;
import Utils.Math.MathHelpers;

// Defines for Shadow Map Access

// used for definitions of shadow textures:
SHADOW_TEXTURES
// used for definition of function: void writeToMip'lightIndex'(int mipLevel, uint2 coords, float value)
WRITE_TO_MIP_FUNC
// used for definition of function: float readFromMip'lightIndex'(int mipLevel, uint2 coords)
READ_FROM_MIP_FUNC
// used for definition of function: uint2 getMipDimensions(int mipLevel) 
GET_MIP_DIMENSIONS_FUNC

// Feedback Map: gFeedbackMip'lightIndex'_'mipLevel';
FEEDBACK_TEXTURES
// function to write feedback: void writeFeedback'lightIndex'(int mipLevel, uint2 coordinate)
WRITE_FEEDBACK_FUNC


cbuffer PerFrameCB
{
    float4x4 gViewMat;
    float4x4 gProjMat;
    float    gCameraNearPlaneDepth;
    float3   gCameraPosW;
    float3   gCameraTargetW;
    float gDepthBias;
};

struct VsIn{
    float3 posW : POSITION;
    float radius : PSIZE;
    float3 color : COLOR;
};

struct VsOut{
    float4 posH : SV_POSITION;

    float3 posW : POSITION;
    float radius : PSIZE;
    float3 color : COLOR;
};


VsOut vsMain(VsIn vIn)
{
    VsOut vsOut;

    // pass through
    vsOut.posW = vIn.posW;
    vsOut.radius = vIn.radius;
    vsOut.color = vIn.color;

    
    vsOut.posH = mul(float4(vsOut.posW,1.f), gViewMat);
    vsOut.posH = mul(vsOut.posH, gProjMat);
    
    return vsOut;
}


struct GsOut
{
    float4 posH : SV_POSITION;

    float3 posW : POSITION;
    float radius : PSIZE;
    float3 color : COLOR;
};

[maxvertexcount(4)]
void gsMain(point VsOut input[1], inout TriangleStream<GsOut> outStream)
{
    GsOut gsOut;
    gsOut.posW = input[0].posW;
    gsOut.radius = input[0].radius;
    gsOut.color = input[0].color;

    float radius = input[0].radius;

    //billboard tangent space
    float3 n = normalize(gCameraPosW - input[0].posW);
    float3 t = normalize(cross(float3(0,1,0), n));
    float3 b = normalize(cross(n,t));


    //gScene.camera
    float3 billboard_behind = gsOut.posW - (n * radius);

    float4 center_pos = mul(float4(billboard_behind,1.f), gViewMat);
        

    // vertex 0
    float4 newPos = center_pos - radius*float4(t,0) - radius*float4(b,0) ;
    gsOut.posH = mul(newPos,gProjMat);
    outStream.Append(gsOut);

    // vertex 1
    newPos = center_pos + radius*float4(t,0) - radius*float4(b,0);
    gsOut.posH = mul(newPos,gProjMat);
    outStream.Append(gsOut);

    // vertex 2
    newPos = center_pos - radius*float4(t,0) + radius*float4(b,0);
    gsOut.posH = mul(newPos,gProjMat);
    outStream.Append(gsOut);

    // vertex 3
    newPos = center_pos + radius*float4(t,0) + radius*float4(b,0);
    gsOut.posH = mul(newPos,gProjMat);
    outStream.Append(gsOut);

    outStream.RestartStrip();

}


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

    float3 diffuseColor = gDiffuse[pixelCoord].rgb;
    float3 specularColor = gSpecular[pixelCoord].rgb;
    float3 worldPosW = gWorldPos[pixelCoord].xyz;
    float3 normal = normalize(gWorldNormal[pixelCoord].xyz);
    float3 lightPosW = gsOut.posW;

    float3 direction = lightPosW - worldPosW;
    float dist = length(direction);
   
    if (dist >= gsOut.radius) discard; // skip lights that are too far away
    // normalize direction
    direction = direction / dist;
    // physically incorrect attenuation (but this gives the point light an actual radius)
    float attenuation = pow(1.0 - pow(dist / gsOut.radius, 2.0), 2.0);

    // use correct attenuation
    attenuation = 1.f / (dist*dist);
    float3 lightColor = max(dot(direction, normal), 0.0) * gsOut.color * attenuation;

    out_color = float4(lightColor * (diffuseColor + specularColor), 1);

    // calc shadow

    // calc mipLevel
    //const float4 texGrads = float4(ddx(sd.uv), ddy(sd.uv));
    // use computeDdxPosW from gbuffer, or rt gems ch. 20?

    const float2 ddX = gTexGrad[pixelCoord].xy;
    const float2 ddY = gTexGrad[pixelCoord].zw;

    uint2 resolution;
    //resolution = getMipDimensions(0);

    // calc the two positions at hit point
    //float3 hitPointDx1 = shadeData.posW  + newRayDiff.getdOdx() / 2;
    //float3 hitPointDx2 = shadeData.posW  - newRayDiff.getdOdx() / 2;
    // for dy
    //float3 hitPointDy1 = shadeData.posW  + newRayDiff.getdOdy() / 2;
    //float3 hitPointDy2 = shadeData.posW  - newRayDiff.getdOdy() / 2;


    //float2 shadowCoordsDx1 = world_to_latlong_map(normalize(hitPointDx1 - lightPos)) * resolution;
    //float2 shadowCoordsDx2 = world_to_latlong_map(normalize(hitPointDx2 - lightPos)) * resolution;
    // for dy
    //float2 shadowCoordsDy1 = world_to_latlong_map(normalize(hitPointDy1 - lightPos)) * resolution;
    //float2 shadowCoordsDy2 = world_to_latlong_map(normalize(hitPointDy2 - lightPos)) * resolution;

    //float2 ShadowCoordDx = abs(shadowCoordsDx1 - shadowCoordsDx2);
    //float2 ShadowCoordDy = abs(shadowCoordsDy1 - shadowCoordsDy2);

    //float mipLevel = clamp( calcLod(ShadowCoordDx, ShadowCoordDy) ,0 ,MIPCOUNT);

    // Update Resolution to chosen mipmap level
    //resolution = getMipDimensions(int(mipLevel));
    //float2 shadowMapCoord = world_to_latlong_map(toHitPos) * resolution;
    // ray should always go through center of the shadow map pixel
    //float3 pixel_centered_dir = latlong_map_to_world( (floor(shadowMapCoord)+float2(0.5f)) / resolution);

    //float bias = max(gDepthBias * (1.0 - dot(shadeData.N, pixel_centered_dir)), 0.01);


    // create shadow depth ray from light pos to fragment world pos
    // Ray(origin,  dir,  tMin = 0.f, float tMax = FLT_MAX)
    float3 origin = lightPosW;
    float3 dir = normalize(worldPosW - lightPosW);
    Ray ray = Ray(origin, dir, 1.0e-4);


    SceneRayQuery<0> sceneRayQuery;
    HitInfo hit;
    float hitT;
    float closestDepth = 0.f;
    uint flags = RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES;

    float debug = 0.f;

    if (sceneRayQuery.traceRay(ray, hit, hitT, flags, 0xff))
    {
        debug = 1.f;
        // hitT == depth from light
        closestDepth = hitT;
    }
    else
    {
        closestDepth = float(1e+38f);
    }

    float shadowFactor = 1.0f;
    float bias = max(gDepthBias * (1.0f - dot(normal, dir)), 0.01f);

    float currentDepth = dist;
    if(currentDepth - bias > closestDepth){
        shadowFactor = 0.0f;
    }

    out_color *= shadowFactor;


    //debug = closestDepth;
    gDebug[pixelCoord] = float4(debug, 0.f, 0.f, 1.f);

    //writeFeedback(0, 0, uint2(0,0));

    return out_color;
}
