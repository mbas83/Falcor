/*
#include "stdafx.h"
#include "SamplerFeedbackMap.h"


#include "../Utils/d3dx12.h"
#include "Core/API/D3D12/FalcorD3D12.h"
#include "Core/API/Device.h"

namespace Falcor
{
    SamplerFeedbackMap::SharedPtr SamplerFeedbackMap::create2DFeedbackMap(TiledTexture::SharedConstPtr tiledTex)
    {
        FALCOR_ASSERT(tiledTex);
        FALCOR_ASSERT(tiledTex->getType() == Type::Texture2D);
        SamplerFeedbackMap::SharedPtr feedbackMap = SharedPtr(new SamplerFeedbackMap(tiledTex->getWidth(),
            tiledTex->getHeight(), tiledTex->getFormat(), tiledTex->getArraySize(), tiledTex->getMipCount(), tiledTex->getType(), tiledTex->getBindFlags()));
        feedbackMap->init(tiledTex);
        return feedbackMap;
    }

    ShaderResourceView::SharedPtr SamplerFeedbackMap::getSRV()
    {
        FALCOR_UNREACHABLE();
        return nullptr;
    }

    UnorderedAccessView::SharedPtr SamplerFeedbackMap::getUAV()
    {
        return getUAV(0);
    }

    UnorderedAccessView::SharedPtr SamplerFeedbackMap::getUAV(uint32_t mipLevel, uint32_t firstArraySlice, uint32_t arraySize)
    {
        ResourceViewInfo view = ResourceViewInfo(mipLevel, mMipLevels, firstArraySlice, arraySize);

        mUavs[view] = UnorderedAccessView::create(std::static_pointer_cast<SamplerFeedbackMap>(this->shared_from_this()), mipLevel, firstArraySlice, arraySize);

        return mUavs[view];
    }

    SamplerFeedbackMap::SamplerFeedbackMap(uint32_t width, uint32_t height, ResourceFormat format, uint32_t arraySize, uint32_t mipLevels, Type type, BindFlags bindflags)
        : Resource(type, bindflags, 0), mWidth(width), mHeight(height), mMipLevels(mipLevels), mArraySize(arraySize), mFormat(format),
        mTileShape{}, mPackedMipInfo{}, mNumTilesTotal(0)
    {
    }

    void SamplerFeedbackMap::init(TiledTexture::SharedConstPtr tiledTex)
    {
        // get resource tiling
        // query the reserved resource for its tile properties
        {
            UINT subresourceCount = tiledTex->getMipCount();
            mTiling.resize(subresourceCount);
            gpDevice->getApiHandle()->GetResourceTiling(tiledTex->getApiHandle(), &mNumTilesTotal, &mPackedMipInfo, &mTileShape, &subresourceCount, 0, &mTiling[0]);
        }


        D3D12_RESOURCE_DESC1 feedbackMapDesc = CD3DX12_RESOURCE_DESC1::Tex2D(
            DXGI_FORMAT_SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE,
            tiledTex->getWidth(), tiledTex->getHeight(), 1, tiledTex->getMipCount(), tiledTex->getSampleCount());
        feedbackMapDesc.SamplerFeedbackMipRegion = D3D12_MIP_REGION{ GetTileTexelWidth(), GetTileTexelHeight(), 1 };

        // the feedback texture must be in the unordered state to be written, then transitioned to RESOLVE_SOURCE
        feedbackMapDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        const auto heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

        //ResourceHandle mApiHandle;
        FALCOR_D3D_CALL(gpDevice->getApiHandle()->CreateCommittedResource2(
            &heapProperties,
            D3D12_HEAP_FLAG_NONE,
            &feedbackMapDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            nullptr, // clear value
            nullptr,
            IID_PPV_ARGS(&mApiHandle)));
        mApiHandle->SetName(L"SamplerFeedbackMap");

        // CPU heap used for ClearUnorderedAccessView on feedback map
        {
            D3D12_DESCRIPTOR_HEAP_DESC desc = {};
            desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            desc.NumDescriptors = 1; // only need the one for the single feedback map
            desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            FALCOR_D3D_CALL(gpDevice->getApiHandle()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&mUavHeap)));
        }

        // now that both feedback map and paired texture have been created,
        // can create the sampler feedback view
        {
            gpDevice->getApiHandle()->CreateSamplerFeedbackUnorderedAccessView(tiledTex->getApiHandle(), mApiHandle, mUavHeap->GetCPUDescriptorHandleForHeapStart());
        }
        
    }

}
*/
