/***************************************************************************
 # Copyright (c) 2015-21, NVIDIA CORPORATION. All rights reserved.
 #
 # Redistribution and use in source and binary forms, with or without
 # modification, are permitted provided that the following conditions
 # are met:
 #  * Redistributions of source code must retain the above copyright
 #    notice, this list of conditions and the following disclaimer.
 #  * Redistributions in binary form must reproduce the above copyright
 #    notice, this list of conditions and the following disclaimer in the
 #    documentation and/or other materials provided with the distribution.
 #  * Neither the name of NVIDIA CORPORATION nor the names of its
 #    contributors may be used to endorse or promote products derived
 #    from this software without specific prior written permission.
 #
 # THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS "AS IS" AND ANY
 # EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 # IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 # PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 # CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 # EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 # PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 # PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 # OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 # (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 # OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **************************************************************************/
#include "ShadowRayCache.h"

#include "RenderGraph/RenderPassHelpers.h"
#include "VirtualShadowMap/Utils/ShaderDefineUtils.h"
#include "VirtualShadowMap/Utils/MipColorData.h"

const RenderPass::Info ShadowRayCache::kInfo{ "ShadowRayCache", "Shadow Ray Caching" };

namespace
{

    const char kShaderFile[] = "RenderPasses/ShadowRayCache/shadowray_cache.slang";
    const char kShaderModel[] = "6_5";

    const uint32_t kMaxPayloadSizeBytes = 164;
    const uint32_t kMaxRecursionDepth = 2;

    const ChannelList kOutputChannels =
    {
        { "diffuse",   "gOutputDiffuse","Diffuse color",    true /* optional */, ResourceFormat::RGBA32Float },
        { "lod",       "gOutputLod",    "Lod",              true /* optional */, ResourceFormat::RGBA32Float },
        { "visibility","gOutputVis",    "light visibility" ,true /* optional */, ResourceFormat::RGBA32Float },
    };

    const Gui::DropdownList kLightEvaluationModeList =
    {
        { (uint32_t)ShadowRayCache::LightEvaluationMode::Naive, "Naive" },
        { (uint32_t)ShadowRayCache::LightEvaluationMode::RadiusSearch, "Radius Search" },
    };
}

// Don't remove this. it's required for hot-reload to function properly
extern "C" FALCOR_API_EXPORT const char* getProjDir()
{
    return PROJECT_DIR;
}

extern "C" FALCOR_API_EXPORT void getPasses(Falcor::RenderPassLibrary & lib)
{
    lib.registerPass(ShadowRayCache::kInfo, ShadowRayCache::create);
}

ShadowRayCache::SharedPtr ShadowRayCache::create(RenderContext* pRenderContext, const Dictionary& dict)
{
    SharedPtr pPass = SharedPtr(new ShadowRayCache());
    return pPass;
}

ShadowRayCache::ShadowRayCache() : RenderPass(kInfo), mLightEvaluationMode((uint32_t)ShadowRayCache::LightEvaluationMode::Naive),
mDepthBias(0.05f)
{
}

Dictionary ShadowRayCache::getScriptingDictionary()
{
    return Dictionary();
}

RenderPassReflection ShadowRayCache::reflect(const CompileData& compileData)
{
    // Define the required resources here
    RenderPassReflection reflector;

    addRenderPassOutputs(reflector, kOutputChannels);

    return reflector;
}

