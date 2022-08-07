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
        float3 pos;
        float radius;
        float3 color;
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

    if (!mpScene) return;

    // GBuffer Input
    mpVars["gWorldPos"] = renderData[kPosW]->asTexture();
    mpVars["gWorldNormal"] = renderData[kNormW]->asTexture();
    mpVars["gDiffuse"] = renderData[kDiffuse]->asTexture();
    mpVars["gSpecular"] = renderData[kSpecular]->asTexture();
    mpVars["gTexGrad"] = renderData[kTexGrad]->asTexture();

    //Output
    mpVars["gDebug"] = renderData[kDebug]->asTexture();

    mpVars["PerFrameCB"]["gViewMat"] = mpScene->getCamera()->getViewMatrix();
    mpVars["PerFrameCB"]["gProjMat"] = mpScene->getCamera()->getProjMatrix();
    mpVars["PerFrameCB"]["gCameraNearPlaneDepth"] = mpScene->getCamera()->getNearPlane();
    mpVars["PerFrameCB"]["gCameraPosW"] = mpScene->getCamera()->getPosition();
    mpVars["PerFrameCB"]["gCameraTargetW"] = mpScene->getCamera()->getTarget();
    mpVars["PerFrameCB"]["gDepthBias"] = mDepthBias;


    //mpScene->rasterize(pRenderContext, mpState.get(), mpVars.get(), mpRsState, mpRsState);


    FALCOR_PROFILE("rasterizeScene");

    mpState->setVao(mpLightsVao);
    mpState->setRasterizerState(mpRsState);
    mpScene->setRaytracingShaderData(pRenderContext, mpVars->getRootVar());
    mpVars->setParameterBlock("gScene", mpScene->getParameterBlock());

    pRenderContext->drawIndirect(mpState.get(), mpVars.get(), 1, mpDrawBuffer.get(), 0, nullptr, 0);

}

void DeferredRenderer::renderUI(Gui::Widgets& widget)
{
    widget.var("Depth Bias", mDepthBias, 0.f, FLT_MAX, 0.0001f);

    //TODO: choose which light
    if (widget.button("Save Tiled Texture Mip Level"))
    {
        auto filename = std::string("D:\\tiledTex_mip") + std::to_string(lightIndexToWrite) + "_" + std::to_string(mipLevelToWrite) + ".pfm";
        mpShadowMapTextures[lightIndexToWrite]->captureToFile(mipLevelToWrite, 0, filename, Bitmap::FileFormat::PfmFile, Bitmap::ExportFlags::Uncompressed);
    }

    widget.slider("LightIndex", lightIndexToWrite, 0, static_cast<int>(mpShadowMapTextures.size()) - 1);
    widget.slider("Mip Level", mipLevelToWrite, 0, static_cast<int>(mpShadowMapTextures[0]->getMipCount()) - 1);
}

void DeferredRenderer::setScene(RenderContext* pRenderContext, const Scene::SharedPtr& pScene)
{
    mpScene = pScene;


    if (mpScene)
    {
        // create Program
        Program::Desc desc;
        Shader::DefineList defines;
        desc.addShaderLibrary(kShaderFile).vsEntry("vsMain").gsEntry("gsMain").psEntry("psMain");
        desc.addTypeConformances(mpScene->getTypeConformances());
        desc.setShaderModel(kShaderModel);

        defines.add(pScene->getSceneDefines());

        mpProgram = GraphicsProgram::create(desc, defines);
        mpState->setProgram(mpProgram);

        mpVars = GraphicsVars::create(mpProgram->getReflector());

        const auto numLights = mpScene->getLightCount();
        std::vector<PointLightVertex> pointLights;
        for (const auto& light : mpScene->getLights()) {
            PointLightVertex p;
            const auto& lightData = light->getData();
            p.pos = lightData.posW;
            p.color = lightData.intensity;
            //TODO: radius?
            p.radius = 10;

            pointLights.emplace_back(p);
        }

        const uint32_t vbSize = (uint32_t)(sizeof(PointLightVertex) * numLights);
        auto pVB = Buffer::create(vbSize, Buffer::BindFlags::Vertex, Buffer::CpuAccess::Write, (void*)pointLights.data());
        FALCOR_ASSERT(pVB);

        // create vao
        VertexLayout::SharedPtr pLayout = VertexLayout::create();
        VertexBufferLayout::SharedPtr pBufLayout = VertexBufferLayout::create();
        pBufLayout->addElement("POSITION", 0, ResourceFormat::RGB32Float, 1, 0);               //pos
        pBufLayout->addElement("PSIZE", 3 * sizeof(float), ResourceFormat::R32Float, 1, 1);    //radius
        pBufLayout->addElement("COLOR", 4 * sizeof(float), ResourceFormat::RGB32Float, 1, 2);   //color
        pLayout->addBufferLayout(0, pBufLayout);

        Vao::BufferVec buffers{ pVB };
        mpLightsVao = Vao::create(Vao::Topology::PointList, pLayout, buffers);

        // create draw buffer
        std::vector<DrawArguments> drawLightPoints;

        DrawArguments draw;
        draw.VertexCountPerInstance = 1;
        draw.InstanceCount = numLights;
        draw.StartVertexLocation = 0;
        draw.StartInstanceLocation = 0;
        drawLightPoints.push_back(draw);

        mpDrawBuffer = Buffer::create(sizeof(drawLightPoints[0]) * drawLightPoints.size(), Resource::BindFlags::IndirectArg, Buffer::CpuAccess::None, drawLightPoints.data());
        mpDrawBuffer->setName("Lights Draw Buffer");

        //VSM
        constexpr uint shadowMapWidth = 16384;
        constexpr uint shadowMapHeight = 8192;

        auto test = TiledTexture::create2DTiled(shadowMapWidth, shadowMapHeight, ResourceFormat::R32Float);
        mpShadowMapTextures.emplace_back(test);
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
