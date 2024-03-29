
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

RWTexture2D<float4> gOutput;

RWTexture2D<float> gWriteTestTextures[(uint)MIPCOUNT];

// safe absolute fraction abs(n/d)
float sAbsFrac(float n, float d)
{
    float eps = 0.0000001;

    return (abs(n) + eps) / (abs(d) + eps);
}

// dx, dy in pixel coordinates
float calcLod(float2 dX, float2 dY)
{
    // from: https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm#LODCalculation
    
    float A = (dX.y*dX.y) + (dY.y*dY.y);
    float B = -2.0 * (dX.x * dX.y + dY.x * dY.y);
    float C = (dX.x*dX.x) + (dY.x*dY.x);
    float F = (dX.x * dY.y - dY.x * dX.y) * (dX.x * dY.y - dY.x * dX.y);

    float p = A - C;
    float q = A + C;
    float t = sqrt((p*p) + (B*B));

    float lengthX = sqrt(sAbsFrac(F * (t+p), t * (q+t)) + sAbsFrac(F * (t-p), t * (q+t)));
    float lengthY = sqrt(sAbsFrac(F * (t-p), t * (q-t)) + sAbsFrac(F * (t+p), t * (q-t)));
    
    return log2(max(lengthX,lengthY));
}

uint2 getMipDimensions(uint mipLevel){
    uint2 resolution = uint2(0,0);

    switch(mipLevel){
        case 0:
            resolution = uint2(4096,4096);
            break;
        case 1:
            resolution = uint2(2048,2048);
            break;
        case 2:
            resolution = uint2(1024,1024);
            break;
        case 3:
            resolution = uint2(512,512);
            break;
        case 4:
            resolution = uint2(256,256);
            break;
        case 5:
            resolution = uint2(128,128);
            break;
    }
    return resolution;
}


// payload for primary ray
struct PrimaryRayPayload {
    RayDiff rayDiff;
};

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
    gOutput[DispatchRaysIndex().xy] = float4( envColor, 1.0f );
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

    uint2 resolution;
    resolution = getMipDimensions(0);
    float3 hitPointDx1 = shadeData.posW  + newRayDiff.getdOdx() / 2; float3 hitPointDx2 = shadeData.posW  - newRayDiff.getdOdx() / 2; 
    float3 hitPointDy1 = shadeData.posW  + newRayDiff.getdOdy() / 2; float3 hitPointDy2 = shadeData.posW  - newRayDiff.getdOdy() / 2; 
    LightData lightData =  gScene.getLight(0);
    AnalyticLightSample ls;
    samplePointLight(shadeData.posW, lightData, ls);
    float3 toLight = ls.dir;
    float3 toHitPos = -toLight;
    float3 lightIntensity = ls.Li;
    float3 lightPos = lightData.posW; 
    float2 shadowCoordsDx1 = world_to_latlong_map(normalize(hitPointDx1 - lightPos)) * resolution;
    float2 shadowCoordsDx2 = world_to_latlong_map(normalize(hitPointDx2 - lightPos)) * resolution;
    float2 shadowCoordsDy1 = world_to_latlong_map(normalize(hitPointDy1 - lightPos)) * resolution;
    float2 shadowCoordsDy2 = world_to_latlong_map(normalize(hitPointDy2 - lightPos)) * resolution;
    float2 ShadowCoordDx = abs(shadowCoordsDx1 - shadowCoordsDx2);
    float2 ShadowCoordDy = abs(shadowCoordsDy1 - shadowCoordsDy2);
    float mipLevel = clamp( calcLod(ShadowCoordDx, ShadowCoordDy) ,0 ,MIPCOUNT-1);
    resolution = getMipDimensions(int(mipLevel));
    float2 shadowMapCoord = world_to_latlong_map(toHitPos) * resolution;
    float3 pixel_centered_dir = latlong_map_to_world( (floor(shadowMapCoord)+float2(0.5f)) / resolution);

    // shoot depth ray
    uint mipIndex = (uint)mipLevel;

    float shadowFactor = 1.0f;
    float closestDepth = gWriteTestTextures[NonUniformResourceIndex(mipIndex)][shadowMapCoord];
    float currentDepth =  ls.distance;

    float bias = max(0.05f * (1.0 - dot(shadeData.N, pixel_centered_dir)), 0.01);
    if(closestDepth >0){
        
        if(currentDepth - bias > closestDepth){
            shadowFactor = 0.0f;
        }

    }


    else{
        closestDepth = depthRay(lightPos, pixel_centered_dir, 0, 1e+38f);
        gWriteTestTextures[NonUniformResourceIndex(mipIndex)][shadowMapCoord] = closestDepth;

        if(currentDepth - bias > closestDepth){
            shadowFactor = 0.0f;
        }
    }
    

    float NdotL = saturate(dot(shadeData.N, toLight));
    IBSDF bsdf = gScene.materials.getBSDF(shadeData, lod);
    BSDFProperties bsdfProperties = bsdf.getProperties(shadeData);
    float shininess = 10.0;
    float3 outColor = shadowFactor * (bsdfProperties.diffuseReflectionAlbedo + (pow(NdotL, shininess) * bsdfProperties.specularReflectionAlbedo)) * M_1_PI * lightIntensity;

      
    // TEST WRITE just test mip
    


    gOutput[launchIndex] = float4(outColor, shadeData.opacity);
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
