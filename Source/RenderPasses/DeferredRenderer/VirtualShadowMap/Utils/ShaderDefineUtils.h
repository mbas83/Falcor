#pragma once

#include <string>


// light i -> gShadowMap'i'_'mipindex'
static std::string getMipViewDefineString(const uint32_t lightCount, const uint32_t mipCount)
{
    std::string s;

    for (uint32_t lightIndex = 0; lightIndex < lightCount; ++lightIndex)
    {
        for (uint32_t mipIndex = 0; mipIndex < mipCount; ++mipIndex)
        {
            s += "RWTexture2D<float> gShadowMap" + std::to_string(lightIndex) + "_" + std::to_string(mipIndex) + ";";
        }
    }

    return s;
}


static std::string mipDimensionsFunctionString(const uint32_t mipCount)
{
    std::string s("uint2 getMipDimensions(int mipLevel){uint2 resolution; switch(lightIndex){");

    for (uint32_t i = 0; i < mipCount; ++i)
    {
        // switch case for every mip level view
        s += "case " + std::to_string(i) + ": gShadowMap0_" + std::to_string(i) + +".GetDimensions(resolution.x,resolution.y); break;";
    }

    s += "default: gShadowMap0_0.GetDimensions(resolution.x,resolution.y); break; } return resolution; }";

    return s;
}


// light i -> writeDepth(i,mipLevel,coord)
static std::string getWriteToMipFunctionString(const uint32_t lightCount, const uint32_t mipCount)
{
    std::string s("void writeDepth(uint lightIndex, int mipLevel, uint2 coords, float value){switch(lightIndex){");

    for (uint32_t lightIndex = 0; lightIndex < lightCount; ++lightIndex)
    {
        s += "case " + std::to_string(lightIndex) + ": switch(mipLevel){";

        for (uint32_t mipIndex = 0; mipIndex < mipCount; ++mipIndex)
        {
            // switch case for every mip level view
            s += "case " + std::to_string(mipIndex) + ": gShadowMap" + std::to_string(lightIndex) + "_" + std::to_string(mipIndex) + "[coords] = value; break;";
        }

        s += "default: break; }";

    }

    s += "default: break; } }";

    return s;
}


// light i -> readDepth(i,mipLevel,coords)
static std::string getReadFromMipFunctionString(const uint32_t lightCount, const uint32_t mipCount)
{
    std::string s("float readDepth" + std::to_string(lightCount) + "(uint lightIndex, int mipLevel, uint2 coords){switch(lightIndex){");

    for (uint32_t lightIndex = 0; lightIndex < lightCount; ++lightIndex)
    {
        s += "case " + std::to_string(lightIndex) + ": switch(mipLevel){";

        for (int mipIndex = 0; mipIndex < mipCount; ++mipIndex)
        {
            // switch case for every mip level view
            s += "case " + std::to_string(mipIndex) + ": return gShadowMap" + std::to_string(lightIndex) + "_" + std::to_string(mipIndex) + "[coords];";
        }

        s += "default: return 0.f; }";

    }

    s += "default: return 0.f; } return 0.f; }";

    return s;
}


// light i -> gFeedbackMip'i'_'mipIndex'
static std::string getFeedbackViewDefineString(const uint32_t lightCount, const uint32_t standardMipCount)
{
    // only the normal (not packed) mips are used for feedback
    // RWTexture2D<uint> gFeedbackMip(i);
    std::string s;

    for (uint32_t lightIndex = 0; lightIndex < lightCount; ++lightIndex)
    {
        for (uint32_t mipIndex = 0; mipIndex < standardMipCount; ++mipIndex)
        {
            s += "RWTexture2D<uint> gFeedbackMip" + std::to_string(lightIndex) + "_" + std::to_string(mipIndex) + ";";
        }
    }

    return s;
}

static std::string getWriteFeedbackString(const uint32_t lightCount, const uint32_t standardMipCount)
{


    /*  Coordinates are in size of the chosen mipLevel, so just divide by Tile size
     *
     *   void writeFeedback(uint lightIndex, int mipLevel, uint2 coordinate)
     *   {
     *      uint2 feedback_coord = uint2(coordinate / uint2(TILE_WIDTH, TILE_HEIGHT));
     *
     *
     *      switch(lightIndex){
     *          case 0:
     *              switch(mipLevel){
     *                  case 0:
     *                      gFeedbackMip0_0[feedback_coord] = 0xFF;
     *                      break;
     *                  case 1:
     *                      gFeedbackMip0_1[feedback_coord] = 0xFF;
     *                      break;
     *              }
     *          case 1:
     *              ....
     *
     *      }
     *   }
     */

     // only the normal (not packed) mips are used for feedback
    std::string s("void writeFeedback(uint lightIndex, int mipLevel, uint2 coordinate){ uint2 feedback_coord = uint2(coordinate / uint2(TILE_WIDTH, TILE_HEIGHT)); switch(lightIndex){");

    for (uint32_t lightIndex = 0; lightIndex < lightCount; ++lightIndex)
    {
        s += "case " + std::to_string(lightIndex) + ": switch(mipLevel){";

        for (int mipLevel = 0; mipLevel < standardMipCount; ++mipLevel)
        {
            // switch case for every mip level view
            s += "case " + std::to_string(mipLevel) + ": gFeedbackMip" + std::to_string(lightIndex) + "_" + std::to_string(mipLevel) + "[feedback_coord] = 0xFF;break;";
        }

        s += "default: break; }";
    }

    s += "}}";

    return s;
}

static std::string getLightCalculationString(const uint32_t numLights)
{
    /*
    //Get resolution of mip 0 for LOD calculation
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

    float mipLevel = clamp( calcLod(ShadowCoordDx, ShadowCoordDy) ,0 ,MIPCOUNT);

    // Update Resolution to chosen mipmap level
    resolution = getMipDimensions(int(mipLevel));
    float2 shadowMapCoord = world_to_latlong_map(toHitPos) * resolution;
    // ray should always go through center of the shadow map pixel
    float3 pixel_centered_dir = latlong_map_to_world( (floor(shadowMapCoord)+float2(0.5f)) / resolution);

    float bias = max(gDepthBias * (1.0 - dot(shadeData.N, pixel_centered_dir)), 0.01);

    float shadowFactor = 1.0f;
    float closestDepth = readFromMip'lightIndex'(int(mipLevel),shadowMapCoord);

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
        writeDepth'lightIndex'(int(mipLevel), shadowMapCoord, closestDepth);


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

    writeFeedback'lightIndex'(mipLevel,shadowMapCoord)

     } //end for

     */

    return "s";
}
