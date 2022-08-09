#pragma once

#include <string>


// light i -> gShadowMap'i'_'mipindex'
static std::string getMipViewDefineString(const uint lightCount, const uint mipCount)
{
    std::string s;

    for (uint lightIndex = 0; lightIndex < lightCount; ++lightIndex)
    {
        for (int mipIndex = 0; mipIndex < mipCount; ++mipIndex)
        {
            s += "RWTexture2D<float> gShadowMap" + std::to_string(lightIndex) + "_" + std::to_string(mipIndex) + ";";
        }
    }
    
    return s;
}


static std::string mipDimensionsFunctionString(const uint mipCount)
{
    std::string s("uint2 getMipDimensions(int mipLevel){uint2 resolution; switch(mipLevel){");

    for (uint i = 0; i < mipCount; ++i)
    {
        // switch case for every mip level view
        s += "case " + std::to_string(i) + ": gShadowMap0_" + std::to_string(i) + +".GetDimensions(resolution.x,resolution.y); break;";
    }

    s += "default: gShadowMap0_0.GetDimensions(resolution.x,resolution.y); break; } return resolution; }";

    return s;
}


// light i -> writeDepth'i'()
static std::string getWriteToMipFunctionString(const uint lightIndex, const uint mipCount)
{
    std::string s("void writeDepth" + std::to_string(lightIndex) + "(int mipLevel, uint2 coords, float value){switch(mipLevel){");

    for (uint i = 0; i < mipCount; ++i)
    {
        // switch case for every mip level view
        s += "case " + std::to_string(i) + ": gShadowMap" + std::to_string(lightIndex) + "_" + std::to_string(i) + "[coords] = value; break;";
    }

    s += "default: break; } }";

    return s;
}


// light i -> readDepth'i'()
static std::string getReadFromMipFunctionString(const uint lightIndex, const uint mipCount)
{
    std::string s("float readDepth" + std::to_string(lightIndex) + "(int mipLevel, uint2 coords){switch(mipLevel){");

    for (uint i = 0; i < mipCount; ++i)
    {
        // switch case for every mip level view
        s += "case " + std::to_string(i) + ": return gShadowMap" + std::to_string(lightIndex) + "_" + std::to_string(i) + "[coords];";
    }

    s += "default: return 0.f; } return 0.f; }";

    return s;
}


// light i -> gFeedbackMip'i'_'mipIndex'
static std::string getFeedbackViewDefineString(const uint lightCount, const uint standardMipCount)
{
    // only the normal (not packed) mips are used for feedback
    // RWTexture2D<uint> gFeedbackMip(i);
    std::string s;

    for (uint lightIndex = 0; lightIndex < lightCount; ++lightIndex)
    {
        for (int mipIndex = 0; mipIndex < standardMipCount; ++mipIndex)
        {
            s += "RWTexture2D<uint> gFeedbackMip" + std::to_string(lightIndex) + "_" + std::to_string(mipIndex) + ";";
        }
    }

    return s;
}


static std::string getWriteFeedbackString(const uint lightIndex, const uint standardMipCount)
{


    /*  Coordinates are in size of the chosen mipLevel, so just divide by Tile size
     *
     *   void writeFeedback'lightIndex'(int mipLevel, uint2 coordinate)
     *   {
     *      uint2 feedback_coord = uint2(coordinate / uint2(TILE_WIDTH, TILE_HEIGHT));
     *
     *
     *      switch(mipLevel){
     *          case 0:
     *              gFeedbackMip'lightIndex'_0[feedback_coord] = 0xFF;
     *              break;
     *          case 1:
     *              gFeedbackMip'lightIndex'_1[feedback_coord] = 0xFF;
     *              break;
     *
     *      }
     *   }
     */

     // only the normal (not packed) mips are used for feedback
    std::string s("void writeFeedback" + std::to_string(lightIndex) + "(int mipLevel, uint2 coordinate){ uint2 feedback_coord = uint2(coordinate / uint2(TILE_WIDTH, TILE_HEIGHT)); switch(mipLevel){");

    for (uint i = 0; i < standardMipCount; ++i)
    {
        // switch case for every mip level view
        s += "case " + std::to_string(i) + ": gFeedbackMip" + std::to_string(lightIndex) + "_" + std::to_string(i) + "[feedback_coord] = 0xFF;break;";
    }

    s += "}}";

    return s;
}

