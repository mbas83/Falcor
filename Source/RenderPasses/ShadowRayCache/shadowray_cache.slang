#include "Scene/VertexAttrib.slangh"

#include "Utils/Math/MathConstants.slangh"
import Scene.HitInfo;
import Scene.Raytracing;
import Scene.SceneTypes;
import Scene.Shading;
import Scene.Lights.EnvMap;
import Utils.Math.MathHelpers;
// Ray Diffs
import Rendering.Materials.TexLODTypes;
import Rendering.Materials.TexLODHelpers;
import Rendering.Lights.LightHelpers;

RWTexture2D<float4> gOutputDiffuse, gOutputLod, gOutputVis;

// (tiled) shadow maps that save depth from light source
// RWTexture2D<float> gShadowMap0_0;
// RWTexture2D<float> gShadowMap0_1;
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


// Mip Color Texture
Texture1D<float4> gMipColor;

// depth bias
cbuffer PerFrameCB{
    float gDepthBias;
}

// payload for primary ray
struct PrimaryRayPayload {
    RayDiff rayDiff;
};

// payload for radus search
struct RadiusSearchPayload {

};


#include "Helper.slang" // contains helper function for LOD calculation

// search all lights in radius and add their light contribution
void radiusSearch(inout float3 color, RayDesc ray, RayDiff raydiff, ShadingData shadeData, ITextureSampler hitlod){
    
    RadiusSearchPayload payload;
    payload.rayDiff = raydiff;
    payload.shadeData = shadeData;
    payload.color = float3(0.f);
    payload.hitLod = hitlod;

    // trace single ray and handle everything in any-hit shaders
    TraceRay(gScene.rtAccel, RAY_FLAG_SKIP_TRIANGLES | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, 0xff /* instanceInclusionMask */, 0 /* hitIdx */, rayTypeCount, 0 /* missIdx */, ray, payload);

    color = payload.color;
}


// Generate Rays
[shader("raygeneration")]
void RayGen()
{
    uint2 launchIndex = DispatchRaysIndex().xy;
    uint2 launchDim = DispatchRaysDimensions().xy;
    
	// Convert our ray index into a ray direction in world space.  Grab pixel location, convert to
	//     normalized device coordiates, then use built-in Falcor variables containing our camera matrix
	//     to get our world-space ray direction for this pixel.
	float2 pixelCenter = (launchIndex + float2(0.5f, 0.5f)) / launchDim; 
	float2 ndc = float2(2, -2) * pixelCenter + float2(-1, 1);                    
    float3 nonNormalizedRayDir = ndc.x * gScene.camera.data.cameraU + ndc.y * gScene.camera.data.cameraV + gScene.camera.data.cameraW;  

	// Initialize a ray structure for our ray tracer
	RayDesc ray;
	ray.Origin    = gScene.camera.data.posW;      // Start our ray at the world-space camera position
	ray.Direction = normalize(nonNormalizedRayDir); // Our ray direction; normalizing this is often wise
	ray.TMin      = 0.0f;              // Start at 0.0; for camera, no danger of self-intersection
	ray.TMax      = 1e+38f;            // Maximum distance to look for a ray hit


    float3 dDdx, dDdy;
    // calculate intial raydiff (eq. 8)
    computeRayDirectionDifferentials(nonNormalizedRayDir, gScene.camera.data.cameraU, gScene.camera.data.cameraV, launchDim, dDdx, dDdy);

    RayDiff rd = RayDiff(float3(0.f), float3(0.f), dDdx, dDdy);

    // Initialize our ray payload (a per-ray, user-definable structure); 
	PrimaryRayPayload rayData =  {rd}; 


	// Trace our ray
	TraceRay(gScene.rtAccel,                        // A Falcor built-in containing the raytracing acceleration structure
		RAY_FLAG_CULL_BACK_FACING_TRIANGLES,  // Ray flags.  (Here, we will skip hits with back-facing triangles)
		0xFF,                                 // Instance inclusion mask.  0xFF => no instances discarded from this mask
		0,                                    // Hit group to index (i.e., when intersecting, call hit shader #0)
		rayTypeCount,                      // Number of hit groups ('rayTypeCount' is built-in from Falcor with the right number)
		0,                                    // Miss program index (i.e., when missing, call miss shader #0)
		ray,                                  // Data structure describing the ray to trace
		rayData);                             // Our user-defined ray payload structure to store intermediate results
}   


