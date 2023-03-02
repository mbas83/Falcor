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
#include "DeferredRenderer.h"

#include "Core/API/BlendState.h"
#include "VirtualShadowMap/Utils/MipColorData.h"
#include "VirtualShadowMap/Utils/ShaderDefineUtils.h"

const RenderPass::Info DeferredRenderer::kInfo{ "DeferredRenderer", "Uses G-Buffer for Deferred Rendering." };

// Don't remove this. it's required for hot-reload to function properly
extern "C" FALCOR_API_EXPORT const char* getProjDir()
{
    return PROJECT_DIR;
}

// pybind to get current memory usage
static void regDeferredPass(pybind11::module& m)
{
    pybind11::class_<DeferredRenderer, RenderPass, DeferredRenderer::SharedPtr> pass(m, "DeferredRenderer");

    pass.def_property_readonly("memoryUsage", &DeferredRenderer::getCurrentMemoryUsage);
    pass.def("startCaptureMemoryUsage", &DeferredRenderer::startCaptureMemoryUsage);
    pass.def("endCaptureMemoryUsage", &DeferredRenderer::endCaptureMemoryUsage);
    pass.def("outputCapturedMemoryUsage", &DeferredRenderer::outputMemoryUsage);
}


extern "C" FALCOR_API_EXPORT void getPasses(Falcor::RenderPassLibrary & lib)
{
    lib.registerPass(DeferredRenderer::kInfo, DeferredRenderer::create);
    ScriptBindings::registerBinding(regDeferredPass);
}

namespace
{
    const char kShaderFile[] = "RenderPasses/DeferredRenderer/DeferredRenderer.3d.slang";
    const char kShaderModel[] = "6_5";

    const std::string kPosW = "posW";
    const std::string kNormW = "normW";
    const std::string kDiffuse = "diffuse";
    const std::string kSpecular = "specular";
    const std::string kTexGrad = "texGrad";

    const std::string kDebug = "debug";

    const Falcor::ChannelList kInputChannels =
    {
        { kPosW,        "gWorldPos",         "position in world space" },
        { kNormW,  "gWorldNormal",   "normal in world space" },
        { kDiffuse,        "gDiffuse",           "diffuse color" },
        { kSpecular,    "gSpecular",     "specular color" },
        { kTexGrad,    "gTexGrads",     "Texture gradients (ddx, ddy)" }
    };


    const std::string kOut = "outColor";

    struct PointLightVertex
    {
        float radius;
        uint lightIndex;
    };

}


DeferredRenderer::SharedPtr DeferredRenderer::create(RenderContext* pRenderContext, const Dictionary& dict)
{
    SharedPtr pPass = SharedPtr(new DeferredRenderer());
    return pPass;
}

Dictionary DeferredRenderer::getScriptingDictionary()
{
    return Dictionary();
}

RenderPassReflection DeferredRenderer::reflect(const CompileData& compileData)
{
    // Define the required resources here
    RenderPassReflection reflector;
    addRenderPassInputs(reflector, kInputChannels);

    reflector.addOutput(kOut, "Final color of deferred renderer");
    reflector.addOutput(kDebug, "debug tex").format(ResourceFormat::RGBA32Float);
    return reflector;
}

