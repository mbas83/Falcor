
import Scene.RaytracingInline;
import Utils.Math.Ray;

cbuffer PerFrameCB
{
    float4x4 gViewMat;
    float4x4 gProjMat;
    float    gCameraNearPlaneDepth;
    float3   gCameraPosW;
    float3   gCameraTargetW;
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
        

    float4 center_pos = mul(float4(gsOut.posW,1.f), gViewMat);
    //gScene.camera

    float3 dir = gCameraTargetW - gCameraPosW;
    float depthSign = sign(gCameraTargetW.z);
    // depends on viewdirection -z or +z
    float billboard_depth = -1 * radius;

    //billboard tangent space
    float3 n = normalize(gCameraPosW - input[0].posW);
    float3 t = normalize(cross(float3(0,1,0), n));
    float3 b = normalize(cross(n,t));

    // vertex 0
    float4 newPos = center_pos + float4(-radius,-radius,billboard_depth,0);

    newPos = center_pos - radius*float4(t,0) - radius*float4(b,0);

    gsOut.posH = mul(newPos,gProjMat);
    outStream.Append(gsOut);

    // vertex 1
    newPos = center_pos + float4(radius,-radius,billboard_depth,0);

    newPos = center_pos + radius*float4(t,0) - radius*float4(b,0);

    gsOut.posH = mul(newPos,gProjMat);
    outStream.Append(gsOut);

    // vertex 2
    newPos = center_pos + float4(-radius,radius,billboard_depth,0);

    newPos = center_pos - radius*float4(t,0) + radius*float4(b,0);

    gsOut.posH = mul(newPos,gProjMat);
    outStream.Append(gsOut);

    // vertex 3
    newPos = center_pos + float4(radius,radius,billboard_depth,0);

    newPos = center_pos + radius*float4(t,0) + radius*float4(b,0);

    gsOut.posH = mul(newPos,gProjMat);
    outStream.Append(gsOut);

    outStream.RestartStrip();

}



Texture2D<float4> gWorldPos;
Texture2D<float4> gWorldNormal;
Texture2D<float4> gDiffuse;
Texture2D<float4> gSpecular;
Texture2D<float4> gTexGrad;

float4 psMain(GsOut gsOut) : SV_TARGET0
{
    float4 out_color;
     
    int2 pixelCoord = gsOut.posH.xy;

    float3 diffuseColor = gDiffuse[pixelCoord].rgb;
    float3 specularColor = gSpecular[pixelCoord].rgb;
    float3 worldPos = gWorldPos[pixelCoord].xyz;
    float3 normal = normalize(gWorldNormal[pixelCoord].xyz);

    float3 direction = gsOut.posW - worldPos;
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
    //const float4 texGrads = float4(ddx(sd.uv), ddy(sd.uv));
    const float2 ddx = gTexGrad[pixelCoord].xy;
    const float2 ddy = gTexGrad[pixelCoord].zw;

    // create shadow depth ray from light pos to fragment world pos
    //const Ray ray = gGBufferRT.generateRay(pixel);

    SceneRayQuery<0> sceneRayQuery;
    HitInfo hit;
    float hitT;
    float depth;
    if (sceneRayQuery.traceRay(ray, hit, hitT, GBufferRT::kRayFlags, 0xff))
    {
        //
        depth = hitT;
    }
    else
    {
        depth = float(1e+38f);
    }



    return out_color;
}
