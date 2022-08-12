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

extern "C" FALCOR_API_EXPORT void getPasses(Falcor::RenderPassLibrary & lib)
{
    lib.registerPass(DeferredRenderer::kInfo, DeferredRenderer::create);
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

    mpVars["PerFrameCB"]["gDepthBias"] = mDepthBias;

    /*

    // bind all mip uavs for all ligh ts
    for (uint lightIndex = 0; lightIndex < mpScene->getActiveLightCount(); ++lightIndex)
    {
        for (uint mipLevel = 0; mipLevel < mpShadowMapTextures[0]->getMipCount(); ++mipLevel) {
            mpVars["gShadowMap" + std::to_string(lightIndex) + "_" + std::to_string(mipLevel)].setUav(mpShadowMapTextures[lightIndex]->getUAV(mipLevel));
        }
    }

    // bind all feedback texture standard mip uavs
    for (uint lightIndex = 0; lightIndex < mpScene->getActiveLightCount(); ++lightIndex)
    {
        // feedback only for standard packed mips
        for (uint mipLevel = 0; mipLevel < mpShadowMapTextures[0]->getPackedMipInfo().NumStandardMips; ++mipLevel) {
            mpVars["gFeedbackMip" + std::to_string(lightIndex) + "_" + std::to_string(mipLevel)].setUav(mpFeedbackTextures[lightIndex]->getUAV(mipLevel));
        }
    }*/

    // bind mip color texture
    mpVars["gMipColor"] = mipColorTex->asTexture();

    FALCOR_PROFILE("drawLights");

    mpState->setVao(mpLightsVao);
    mpState->setRasterizerState(mpRsState);
    mpScene->setRaytracingShaderData(pRenderContext, mpVars->getRootVar());
    mpVars->setParameterBlock("gScene", mpScene->getParameterBlock());

    pRenderContext->drawIndirect(mpState.get(), mpVars.get(), 1, mpDrawBuffer.get(), 0, nullptr, 0);


    if (mSaveDebug)
    {
        auto filename = std::string("D:\\debugTex") + ".pfm";
        renderData[kDebug]->asTexture()->captureToFile(0, 0, filename, Bitmap::FileFormat::PfmFile, Bitmap::ExportFlags::Uncompressed);
        mSaveDebug = false;
    }

    // Feedback processing
    //mpTileUpdateManager->processFeedback();
    //mpTileUpdateManager->updateTiles();
    //mpTileUpdateManager->clearFeedback();
}

void DeferredRenderer::renderUI(Gui::Widgets& widget)
{
    widget.var("Depth Bias", mDepthBias, 0.f, FLT_MAX, 0.0001f);

    if (widget.button("Save Tiled Texture Mip Level"))
    {
        auto filename = std::string("D:\\tiledTex_mip") + std::to_string(lightIndexToWrite) + "_" + std::to_string(mipLevelToWrite) + ".pfm";
        mpShadowMapTextures[lightIndexToWrite]->captureToFile(mipLevelToWrite, 0, filename, Bitmap::FileFormat::PfmFile, Bitmap::ExportFlags::Uncompressed);
    }

    if (widget.button("Save Debug Tex"))
    {
        mSaveDebug = true;
    }

    widget.slider("LightIndex", lightIndexToWrite, 0, static_cast<int>(mpShadowMapTextures.size()) - 1);
    widget.slider("Mip Level", mipLevelToWrite, 0, static_cast<int>(mpShadowMapTextures[0]->getMipCount()) - 1);
}

void DeferredRenderer::setScene(RenderContext* pRenderContext, const Scene::SharedPtr& pScene)
{
    mpScene = pScene;


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
        const auto numLights = mpScene->getActiveLightCount();
        std::vector<PointLightVertex> pointLights;

        const auto sceneLights = mpScene->getActiveLights();

        for (uint i = 0; i < numLights; ++i)
        {
            PointLightVertex p;
            const auto& lightData = mpScene->getLight(i)->getData();
            p.radius = 10; //TODO: change radius calculation
            p.lightIndex = i;
            pointLights.emplace_back(p);
        }

        const uint32_t vbSize = (uint32_t)(sizeof(PointLightVertex) * numLights);
        auto pVB = Buffer::create(vbSize, Buffer::BindFlags::Vertex, Buffer::CpuAccess::Write, (void*)pointLights.data());
        FALCOR_ASSERT(pVB);

        // create vao (only need radius and index of light, 
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

        //VSM
        constexpr uint shadowMapWidth = 16384;
        constexpr uint shadowMapHeight = 8192;

        // Texture for LOD colors
        mipColorTex = Texture::create1D(7, ResourceFormat::RGBA8Unorm, 1, 1, getMipColorData().data(), Resource::BindFlags::ShaderResource);

        // TODO: move VSM init to other file?

        // create a tiled tex and feedback map for every light
        for (uint i = 0; i < numLights; ++i)
        {
            auto shadowMap = TiledTexture::create2DTiled(shadowMapWidth, shadowMapHeight, ResourceFormat::R32Float);
            pRenderContext->clearTextureAndAllMips(shadowMap.get());
            uint tileWidth = shadowMap->getTileTexelWidth();
            uint tileHeight = shadowMap->getTileTexelHeight();
            mpShadowMapTextures.push_back(shadowMap);

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


        // add defines for shadow map access
        const uint numMips = mpShadowMapTextures[0]->getMipCount();
        const uint tileWidth = mpShadowMapTextures[0]->getTileTexelWidth();
        const uint tileHeight = mpShadowMapTextures[0]->getTileTexelHeight();
        const uint numStandardMips = mpShadowMapTextures[0]->getPackedMipInfo().NumStandardMips;

        defines.add("SHADOW_TEXTURES", getMipViewDefineString(numLights, numMips));
        defines.add("WRITE_TO_MIP_FUNC", getWriteToMipFunctionString(numLights, numMips));
        defines.add("READ_FROM_MIP_FUNC", getReadFromMipFunctionString(numLights, numMips));
        defines.add("GET_MIP_DIMENSIONS_FUNC", mipDimensionsFunctionString(numMips));

        defines.add("FEEDBACK_TEXTURES", getFeedbackViewDefineString(numLights, numStandardMips));
        defines.add("WRITE_FEEDBACK_FUNC", getWriteFeedbackString(numLights, numStandardMips));

        defines.add("TILE_WIDTH", std::to_string(tileWidth));
        defines.add("TILE_HEIGHT", std::to_string(tileHeight));
        defines.add("MIPCOUNT", std::to_string(mpShadowMapTextures[0]->getMipCount()));

        mpProgram = GraphicsProgram::create(desc, defines);
        mpState->setProgram(mpProgram);

        mpVars = GraphicsVars::create(mpProgram->getReflector());

    }
}

DeferredRenderer::DeferredRenderer() : RenderPass(kInfo), mDepthBias(0.05f)
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
}