void ShadowRayCache::execute(RenderContext* pRenderContext, const RenderData& renderData)
{
    for (const auto& channel : kOutputChannels)
    {
        pRenderContext->clearTexture(renderData[channel.name]->asTexture().get());
    }

    if (!mpScene) return;

    if (mOptionsChanged)
    {
        Program::DefineList defines;
        // change light method
        defines.add("USE_RADIUS_SEARCH", (mLightEvaluationMode == (uint32_t)ShadowRayCache::LightEvaluationMode::RadiusSearch) ? "true" : "false");

        mpProgram->addDefines(defines);

        mOptionsChanged = false;
    }

    // Prepare program vars. This may trigger shader compilation.
    // The program should have all necessary defines set at this point.
    if (!mpVars) prepareVars();
    assert(mpVars);

    // Get dimensions of ray dispatch.
    const uint2 targetDim = renderData.getDefaultTextureDims();
    assert(targetDim.x > 0 && targetDim.y > 0);

    // Bind I/O buffers. These needs to be done per-frame as the buffers may change anytime.
    auto bind = [&](const ChannelDesc& desc)
    {
        if (!desc.texname.empty())
        {
            auto pGlobalVars = mpVars->getRootVar();
            pGlobalVars[desc.texname] = renderData[desc.name]->asTexture();
        }
    };

    for (const auto& channel : kOutputChannels) bind(channel);

    // bind all mip uavs for all lights
    for (uint lightIndex = 0; lightIndex < mpScene->getActiveLightCount(); ++lightIndex)
    {
        for (uint mipLevel = 0; mipLevel < mpShadowMapTextures[0]->getMipCount(); ++mipLevel) {
            mpVars["gShadowMap" + std::to_string(lightIndex) + "_" + std::to_string(mipLevel)].setUav(mpShadowMapTextures[lightIndex]->getUAV(mipLevel));
        }
    }

    // bind mip color texture
    mpVars["gMipColor"] = mipColorTex->asTexture();

    // bind all feedback texture standard mip uavs
    for (uint lightIndex = 0; lightIndex < mpScene->getActiveLightCount(); ++lightIndex)
    {
        // feedback only for standard packed mips
        for (uint mipLevel = 0; mipLevel < mpShadowMapTextures[0]->getPackedMipInfo().NumStandardMips; ++mipLevel) {
            mpVars["gFeedbackMip" + std::to_string(lightIndex) + "_" + std::to_string(mipLevel)].setUav(mpFeedbackTextures[lightIndex]->getUAV(mipLevel));
        }
    }

    // set depth bias
    mpVars["PerFrameCB"]["gDepthBias"] = mDepthBias;
 
    mpScene->raytrace(pRenderContext, mpProgram.get(), mpVars, uint3(targetDim, 1));

    // Feedback processing
    mpTileUpdateManager->processFeedback();
    mpTileUpdateManager->updateTiles();
    mpTileUpdateManager->clearFeedback();

}

void ShadowRayCache::renderUI(Gui::Widgets& widget)
{
    bool dirty = false;

    if (widget.dropdown("Light Evaluation mode", kLightEvaluationModeList, mLightEvaluationMode))
        dirty = true;
    widget.tooltip("How to determine lights used for shading.");

    //TODO: choose which light
    if (widget.button("Save Tiled Texture Mip Level"))
    {
        auto filename = std::string("D:\\tiledTex_mip") + std::to_string(mipLevelToWrite) + ".pfm";
        mpShadowMapTextures[0]->captureToFile(mipLevelToWrite, 0, filename, Bitmap::FileFormat::PfmFile, Bitmap::ExportFlags::Uncompressed);
    }

    widget.slider("Mip Level", mipLevelToWrite, 0, static_cast<int>(mpShadowMapTextures[0]->getMipCount()) - 1);

    widget.var("Depth Bias", mDepthBias, 0.f, FLT_MAX, 0.0001f);

    if (dirty)
    {
        mOptionsChanged = true;
    }

}