[shader("miss")]
void PrimaryMiss(inout PrimaryRayPayload rayData: SV_RayPayload)
{
    float lod = computeEnvironmentMapLOD(rayData.rayDiff.getdDdx(), rayData.rayDiff.getdDdy(), gScene.envMap.envMap);

    float3 envColor = gScene.envMap.eval(WorldRayDirection(), lod);

    gOutputDiffuse[DispatchRaysIndex().xy] = float4( envColor, 1.0f );
    gOutputLod[DispatchRaysIndex().xy] = float4( float3(0.0f), 1.0f );
    gOutputVis[DispatchRaysIndex().xy] = float4( float3(0.0f), 1.0f );
}

[shader("anyhit")]
void PrimaryAnyHit(inout PrimaryRayPayload : SV_RayPayload, BuiltInTriangleIntersectionAttributes attribs : SV_IntersectionAttributes)
{
    GeometryInstanceID instanceID = getGeometryInstanceID();
    VertexData v = getVertexData(instanceID, PrimitiveIndex(), attribs);
    const uint materialID = gScene.getMaterialID(instanceID);
    if (gScene.materials.alphaTest(v, materialID, 0.f)) IgnoreHit();
}

[shader("closesthit")]
void PrimaryClosestHit(inout PrimaryRayPayload rayData: SV_RayPayload, BuiltInTriangleIntersectionAttributes attribs : SV_IntersectionAttributes)
{
    // Which pixel spawned our ray?
	uint2 launchIndex = DispatchRaysIndex().xy;
    uint2 launchDim = DispatchRaysDimensions().xy;

    const float3 rayDir = WorldRayDirection();
    const float3 rayOrg = WorldRayOrigin();
    float hitT = RayTCurrent();

    const GeometryInstanceID instanceID = getGeometryInstanceID();

    // Get hit data
    VertexData v = getVertexData(instanceID, PrimitiveIndex(), attribs);
    const uint materialID = gScene.getMaterialID(instanceID);

    //Ray Diff
    float2 dUVdx, dUVdy;    // Ray differential variables for the texture lookup.
    float2 dBarydx, dBarydy;
    float3 geometricNormal = gScene.getFaceNormalW(instanceID, PrimitiveIndex());
    RayDiff newRayDiff = rayData.rayDiff.propagate(rayOrg, rayDir, hitT, geometricNormal);
          
    float3 barycentrics;
    StaticVertexData triangleVertices[3];
    v = getVertexData(instanceID, PrimitiveIndex(), attribs, barycentrics, triangleVertices);


    float3 unnormalizedN;   // Non-normalized interpolated normal for ray differential scatter.
    float3 normals[3];      // Non-normalized normals for ray differential scatter.

    prepareRayDiffAtHitPoint(v, triangleVertices, barycentrics, rayDir, gScene.getWorldMatrix(instanceID), gScene.getInverseTransposeWorldMatrix(instanceID),
            newRayDiff, unnormalizedN, normals, dBarydx, dBarydy, dUVdx, dUVdy);  

    
    ITextureSampler lod = ExplicitRayDiffsIsotropicTextureSampler(dUVdx, dUVdy);
    ShadingData shadeData = gScene.materials.prepareShadingData(v, materialID, -rayDir, lod);


    // sum light contribution   
    float3 outColor = float3(0.f);

    #if USE_RADIUS_SEARCH

    // radius search of lights from hitpoint of surface
    float3 outColor;
    const float eps = 1.0e-4;
    RayDesc ray;
    ray.Origin    = shadeData.posW;      // Start our ray at the hitpoint
	ray.Direction = normalize(float3(1.0f)); // any direction
	ray.TMin      = 0.0f;              
	ray.TMax      = eps;
    radiusSearch(outColor,ray,newRayDiff,shadeData, lod);

    #else // iterate over all lights

    uint2 resolution;
    resolution = getMipDimensions(0);

    // calc the two positions at hit point
    float3 hitPointDx1 = shadeData.posW  + newRayDiff.getdOdx() / 2;
    float3 hitPointDx2 = shadeData.posW  - newRayDiff.getdOdx() / 2;
    // for dy
    float3 hitPointDy1 = shadeData.posW  + newRayDiff.getdOdy() / 2;
    float3 hitPointDy2 = shadeData.posW  - newRayDiff.getdOdy() / 2;

    float3 outColor= float3(0.f);

    for(int lightIndex = 0; lightIndex < gScene.getLightCount(); lightIndex++){

    //Get Light data
    LightData lightData =  gScene.getLight(lightIndex);
    AnalyticLightSample ls;
    samplePointLight(shadeData.posW, lightData, ls);
    float3 toLight = ls.dir;
    float3 toHitPos = -toLight;
    float3 lightIntensity = ls.Li;

     // direction from light to hitpos
    float3 lightPos = lightData.posW;
    float2 shadowCoordsDx1 = world_to_latlong_map(normalize(hitPointDx1 - lightPos)) * resolution;
    float2 shadowCoordsDx2 = world_to_latlong_map(normalize(hitPointDx2 - lightPos)) * resolution;
    // for dy
    float2 shadowCoordsDy1 = world_to_latlong_map(normalize(hitPointDy1 - lightPos)) * resolution;
    float2 shadowCoordsDy2 = world_to_latlong_map(normalize(hitPointDy2 - lightPos)) * resolution;

    float2 ShadowCoordDx = abs(shadowCoordsDx1 - shadowCoordsDx2);
    float2 ShadowCoordDy = abs(shadowCoordsDy1 - shadowCoordsDy2);

    float mipLevel = clamp( calcLod(ShadowCoordDx, ShadowCoordDy) ,0 ,MIPCOUNT - 1);

    // Update Resolution to chosen mipmap level
    resolution = getMipDimensions(int(mipLevel));
    float2 shadowMapCoord = world_to_latlong_map(toHitPos) * resolution;
    // ray should always go through center of the shadow map pixel
    float3 pixel_centered_dir = latlong_map_to_world( (floor(shadowMapCoord)+float2(0.5f)) / resolution);

    float bias = max(gDepthBias * (1.0 - dot(shadeData.N, pixel_centered_dir)), 0.01);

    float shadowFactor = 1.0f;
    float closestDepth = readFromMip(lightIndex, int(mipLevel), shadowMapCoord);

    if(closestDepth > 0.0f)
    {
        float currentDepth =  ls.distance;
        if(currentDepth - bias > closestDepth){
            shadowFactor = 0.0f;
        }
    }
    else
    {
        // Shoot ray from light to hitpos direction
        float closestDepth = depthRay(lightData.posW, pixel_centered_dir, 0, 1e+38f);

        // save to texture
        writeDepth(lightIndex, int(mipLevel), shadowMapCoord, closestDepth);


        float currentDepth =  ls.distance;
        if(currentDepth - bias > closestDepth){
            shadowFactor = 0.0f;
        }
    }

    // Compute our lambertian term (L dot N)
    float NdotL = saturate(dot(shadeData.N, toLight));

    // Create BSDF instance and query its properties.
    let bsdf = gScene.materials.getBSDF(shadeData, lod);
    let bsdfProperties = bsdf.getProperties(shadeData);

    // Phong shading
    float shininess = 10.0;
    outColor += shadowFactor * (bsdfProperties.diffuseReflectionAlbedo + (pow(NdotL, shininess) * bsdfProperties.specularReflectionAlbedo)) * M_1_PI * lightIntensity;

    writeFeedback(lightIndex, mipLevel, shadowMapCoord)

    #endif

    // Reflect RayDiff (not needed, reflection not traced)
    reflectRayDifferential(newRayDiff, rayDir, unnormalizedN, v.normalW, dBarydx, dBarydy, normals);
    rayData.rayDiff = newRayDiff;
        

	// Write to output textures
	gOutputDiffuse[launchIndex] = float4(outColor, shadeData.opacity);
    gOutputLod[DispatchRaysIndex().xy] = float4( gMipColor[int(mipLevel)] );
    gOutputVis[DispatchRaysIndex().xy] = float4( float3(shadowFactor), 1.0f );
            
}

//***************** Hit Group 1: Depth Ray *********************************************************************************

struct DepthRayPayload
{
	float depth;  // Saves position of hit in world space
};

// Trace shadow from light to hitpos and return depth of first hit
float depthRay(float3 origin, float3 direction, float minT, float maxT)
{
	// Setup our shadow ray
	RayDesc ray;
	ray.Origin = origin;        // Where does it start?
	ray.Direction = direction;  // What direction do we shoot it?
	ray.TMin = minT;            // The closest distance we'll count as a hit
	ray.TMax = maxT;            // The farthest distance we'll count as a hit

	DepthRayPayload payload = { float(1e+38f) };

	// Query if anything is between the current point and the light
	TraceRay(gScene.rtAccel,
		RAY_FLAG_NONE,
		0xFF, 1, rayTypeCount, 1, ray, payload);


	// Return depth from light position
	return payload.depth;
}


[shader("miss")]
void ShadowMiss(inout DepthRayPayload rayData)
{
}

[shader("anyhit")]
void ShadowAnyHit(inout DepthRayPayload : SV_RayPayload, BuiltInTriangleIntersectionAttributes attribs : SV_IntersectionAttributes)
{
    GeometryInstanceID instanceID = getGeometryInstanceID();
    VertexData v = getVertexData(instanceID, PrimitiveIndex(), attribs);
    const uint materialID = gScene.getMaterialID(instanceID);
    if (gScene.materials.alphaTest(v, materialID, 0.f)) IgnoreHit();
}   


[shader("closesthit")]
void ShadowClosestHit(inout DepthRayPayload rayData : SV_RayPayload, BuiltInTriangleIntersectionAttributes attribs : SV_IntersectionAttributes)
{
    rayData.depth = RayTCurrent();
}


//***************** Hit Group 0 Custom Primitives: Radius Search shaders *********************************************************************************
struct BoxIntersectionAttributes
{
    int aabbOffset; // is the light index
};

[shader("intersection")]
void boxIntersect()
{
    const uint aabbOffset = gScene.customPrimitives[GeometryIndex()].aabbOffset;
    const AABB bbox = gScene.proceduralPrimitiveAABBs[aabbOffset];
    const float2 dim = bbox.extent().xy; // w = dim.x, h = dim.y
    const float3 C = bbox.center();

    const float radius  = 0.5 * dim.x;
    const float distToLight = distance(WorldRayOrigin(), C);

    if(distToLight > radius){   
        return;
    }

    const float t = dot(WorldRayDirection(), C - WorldRayOrigin());

    BoxIntersectionAttributes attribs;
    attribs.aabbOffset = aabbOffset;
    ReportHit(t, 0 , attribs);
}


[shader("anyhit")]
void boxAnyHit(inout RadiusSearchPayload rayData : SV_RayPayload, BoxIntersectionAttributes attribs : SV_IntersectionAttributes)
{
    
}


//not used
[shader("closesthit")]
void boxClosestHit(){}
