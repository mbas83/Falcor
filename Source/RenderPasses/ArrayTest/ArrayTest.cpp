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
#include "ArrayTest.h"

const RenderPass::Info ArrayTest::kInfo{ "ArrayTest", "Insert pass description here." };

namespace
{

    const char kShaderFile[] = "RenderPasses/ArrayTest/ArrayTest.rt.slang";
    const char kShaderModel[] = "6_5";

    const uint32_t kMaxPayloadSizeBytes = 164;
    const uint32_t kMaxRecursionDepth = 2;

    const ChannelList kOutputChannels =
    {
        { "output",   "gOutput","Output color",    true /* optional */, ResourceFormat::RGBA32Float },
    };


}

// Don't remove this. it's required for hot-reload to function properly
extern "C" FALCOR_API_EXPORT const char* getProjDir()
{
    return PROJECT_DIR;
}

extern "C" FALCOR_API_EXPORT void getPasses(Falcor::RenderPassLibrary & lib)
{
    lib.registerPass(ArrayTest::kInfo, ArrayTest::create);
}

ArrayTest::SharedPtr ArrayTest::create(RenderContext* pRenderContext, const Dictionary& dict)
{
    SharedPtr pPass = SharedPtr(new ArrayTest());
    return pPass;
}

Dictionary ArrayTest::getScriptingDictionary()
{
    return Dictionary();
}

RenderPassReflection ArrayTest::reflect(const CompileData& compileData)
{
    // Define the required resources here
    RenderPassReflection reflector;
    addRenderPassOutputs(reflector, kOutputChannels);

    return reflector;
}

void ArrayTest::execute(RenderContext* pRenderContext, const RenderData& renderData)
{
    for (const auto& channel : kOutputChannels)
    {
        pRenderContext->clearTexture(renderData[channel.name]->asTexture().get());
    }

    if (!mpScene) return;

    if (!mpVars) prepareVars();

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


    // bind texs into array
    for (uint i = 0; i < mpTexture->getMipCount(); ++i)
    {
        mpVars["gWriteTestTextures"][i].setUav(mpTexture->getUAV(i));
    }

    mpScene->raytrace(pRenderContext, mpProgram.get(), mpVars, uint3(targetDim, 1));
}

void ArrayTest::renderUI(Gui::Widgets& widget)
{
    if (widget.button("Save Tiled Texture Mip Level"))
    {
        auto filename = std::string("D:\\tiledTex_mip") + std::to_string(mipLevelToWrite) + ".pfm";
        mpTexture->captureToFile(mipLevelToWrite, 0, filename, Bitmap::FileFormat::PfmFile, Bitmap::ExportFlags::Uncompressed);
    }

    widget.slider("Mip Level", mipLevelToWrite, 0, static_cast<int>(mpTexture->getMipCount()) - 1);
}

void ArrayTest::setScene(RenderContext* pRenderContext, const Scene::SharedPtr& pScene)
{
    mpScene = pScene;

    recreateVars();

    mpTexture = Texture::create2D(4096, 4096, ResourceFormat::R32Float, 1, maxMips, nullptr, ResourceBindFlags::UnorderedAccess | ResourceBindFlags::ShaderResource);

    for (uint i = 0; i < mpTexture->getMipCount(); ++i)
    {
        auto test = mpTexture->getUAV(i);
    }

    //create test tex with mips
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

        auto geometryIDs = pScene->getGeometryIDs(Scene::GeometryType::TriangleMesh, materialType);

        sbt->setHitGroup(0, geometryIDs, primaryShaderID);
        sbt->setHitGroup(1, geometryIDs, shadowShaderID);
    }
    Program::DefineList defines;
    defines.add(mpScene->getSceneDefines());
    defines.add("MIPCOUNT", std::to_string(mpTexture->getMipCount()));

    mpProgram = RtProgram::create(progDesc, defines);

    assert(mpProgram);


}

void ArrayTest::prepareVars()
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
