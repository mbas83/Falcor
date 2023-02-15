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
#include "DeferredRTShadows.h"

const RenderPass::Info DeferredRTShadows::kInfo{ "DeferredRTShadows", "Insert pass description here." };

// Don't remove this. it's required for hot-reload to function properly
extern "C" FALCOR_API_EXPORT const char* getProjDir()
{
    return PROJECT_DIR;
}

extern "C" FALCOR_API_EXPORT void getPasses(Falcor::RenderPassLibrary & lib)
{
    lib.registerPass(DeferredRTShadows::kInfo, DeferredRTShadows::create);
}

namespace
{
    const char kShaderFile[] = "RenderPasses/DeferredRTShadows/DeferredRTShadows.3d.slang";
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


DeferredRTShadows::SharedPtr DeferredRTShadows::create(RenderContext* pRenderContext, const Dictionary& dict)
{
    SharedPtr pPass = SharedPtr(new DeferredRTShadows());
    return pPass;
}

Dictionary DeferredRTShadows::getScriptingDictionary()
{
    return Dictionary();
}

RenderPassReflection DeferredRTShadows::reflect(const CompileData& compileData)
{
    // Define the required resources here
    RenderPassReflection reflector;
    addRenderPassInputs(reflector, kInputChannels);

    reflector.addOutput(kOut, "Final color of deferred RT shadow renderer");
    reflector.addOutput(kDebug, "debug tex").format(ResourceFormat::RGBA32Float);
    return reflector;
}

void DeferredRTShadows::execute(RenderContext* pRenderContext, const RenderData& renderData)
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
    mpVars["PerFrameCB"]["viewportDims"] = float2(mpFbo->getWidth(), mpFbo->getHeight());

    {
        FALCOR_PROFILE("drawLights");

        mpState->setVao(mpLightsVao);
        mpState->setRasterizerState(mpRsState);
        mpScene->setRaytracingShaderData(pRenderContext, mpVars->getRootVar());
        mpVars->setParameterBlock("gScene", mpScene->getParameterBlock());

        pRenderContext->drawIndirect(mpState.get(), mpVars.get(), 1, mpDrawBuffer.get(), 0, nullptr, 0);
        //pRenderContext->draw(mpState.get(),mpVars.get(),numLights,0);
    }

    if (mSaveDebug)
    {
        auto filename = std::string("D:\\debugTex") + ".pfm";
        renderData[kDebug]->asTexture()->captureToFile(0, 0, filename, Bitmap::FileFormat::PfmFile, Bitmap::ExportFlags::Uncompressed);
        mSaveDebug = false;
    }

    {
        FALCOR_PROFILE("drawAmbientLight");
        executeAmbientLightPass(pRenderContext, renderData);
    }
}

void DeferredRTShadows::renderUI(Gui::Widgets& widget)
{
    if (widget.button("Save Debug Tex"))
    {
        mSaveDebug = true;
    }
}

void DeferredRTShadows::setScene(RenderContext* pRenderContext, const Scene::SharedPtr& pScene)
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

        // Create Program
        mpProgram = GraphicsProgram::create(desc, defines);
        mpState->setProgram(mpProgram);

        mpVars = GraphicsVars::create(mpProgram->getReflector());
    }

}

DeferredRTShadows::DeferredRTShadows() : RenderPass(kInfo)
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
}

void DeferredRTShadows::executeAmbientLightPass(RenderContext* pRenderContext, const RenderData& renderData)
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