void DeferredRenderer::execute(RenderContext* pRenderContext, const RenderData& renderData)
{
    // bind output fbo
    mpFbo->attachColorTarget(renderData[kOut]->asTexture(), 0);
    constexpr float4 clearColor(0, 0, 0, 1);
    pRenderContext->clearFbo(mpFbo.get(), clearColor, 1.0f, 0, FboAttachmentType::All);
    mpState->setFbo(mpFbo);

    pRenderContext->clearTexture(renderData[kDebug]->asTexture().get());

    if (!mpScene) return;

    // GBuffer Input
    mpVars["gWorldPos"] = renderData[kPosW]->asTexture();
    mpVars["gWorldNormal"] = renderData[kNormW]->asTexture();
    mpVars["gDiffuse"] = renderData[kDiffuse]->asTexture();
    mpVars["gSpecular"] = renderData[kSpecular]->asTexture();
    mpVars["gTexGrad"] = renderData[kTexGrad]->asTexture();

    //Output
    mpVars["gDebug"] = renderData[kDebug]->asTexture();

    mpVars["PerFrameCB"]["gFrameCount"] = mFrameCount++;
    mpVars["PerFrameCB"]["viewportDims"] = float2(mpFbo->getWidth(), mpFbo->getHeight());
    // TODO: test: depth bias
    mpVars["PerFrameCB"]["gMipBias"].setBlob(mipBiasVals.data(), sizeof(float4) * 6);


    // bind all shadow map mip UAVs for all lights
    for (uint lightIndex = 0; lightIndex < numLights; ++lightIndex)
    {
        for (uint mipLevel = 0; mipLevel < numShadowMips; ++mipLevel) {
            // index = numMips*lightIndex + mipLevel
            mpVars["gVirtualShadowMapUAVs"][lightIndex * numShadowMips + mipLevel].setUav(mpShadowMapUAVs[lightIndex][mipLevel]);
        }
    }

    // bind all shadow map SRVs
    for (uint lightIndex = 0; lightIndex < numLights; ++lightIndex)
    {
        // specific mips are accessed in shader
        mpVars["gVirtualShadowMapSRVs"][lightIndex] = mpShadowMapTextures[lightIndex]->asTexture();
    }

    // bind all feedback texture standard mip uavs
    for (uint lightIndex = 0; lightIndex < numLights; ++lightIndex)
    {
        // feedback only for standard packed mips
        for (uint mipLevel = 0; mipLevel < numStandardMips; ++mipLevel) {
            mpVars["gFeedbackMaps"][lightIndex * numStandardMips + mipLevel].setUav(mpFeedbackMapUAVs[lightIndex][mipLevel]);
        }
    }

    // bind mip color texture
    mpVars["gMipColor"] = mipColorTex->asTexture();
    {
        FALCOR_PROFILE("drawLights");

        mpState->setVao(mpLightsVao);
        mpState->setRasterizerState(mpRsState);
        mpScene->setRaytracingShaderData(pRenderContext, mpVars->getRootVar());
        mpVars->setParameterBlock("gScene", mpScene->getParameterBlock());

        pRenderContext->drawIndirect(mpState.get(), mpVars.get(), 1, mpDrawBuffer.get(), 0, nullptr, 0);
        //pRenderContext->draw(mpState.get(),mpVars.get(),numLights,0);
    }

    {
        FALCOR_PROFILE("Ambient Light");
        executeAmbientLightPass(pRenderContext, renderData);
    }

    if (mSaveDebug)
    {
        auto filename = std::string("D:\\debugTex") + ".pfm";
        renderData[kDebug]->asTexture()->captureToFile(0, 0, filename, Bitmap::FileFormat::PfmFile, Bitmap::ExportFlags::Uncompressed);
        mSaveDebug = false;
    }

    // determine used feddback textures for thhe current frame
    mEndIndexFeedback = std::min(mStartIndexFeedback + mNumFeedbackReadsPerFrame, numLights);

    // Feedback processing (Reading from feedback texture and processing data on CPU)
    {
        FALCOR_PROFILE("Read and Process Feedback");
        mpTileUpdateManager->processFeedback(mStartIndexFeedback, mEndIndexFeedback);
    }

    mStartIndexFeedback = mEndIndexFeedback % numLights;

    // map and unmap tiles
    {
        FALCOR_PROFILE("Update Tiles");
        mpTileUpdateManager->updateTiles(mStartIndexFeedback, mEndIndexFeedback);
    }
    //clear Feedback textures
    {
        FALCOR_PROFILE("Clear Feedback Textures");
        mpTileUpdateManager->clearFeedback();
    }


    if (mRenderShadowMap)
    {
        FALCOR_PROFILE("Draw Shadow Map");
        executeDrawShadowMap(pRenderContext, renderData);
    }

    if (mRecordMemoryUsage)
    {
        recordMemoryUsage();
    }

}

