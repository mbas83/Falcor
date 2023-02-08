#include "stdafx.h"
#include "FeedbackTexture.h"

#include "../Utils/d3dx12.h"

namespace Falcor
{



    FeedbackTexture::~FeedbackTexture()
    {
        gpDevice->releaseResource(mApiHandle);
    }


    FeedbackTexture::SharedPtr FeedbackTexture::createFeedbackTexture(uint32_t pairedTexwidth, uint32_t pairedTexHeight, uint32_t tileWidth, uint32_t tileHeight, uint32_t mipLevels)
    {
        // Tile Factor = number of tiles in tiled tex that correspond to one texel in Feedback Texture
        auto width = pairedTexwidth / (tileWidth);
        auto height = pairedTexHeight / (tileHeight);

        FeedbackTexture::SharedPtr pTexture = SharedPtr(new FeedbackTexture(width, height, 1, 1, mipLevels, 1, ResourceFormat::R8Uint, Type::Texture2D, BindFlags::UnorderedAccess | BindFlags::ShaderResource | BindFlags::RenderTarget, tileWidth, tileHeight));
        pTexture->init();
        return pTexture;
    }

    FeedbackTexture::FeedbackTexture(uint32_t width, uint32_t height, uint32_t depth, uint32_t arraySize, uint32_t mipLevels, uint32_t sampleCount, ResourceFormat format, Type type, BindFlags bindFlags, uint32_t tileWidth, uint32_t tileHeight)
        :Texture(width, height, depth, arraySize, mipLevels, sampleCount, format, type, bindFlags), mTileWidth(tileWidth), mTileHeight(tileHeight)
    {
    }

    void FeedbackTexture::init()
    {
        D3D12_RESOURCE_DESC desc = {};

        desc.MipLevels = mMipLevels;
        desc.Format = getDxgiFormat(mFormat);
        desc.Width = mWidth;
        desc.Height = mHeight;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Alignment = 0;
        desc.DepthOrArraySize = 1;

        const auto heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

        D3D12_HEAP_FLAGS heapFlags = is_set(mBindFlags, ResourceBindFlags::Shared) ? D3D12_HEAP_FLAG_SHARED : D3D12_HEAP_FLAG_NONE;
        FALCOR_D3D_CALL(gpDevice->getApiHandle()->CreateCommittedResource(&heapProperties, heapFlags, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&mApiHandle)));

        FALCOR_ASSERT(mApiHandle);
        mApiHandle->SetName(L"FeedbackTexture");
    }

}
