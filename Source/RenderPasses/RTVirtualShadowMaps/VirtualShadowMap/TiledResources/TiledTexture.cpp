#include "stdafx.h"
#include "TiledTexture.h"   

#include "../Utils/d3dx12.h"
#include "Core/API/Device.h"
#include "Core/API/D3D12/FalcorD3D12.h"


namespace Falcor
{

    TiledTexture::TiledTexture(uint32_t width, uint32_t height, uint32_t depth, uint32_t arraySize, uint32_t mipLevels,
        uint32_t sampleCount, ResourceFormat format, Type type, BindFlags bindFlags) : Texture(width, height, depth, arraySize, mipLevels, sampleCount, format, type, bindFlags),
        mTileShape{}, mPackedMipInfo{}, mNumTilesTotal(0)
    {
    }



    TiledTexture::~TiledTexture()
    {
        gpDevice->releaseResource(mApiHandle);
    }

    TiledTexture::SharedPtr TiledTexture::create2DTiled(uint32_t width, uint32_t height, ResourceFormat format, uint32_t arraySize, uint32_t mipLevels)
    {
        TiledTexture::SharedPtr pTexture = SharedPtr(new TiledTexture(width, height, 1, arraySize, mipLevels, 1, format, Type::Texture2D, BindFlags::UnorderedAccess | BindFlags::ShaderResource));
        pTexture->init();
        return pTexture;
    }


    void TiledTexture::init()
    {
        D3D12_RESOURCE_DESC desc = {};

        desc.MipLevels = mMipLevels;
        desc.Format = getDxgiFormat(mFormat);
        desc.Width = align_to(getFormatWidthCompressionRatio(mFormat), mWidth);
        desc.Height = align_to(getFormatHeightCompressionRatio(mFormat), mHeight);
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;
        desc.Alignment = 0;
        desc.DepthOrArraySize = 1;

        D3D12_CLEAR_VALUE* pClearVal = nullptr;
        D3D12_HEAP_FLAGS heapFlags = is_set(mBindFlags, ResourceBindFlags::Shared) ? D3D12_HEAP_FLAG_SHARED : D3D12_HEAP_FLAG_NONE;

        FALCOR_D3D_CALL(gpDevice->getApiHandle()->CreateReservedResource(&desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&mApiHandle)));
        FALCOR_ASSERT(mApiHandle);
        mApiHandle->SetName(L"TiledTexture");

        // get resource tiling
        // query the reserved resource for its tile properties
        UINT subresourceCount = this->getMipCount();
        mTiling.resize(subresourceCount);
        gpDevice->getApiHandle()->GetResourceTiling(mApiHandle, &mNumTilesTotal, &mPackedMipInfo, &mTileShape, &subresourceCount, 0, &mTiling[0]);

    }

}