void DeferredRenderer::renderUI(Gui::Widgets& widget)
{

    if (widget.button("Save Tiled Texture Mip Level"))
    {
        auto filename = std::string("D:\\tiledTex_mip") + std::to_string(lightIndexToWrite) + "_" + std::to_string(mipLevelToWrite) + ".pfm";
        mpShadowMapTextures[lightIndexToWrite]->captureToFile(mipLevelToWrite, 0, filename, Bitmap::FileFormat::PfmFile, Bitmap::ExportFlags::Uncompressed);
    }

    if (widget.button("Save Debug Tex"))
    {
        mSaveDebug = true;
    }

    if (widget.button("End Capture Memory Usage"))
    {
        mRecordMemoryUsage = false;
        FileDialogFilterVec filters;
        filters.push_back({ "csv", "CSV Files" });
        std::filesystem::path path;
        if (saveFileDialog(filters, path))
        {
            mBenchmarkMemoryOutputFilePath = path.string();
            outputMemoryUsage();
        }
    }

    widget.slider("LightIndex", lightIndexToWrite, 0, static_cast<int>(mpShadowMapTextures.size()) - 1);
    widget.slider("Mip Level", mipLevelToWrite, 0, static_cast<int>(mpShadowMapTextures[0]->getMipCount()) - 1);


    const float heapSizeInMB = static_cast<float>(mpTileUpdateManager->getHeapSizeInBytes()) / 1000 / 1000;
    const float usedMemoryInMB = static_cast<float>(mpTileUpdateManager->getCurrentlyUsedMemory()) / 1000 / 1000;

    widget.checkbox("Show Shadow Map Content", mRenderShadowMap);
    if (mRenderShadowMap)
    {
        if (auto group = widget.group("RenderShadowMap", true)) {
            group.slider("LightIndex", mRenderSMIndex, uint(0), static_cast<uint>(mpShadowMapTextures.size()) - 1);
            group.slider("Mip Level", mRenderMipLevel, uint(0), static_cast<uint>(mpShadowMapTextures[0]->getMipCount()) - 1);
        }
    }

    std::string memstring("TileHeap GPU Memory:" + std::to_string(usedMemoryInMB) + "MB/" + std::to_string(heapSizeInMB) + "MB");
    widget.text(memstring);

    widget.var("Constant Bias Mip 0 ", mipBiasVals[0].x, 0.f, 5.f, 0.001f);
    widget.var("Slope Bias Mip 0", mipBiasVals[0].y, 0.f, 5.f, 0.001f);
    widget.var("Constant Bias Mip 1", mipBiasVals[1].x, 0.f, 5.f, 0.001f);
    widget.var("Slope Bias Mip 1", mipBiasVals[1].y, 0.f, 5.f, 0.001f);
    widget.var("Constant Bias Mip 2", mipBiasVals[2].x, 0.f, 5.f, 0.001f);
    widget.var("Slope Bias Mip 2", mipBiasVals[2].y, 0.f, 5.f, 0.001f);
    widget.var("Constant Bias Mip 3", mipBiasVals[3].x, 0.f, 5.f, 0.001f);
    widget.var("Slope Bias Mip 3", mipBiasVals[3].y, 0.f, 5.f, 0.001f);
    widget.var("Constant Bias Mip 4", mipBiasVals[4].x, 0.f, 5.f, 0.001f);
    widget.var("Slope Bias Mip 4", mipBiasVals[4].y, 0.f, 5.f, 0.001f);
    widget.var("Constant Bias Mip 5", mipBiasVals[5].x, 0.f, 5.f, 0.001f);
    widget.var("Slope Bias Mip 5", mipBiasVals[5].y, 0.f, 5.f, 0.001f);
}