static std::string getLightCalculationString(const uint numLights)
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

    std::string s("uint2 resolution;resolution = getMipDimensions(0);"
        "float3 hitPointDx1 = shadeData.posW  + newRayDiff.getdOdx() / 2; float3 hitPointDx2 = shadeData.posW  - newRayDiff.getdOdx() / 2; \n"
        "float3 hitPointDy1 = shadeData.posW  + newRayDiff.getdOdy() / 2; float3 hitPointDy2 = shadeData.posW  - newRayDiff.getdOdy() / 2; \n"
        "LightData lightData =  gScene.getLight(" + std::to_string(0) + ");"
        "AnalyticLightSample ls;"
        "samplePointLight(shadeData.posW, lightData, ls);"
        "float3 toLight = ls.dir;"
        "float3 toHitPos = -toLight;"
        "float3 lightIntensity = ls.Li;"
        "float3 lightPos = lightData.posW; \n"
        "float2 shadowCoordsDx1 = world_to_latlong_map(normalize(hitPointDx1 - lightPos)) * resolution;"
        "float2 shadowCoordsDx2 = world_to_latlong_map(normalize(hitPointDx2 - lightPos)) * resolution;"
        "float2 shadowCoordsDy1 = world_to_latlong_map(normalize(hitPointDy1 - lightPos)) * resolution;"
        "float2 shadowCoordsDy2 = world_to_latlong_map(normalize(hitPointDy2 - lightPos)) * resolution;"
        "float2 ShadowCoordDx = abs(shadowCoordsDx1 - shadowCoordsDx2);"
        "float2 ShadowCoordDy = abs(shadowCoordsDy1 - shadowCoordsDy2);"
        "float mipLevel = clamp( calcLod(ShadowCoordDx, ShadowCoordDy) ,0 ,MIPCOUNT); \n"
        "resolution = getMipDimensions(int(mipLevel));"
        "float2 shadowMapCoord = world_to_latlong_map(toHitPos) * resolution;"
        "float3 pixel_centered_dir = latlong_map_to_world( (floor(shadowMapCoord)+float2(0.5f)) / resolution);"
        "float bias = max(gDepthBias * (1.0 - dot(shadeData.N, pixel_centered_dir)), 0.01); \n"
        "float shadowFactor = 1.0f;"
        "float closestDepth = readDepth" + std::to_string(0) + "(int(mipLevel),shadowMapCoord);\n"
        "if(closestDepth > 0.0f)"
        "{"
        "  float currentDepth =  ls.distance; "
        "  if(currentDepth - bias > closestDepth){"
        "   shadowFactor = 0.0f;"
        "  }"
        "}"
        "else"
        "{"
        "  float closestDepth = depthRay(lightData.posW, pixel_centered_dir, 0, 1e+38f);"
        "  writeDepth" + std::to_string(0) + "(int(mipLevel), shadowMapCoord, closestDepth);\n"
        "  float currentDepth =  ls.distance;"
        "  if(currentDepth - bias > closestDepth){"
        "    shadowFactor = 0.0f;"
        "  }"
        "}"
        "float NdotL = saturate(dot(shadeData.N, toLight));"
        "IBSDF bsdf = gScene.materials.getBSDF(shadeData, lod);"
        "BSDFProperties bsdfProperties = bsdf.getProperties(shadeData);"
        "float shininess = 10.0;"
        "outColor += shadowFactor * (bsdfProperties.diffuseReflectionAlbedo + (pow(NdotL, shininess) * bsdfProperties.specularReflectionAlbedo)) * M_1_PI * lightIntensity;"
        "writeFeedback" + std::to_string(0) + "(mipLevel,shadowMapCoord);");

    for (uint i = 1; i < numLights; ++i)
    {
        s += "resolution = getMipDimensions(0);"
            " hitPointDx1 = shadeData.posW  + newRayDiff.getdOdx() / 2;  hitPointDx2 = shadeData.posW  - newRayDiff.getdOdx() / 2; \n"
            " hitPointDy1 = shadeData.posW  + newRayDiff.getdOdy() / 2;  hitPointDy2 = shadeData.posW  - newRayDiff.getdOdy() / 2; \n"
            " lightData =  gScene.getLight(" + std::to_string(i) + ");"
            "samplePointLight(shadeData.posW, lightData, ls);"
            " toLight = ls.dir;"
            " toHitPos = -toLight;"
            " lightIntensity = ls.Li;"
            " lightPos = lightData.posW; \n"
            " shadowCoordsDx1 = world_to_latlong_map(normalize(hitPointDx1 - lightPos)) * resolution;"
            " shadowCoordsDx2 = world_to_latlong_map(normalize(hitPointDx2 - lightPos)) * resolution;"
            " shadowCoordsDy1 = world_to_latlong_map(normalize(hitPointDy1 - lightPos)) * resolution;"
            " shadowCoordsDy2 = world_to_latlong_map(normalize(hitPointDy2 - lightPos)) * resolution; \n"
            " ShadowCoordDx = abs(shadowCoordsDx1 - shadowCoordsDx2);"
            " ShadowCoordDy = abs(shadowCoordsDy1 - shadowCoordsDy2); \n"
            " mipLevel = clamp( calcLod(ShadowCoordDx, ShadowCoordDy) ,0 ,MIPCOUNT); \n"
            "resolution = getMipDimensions(int(mipLevel));"
            " shadowMapCoord = world_to_latlong_map(toHitPos) * resolution;"
            " pixel_centered_dir = latlong_map_to_world( (floor(shadowMapCoord)+float2(0.5f)) / resolution);"
            " bias = max(gDepthBias * (1.0 - dot(shadeData.N, pixel_centered_dir)), 0.01); \n"
            " shadowFactor = 1.0f;"
            " closestDepth = readDepth" + std::to_string(i) + "(int(mipLevel),shadowMapCoord);\n"
            "if(closestDepth > 0.0f)"
            "{"
            "  float currentDepth =  ls.distance; "
            "if(currentDepth - bias > closestDepth){"
            "   shadowFactor = 0.0f;"
            "}"
            "} \n"
            "else"
            "{"
            "  float closestDepth = depthRay(lightData.posW, pixel_centered_dir, 0, 1e+38f);"
            "writeDepth" + std::to_string(i) + "(int(mipLevel), shadowMapCoord, closestDepth);\n"
            "float currentDepth =  ls.distance;"
            "if(currentDepth - bias > closestDepth){"
            "    shadowFactor = 0.0f;"
            "}"
            "} \n"
            " NdotL = saturate(dot(shadeData.N, toLight));"
            "bsdf = gScene.materials.getBSDF(shadeData, lod);"
            "bsdfProperties = bsdf.getProperties(shadeData) \n;"
            "shininess = 10.0;"
            "outColor += shadowFactor * (bsdfProperties.diffuseReflectionAlbedo + (pow(NdotL, shininess) * bsdfProperties.specularReflectionAlbedo)) * M_1_PI * lightIntensity;"
            "writeFeedback" + std::to_string(i) + "(mipLevel,shadowMapCoord);";
    }


    return s;
}