void ShadowRayCache::setScene(RenderContext* pRenderContext, const Scene::SharedPtr& pScene)
{
    mpScene = pScene;

    recreateVars();

    constexpr uint shadowMapWidth = 16384;
    constexpr uint shadowMapHeight = 8192;
    const uint numLights = pScene->getActiveLightCount();

    // Texture for LOD colors
    mipColorTex = Texture::create1D(7, ResourceFormat::RGBA8Unorm, 1, 1, getMipColorData().data(), Resource::BindFlags::ShaderResource);

    // create a tiled tex and feedback map for every light
    for (uint i = 0; i < numLights; ++i)
    {
        auto mpShadowMap = TiledTexture::create2DTiled(shadowMapWidth, shadowMapHeight, ResourceFormat::R32Float);
        pRenderContext->clearTextureAndAllMips(mpShadowMap.get());
        uint tileWidth = mpShadowMap->getTileTexelWidth();
        uint tileHeight = mpShadowMap->getTileTexelHeight();
        mpShadowMapTextures.push_back(mpShadowMap);

        auto feedbackTex = FeedbackTexture::createFeedbackTexture(shadowMapWidth, shadowMapHeight, tileWidth, tileHeight);
        for (uint mipIndex = 0; mipIndex < feedbackTex->getMipCount(); ++mipIndex)
        {
            pRenderContext->clearUAV(feedbackTex->getUAV(mipIndex).get(), uint4(0));
        }
        mpFeedbackTextures.push_back(feedbackTex);
    }

    // heapsize (in tiles) for shadow maps
    uint heapsize = numLights * mpShadowMapTextures[0]->getNumTilesTotal() / 2;

    // create heap and heap tile manager
    mpTileUpdateManager = TileUpdateManager::createTileUpdateManager(mpFeedbackTextures, mpShadowMapTextures, heapsize, pRenderContext);

    if (mpScene)
    {
        if (pScene->hasGeometryType(Scene::GeometryType::Custom))
        {
            logWarning("PathTracer: This render pass does not support custom primitives.");
        }

        RtProgram::Desc progDesc;
        progDesc.addShaderLibrary(kShaderFile);
        progDesc.setShaderModel(kShaderModel);
        progDesc.setMaxPayloadSize(kMaxPayloadSizeBytes);
        progDesc.setMaxAttributeSize(pScene->getRaytracingMaxAttributeSize());
        progDesc.setMaxTraceRecursionDepth(kMaxRecursionDepth);


        mpBindingTable = RtBindingTable::create(2, 2, mpScene->getGeometryCount());
        auto& sbt = mpBindingTable;
        sbt->setRayGen(progDesc.addRayGen("RayGen"));
        sbt->setMiss(0, progDesc.addMiss("PrimaryMiss"));
        sbt->setMiss(1, progDesc.addMiss("ShadowMiss"));

        auto materialTypes = pScene->getMaterialSystem()->getMaterialTypes();

        for (const auto materialType : materialTypes)
        {
            auto typeConformances = pScene->getMaterialSystem()->getTypeConformances(materialType);

            auto primaryShaderID = progDesc.addHitGroup("PrimaryClosestHit", "PrimaryAnyHit", "", typeConformances, to_string(materialType));

            auto shadowShaderID = progDesc.addHitGroup("ShadowClosestHit", "ShadowAnyHit", "", typeConformances, to_string(materialType));

            auto customGeometryShaderID = progDesc.addHitGroup("boxClosestHit", "boxAnyHit","boxIntersect", typeConformances, to_string(materialType));

            auto geometryIDs = pScene->getGeometryIDs(Scene::GeometryType::TriangleMesh, materialType);

            sbt->setHitGroup(0, geometryIDs, primaryShaderID);
            sbt->setHitGroup(1, geometryIDs, shadowShaderID);

            auto customGeometryIDs = pScene->getGeometryIDs(Scene::GeometryType::Custom, materialType);

            sbt->setHitGroup(0, customGeometryIDs, customGeometryShaderID);
        }

        // add light AABBs as custom primitive
        for (const auto& light : mpScene->getActiveLights())
        {
            float r = 10.f;
            float3 c = light->getData().posW;
            AABB box = AABB(c - r, c + r);
            mpScene->addCustomPrimitive(0, box);
        }
        
        
        Program::DefineList defines;
        defines.add(mpScene->getSceneDefines());

        // all shadow maps have same mipCount and tile size
        uint mipCount = mpShadowMapTextures[0]->getMipCount();
        uint tileWidth = mpShadowMapTextures[0]->getTileTexelWidth();
        uint tileHeight = mpShadowMapTextures[0]->getTileTexelHeight();
        uint numStandardMips = mpShadowMapTextures[0]->getPackedMipInfo().NumStandardMips;

        std::string shadowMapString, writeDepthString, readDepthString, feedbackMapString, writeFeedbackString;

        // create defines for shadow map access
        for (uint i = 0; i < numLights; ++i)
        {
            //shadow map texture definition
            shadowMapString += getMipViewDefineString(i, mipCount);
            //write depth
            writeDepthString += getWriteToMipFunctionString(i, mipCount);
            //read depth
            readDepthString += getReadFromMipFunctionString(i, mipCount);
            //feedback texture definition
            feedbackMapString += getFeedbackViewDefineString(i, numStandardMips);
            //write feedback
            writeFeedbackString += getWriteFeedbackString(i, numStandardMips);
        }

        defines.add("SHADOW_TEXTURES", shadowMapString);
        defines.add("WRITE_TO_MIP_FUNC", writeDepthString);
        defines.add("READ_FROM_MIP_FUNC", readDepthString);
        defines.add("FEEDBACK_TEXTURES", feedbackMapString);
        defines.add("WRITE_FEEDBACK_FUNC", writeFeedbackString);

        // dimensions for shadow tex, mipcount and tile size are the same for all
        defines.add("GET_MIP_DIMENSIONS_FUNC", mipDimensionsFunctionString(mipCount));
        defines.add("MIPCOUNT", std::to_string(mipCount));
        defines.add("TILE_WIDTH", std::to_string(tileWidth));
        defines.add("TILE_HEIGHT", std::to_string(tileHeight));
        
        defines.add("SUM_LIGHT_FUNCTION", getLightCalculationString(numLights));

        defines.add("USE_RADIUS_SEARCH", (mLightEvaluationMode == (uint32_t)ShadowRayCache::LightEvaluationMode::RadiusSearch) ? "1" : "0");

        mpProgram = RtProgram::create(progDesc, defines);

        assert(mpProgram);
    }
}

void ShadowRayCache::prepareVars()
{
    assert(mpScene);
    assert(mpProgram);

    // Create program variables for the current program/scene.
    // This may trigger shader compilation. If it fails, throw an exception to abort rendering.
    mpVars = RtProgramVars::create(mpProgram, mpBindingTable);
    assert(mpVars);

    // Bind utility classes into shared data.
    auto pGlobalVars = mpVars->getRootVar();
}