void DeferredRenderer::setScene(RenderContext* pRenderContext, const Scene::SharedPtr& pScene)
{
    mpScene = pScene;

    //VSM
    constexpr uint shadowMapWidth = 16384;
    constexpr uint shadowMapHeight = 8192;
    constexpr uint maxMipCount = 6;

    // only use level 0 to numUsedMipsForFeedback-1 for feedback, other mips always allocated
    constexpr uint numPreAllocateHighestMips = 0;


    if (mpScene)
    {
        // create Program desc
        Program::Desc desc;
        Shader::DefineList defines;
        desc.addShaderLibrary(kShaderFile).vsEntry("vsMain").gsEntry("gsMain").psEntry("psMain");
        desc.addTypeConformances(mpScene->getTypeConformances());
        desc.setShaderModel(kShaderModel);

        defines.add(pScene->getSceneDefines());

        // create light vertex attributes
        numLights = mpScene->getActiveLightCount();
        std::vector<PointLightVertex> pointLights;

        const auto sceneLights = mpScene->getActiveLights();

        for (uint i = 0; i < numLights; ++i)
        {
            PointLightVertex p;
            const auto& lightData = mpScene->getLight(i)->getData();
            p.radius = 40; //TODO: change radius calculation
            p.lightIndex = i;
            pointLights.emplace_back(p);
        }

        const uint32_t vbSize = (uint32_t)(sizeof(PointLightVertex) * numLights);
        // create vertex buffer for points
        auto pVB = Buffer::create(vbSize, Buffer::BindFlags::Vertex, Buffer::CpuAccess::Write, (void*)pointLights.data());
        FALCOR_ASSERT(pVB);

        // create vao (only need radius and index of light) 
        VertexLayout::SharedPtr pLayout = VertexLayout::create();
        VertexBufferLayout::SharedPtr pBufLayout = VertexBufferLayout::create();
        pBufLayout->addElement("RADIUSSIZE", 0, ResourceFormat::R32Float, 1, 0);    //radius
        pBufLayout->addElement("LIGHTINDEX", 1 * sizeof(float), ResourceFormat::R16Uint, 1, 1);    //lightindex
        pLayout->addBufferLayout(0, pBufLayout);


        Vao::BufferVec buffers{ pVB };
        mpLightsVao = Vao::create(Vao::Topology::PointList, pLayout, buffers);

        // create draw buffer
        std::vector<DrawArguments> drawLightPoints;

        DrawArguments draw;
        draw.VertexCountPerInstance = numLights;
        draw.InstanceCount = 1;
        draw.StartVertexLocation = 0;
        draw.StartInstanceLocation = 0;
        drawLightPoints.push_back(draw);

        mpDrawBuffer = Buffer::create(sizeof(drawLightPoints[0]) * drawLightPoints.size(), Resource::BindFlags::IndirectArg, Buffer::CpuAccess::None, drawLightPoints.data());
        mpDrawBuffer->setName("Lights Draw Buffer");

        // Texture for LOD colors
        mipColorTex = Texture::create1D(7, ResourceFormat::RGBA8Unorm, 1, 1, getMipColorData().data(), Resource::BindFlags::ShaderResource);

        // create a tiled tex and feedback map for every light
        for (uint i = 0; i < numLights; ++i)
        {
            auto shadowMap = TiledTexture::create2DTiled(shadowMapWidth, shadowMapHeight, ResourceFormat::R32Float, 1, maxMipCount);
            pRenderContext->clearTextureAndAllMips(shadowMap.get());
            uint tileWidth = shadowMap->getTileTexelWidth();
            uint tileHeight = shadowMap->getTileTexelHeight();
            mpShadowMapTextures.push_back(shadowMap);

            auto feedbackTex = FeedbackTexture::createFeedbackTexture(shadowMapWidth, shadowMapHeight, tileWidth, tileHeight, maxMipCount);
            for (uint mipIndex = 0; mipIndex < feedbackTex->getMipCount(); ++mipIndex)
            {
                pRenderContext->clearUAV(feedbackTex->getUAV(mipIndex).get(), uint4(0));
            }
            mpFeedbackTextures.push_back(feedbackTex);
        }


        // heapsize (in tiles) for shadow maps
        uint heapsize = 90 * mpShadowMapTextures[0]->getNumTilesTotal();

        // create heap and heap tile manager
        mpTileUpdateManager = TileUpdateManager::createTileUpdateManager(mpFeedbackTextures, mpShadowMapTextures, heapsize, pRenderContext, numPreAllocateHighestMips);


        // add defines for shadow map access
        numShadowMips = mpShadowMapTextures[0]->getMipCount();
        numStandardMips = mpShadowMapTextures[0]->getPackedMipInfo().NumStandardMips;
        const uint tileWidth = mpShadowMapTextures[0]->getTileTexelWidth();
        const uint tileHeight = mpShadowMapTextures[0]->getTileTexelHeight();

        // assume no packed mips because feedback texture access in shader relies on it
        FALCOR_ASSERT(numStandardMips == numShadowMips);
        FALCOR_ASSERT(numPreAllocateHighestMips < numStandardMips);

        defines.add("TILE_WIDTH", std::to_string(tileWidth));
        defines.add("TILE_HEIGHT", std::to_string(tileHeight));

        defines.add("MIPCOUNT", std::to_string(numShadowMips));
        defines.add("NUMSTANDARDMIPS", std::to_string(numStandardMips));
        defines.add("LIGHTCOUNT", std::to_string(numLights));
        defines.add("SHADOWTEXTURECOUNT", std::to_string(numLights * numShadowMips));
        defines.add("FEEDBACKTEXTURECOUNT", std::to_string(numLights * numStandardMips));


        mpProgram = GraphicsProgram::create(desc, defines);
        mpState->setProgram(mpProgram);

        mpVars = GraphicsVars::create(mpProgram->getReflector());


        preGenerateUAVS();
    }
}



