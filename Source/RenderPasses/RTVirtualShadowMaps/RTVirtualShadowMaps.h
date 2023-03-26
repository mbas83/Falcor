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
#pragma once
#include "Falcor.h"

#include "VirtualShadowMap/TiledResources/TiledTexture.h"
#include "VirtualShadowMap/TiledResources/FeedbackTexture.h"
#include "VirtualShadowMap/TiledResources/TileUpdateManager.h"

using namespace Falcor;

class RTVirtualShadowMaps : public RenderPass
{
public:
    using SharedPtr = std::shared_ptr<RTVirtualShadowMaps>;

    static const Info kInfo;

    /** Create a new render pass object.
        \param[in] pRenderContext The render context.
        \param[in] dict Dictionary of serialized parameters.
        \return A new object, or an exception is thrown if creation failed.
    */
    static SharedPtr create(RenderContext* pRenderContext = nullptr, const Dictionary& dict = {});

    virtual Dictionary getScriptingDictionary() override;
    virtual RenderPassReflection reflect(const CompileData& compileData) override;
    virtual void compile(RenderContext* pRenderContext, const CompileData& compileData) override {}
    virtual void execute(RenderContext* pRenderContext, const RenderData& renderData) override;
    virtual void renderUI(Gui::Widgets& widget) override;
    virtual void setScene(RenderContext* pRenderContext, const Scene::SharedPtr& pScene) override;
    virtual bool onMouseEvent(const MouseEvent& mouseEvent) override { return false; }
    virtual bool onKeyEvent(const KeyboardEvent& keyEvent) override { return false; }

    const float getCurrentMemoryUsage() const;

    // use in python script 
    void startCaptureMemoryUsage() { mBenchmarkMemoryList.reserve(10000);  mRecordMemoryUsage = true; };
    void endCaptureMemoryUsage();
    void outputMemoryUsage();

private:
    RTVirtualShadowMaps();

    GraphicsState::SharedPtr mpState;
    DepthStencilState::SharedPtr mpDsNoDepthWrite;
    Scene::SharedPtr mpScene;
    GraphicsVars::SharedPtr mpVars;
    RasterizerState::SharedPtr mpRsState;
    GraphicsProgram::SharedPtr mpProgram;
    Fbo::SharedPtr mpFbo;
    Vao::SharedPtr mpLightsVao;
    Buffer::SharedPtr mpDrawBuffer;


    // VSM
    // reserved shadow map textures
    std::vector<TiledTexture::SharedPtr> mpShadowMapTextures;

    // feedback textures for shadow maps
    std::vector<FeedbackTexture::SharedPtr> mpFeedbackTextures;

    // manage tile updates
    TileUpdateManager::SharedPtr mpTileUpdateManager;

    // color for mip levels
    Texture::SharedPtr mipColorTex;

    // for writing texture to file
    int lightIndexToWrite = 0;
    int mipLevelToWrite = 0;

    uint numLights;
    uint numShadowMips;
    uint numStandardMips;

    bool mSaveDebug = false;

    // name of texture in shader for binding
    std::vector<std::vector<UnorderedAccessView::SharedPtr>> mpShadowMapUAVs;
    std::vector<std::vector<UnorderedAccessView::SharedPtr>> mpFeedbackMapUAVs;

    // generate all needed uavs beforehand
    void preGenerateUAVS();


    // abmient light pass
    FullScreenPass::SharedPtr mpAmbientLightPass;
    FullScreenPass::SharedPtr mpRenderShadowTexturePass;

    void executeAmbientLightPass(RenderContext* pRenderContext, const RenderData& renderData) const;
    void executeDrawShadowMap(RenderContext* pRenderContext, const RenderData& renderData) const;

    uint mRenderSMIndex = 0, mRenderMipLevel = 0;
    bool mRenderShadowMap = false;
    Sampler::SharedPtr mpRenderSMSampler;

    uint mFrameCount = 0;

    std::array<float4, 6> mipBiasVals;
    // only process x textures for feedback
    UINT mStartIndexFeedback, mEndIndexFeedback;
    UINT mNumFeedbackReadsPerFrame = 10;

    // Benchmarking

    std::vector<float> mBenchmarkMemoryList;

    std::string mBenchmarkMemoryOutputFilePath;


    bool mRecordMemoryUsage = false;

    // append current memory usage to list
    void recordMemoryUsage();
};