DeferredRenderer::DeferredRenderer() : RenderPass(kInfo), mStartIndexFeedback(0), mEndIndexFeedback(0)
{
    mpState = GraphicsState::create();

    // Create the rasterizer state
    RasterizerState::Desc rastDesc;
    rastDesc.setCullMode(RasterizerState::CullMode::None);
    mpRsState = RasterizerState::create(rastDesc);

    DepthStencilState::Desc dsDesc;
    dsDesc.setDepthEnabled(false);
    mpState->setDepthStencilState(DepthStencilState::create(dsDesc));

    BlendState::Desc blendDesc;
    blendDesc.setIndependentBlend(false);
    blendDesc.setRtBlend(0, true);
    blendDesc.setRtParams(0, BlendState::BlendOp::Add,
        BlendState::BlendOp::Max,
        BlendState::BlendFunc::One,
        BlendState::BlendFunc::One,
        BlendState::BlendFunc::One,
        BlendState::BlendFunc::One);
    mpState->setBlendState(BlendState::create(blendDesc));


    mpFbo = Fbo::create();

    // ambient light pass
    mpAmbientLightPass = FullScreenPass::create("RenderPasses/DeferredRenderer/AmbientPass/AmbientPass.ps.slang");
    mpAmbientLightPass->getState()->setBlendState(BlendState::create(blendDesc));

    // render SM pass
    mpRenderShadowTexturePass = FullScreenPass::create("RenderPasses/DeferredRenderer/AmbientPass/RenderSM.ps.slang");
    BlendState::Desc overwriteBlend;
    blendDesc.setIndependentBlend(false);
    blendDesc.setRtBlend(0, true);
    blendDesc.setRtParams(0, BlendState::BlendOp::Add,
        BlendState::BlendOp::Max,
        BlendState::BlendFunc::One,
        BlendState::BlendFunc::Zero,
        BlendState::BlendFunc::One,
        BlendState::BlendFunc::Zero);
    mpRenderShadowTexturePass->getState()->setBlendState(BlendState::create(overwriteBlend));
    mpRenderShadowTexturePass->getState()->setDepthStencilState(DepthStencilState::create(dsDesc));

    // sampler for showing SM content on screen
    Sampler::Desc desc;
    desc.setAddressingMode(Sampler::AddressMode::Clamp, Sampler::AddressMode::Clamp, Sampler::AddressMode::Clamp);
    desc.setReductionMode(Sampler::ReductionMode::Standard);
    desc.setFilterMode(Sampler::Filter::Point, Sampler::Filter::Point, Sampler::Filter::Point);
    mpRenderSMSampler = Sampler::create(desc);

    mipBiasVals.fill(float4(0.01f));
}

void DeferredRenderer::preGenerateUAVS()
{
    // shadow map uavs
    mpShadowMapUAVs.resize(mpShadowMapTextures.size());

    for (uint lightIndex = 0; lightIndex < numLights; ++lightIndex)
    {
        for (uint mipLevel = 0; mipLevel < numShadowMips; ++mipLevel) {
            // index = numMips*lightIndex + mipLevel
            mpShadowMapUAVs[lightIndex].emplace_back(mpShadowMapTextures[lightIndex]->getUAV(mipLevel));
        }
    }

    // feedback map uavs
    mpFeedbackMapUAVs.resize(mpFeedbackTextures.size());

    for (uint lightIndex = 0; lightIndex < numLights; ++lightIndex)
    {
        // feedback only for standard packed mips
        for (uint mipLevel = 0; mipLevel < numStandardMips; ++mipLevel) {
            mpFeedbackMapUAVs[lightIndex].emplace_back(mpFeedbackTextures[lightIndex]->getUAV(mipLevel));
        }
    }
}

void DeferredRenderer::executeAmbientLightPass(RenderContext* pRenderContext, const RenderData& renderData)
{
    // iResolution
    float width = (float)mpFbo->getWidth();
    float height = (float)mpFbo->getHeight();
    mpAmbientLightPass["AmbientCB"]["iResolution"] = float2(width, height);

    // GBuffer Input
    mpAmbientLightPass->getVars()["gWorldPos"] = renderData[kPosW]->asTexture();
    mpAmbientLightPass->getVars()["gDiffuse"] = renderData[kDiffuse]->asTexture();
    mpAmbientLightPass->getVars()["gSpecular"] = renderData[kSpecular]->asTexture();

    mpAmbientLightPass->getState()->setFbo(mpFbo);
    // run final pass
    mpAmbientLightPass->execute(pRenderContext, mpFbo);
}


void DeferredRenderer::executeDrawShadowMap(RenderContext* pRenderContext, const RenderData& renderData)
{
    float width = (float)mpFbo->getWidth();
    float height = (float)mpFbo->getHeight();

    GraphicsState::Viewport lowerRight(width * 0.7f, height * 0.85f, width * 0.3f, height * 0.15f, 0.f, 1.f);

    mpRenderShadowTexturePass->getVars()["gShadowMap"].setSrv(mpShadowMapTextures[mRenderSMIndex]->getSRV(mRenderMipLevel, 1));
    mpRenderShadowTexturePass->getVars()->setSampler("gSampler", mpRenderSMSampler);

    // write into output texture
    mpRenderShadowTexturePass->getState()->setViewport(0, lowerRight, true);

    mpRenderShadowTexturePass->execute(pRenderContext, mpFbo, false);
}




const float DeferredRenderer::getCurrentMemoryUsage() const
{
    const float usedMemoryInMB = static_cast<float>(mpTileUpdateManager->getCurrentlyUsedMemory()) / 1000 / 1000;
    return usedMemoryInMB;
}



void DeferredRenderer::outputMemoryUsage()
{
    if (mBenchmarkMemoryOutputFilePath.empty() || mBenchmarkMemoryList.empty()) return;

    std::ofstream file = std::ofstream(mBenchmarkMemoryOutputFilePath, std::ios::trunc);

    if (!file) {
        reportError(fmt::format("Failed to open file '{}'.", mBenchmarkMemoryOutputFilePath));
        mBenchmarkMemoryOutputFilePath.clear();
        return;
    }

    // Write into file
    file << "Memory Usage in MB" << std::endl;
    file << std::fixed << std::setprecision(16);
    for (size_t i = 0; i < mBenchmarkMemoryList.size(); ++i) {
        file << mBenchmarkMemoryList[i];
        file << std::endl;
    }
    file.close();
}

void DeferredRenderer::recordMemoryUsage()
{
    mBenchmarkMemoryList.push_back(getCurrentMemoryUsage());
}
