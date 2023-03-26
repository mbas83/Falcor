/*
#pragma once

#include <memory>
#include "Core/API/Resource.h"
#include "Core/API/Formats.h"

#include "TiledTexture.h"

namespace Falcor {

    class SamplerFeedbackMap : public Resource
    {
        class Device;

    public:
        using SharedPtr = std::shared_ptr<SamplerFeedbackMap>;
        using SharedConstPtr = std::shared_ptr<const SamplerFeedbackMap>;
        using WeakPtr = std::weak_ptr<SamplerFeedbackMap>;
        using WeakConstPtr = std::weak_ptr<const SamplerFeedbackMap>;


        static SharedPtr create2DFeedbackMap(TiledTexture::SharedConstPtr tiledTex);

        virtual ShaderResourceView::SharedPtr getSRV() override;
        virtual UnorderedAccessView::SharedPtr getUAV() override;

        UnorderedAccessView::SharedPtr getUAV(uint32_t mipLevel, uint32_t firstArraySlice = 0, uint32_t arraySize = 1);

        DescriptorHeapHandle getUavHeap() const { return mUavHeap; }

    private:
        SamplerFeedbackMap(uint32_t width, uint32_t height, ResourceFormat format, uint32_t arraySize, uint32_t mipLevels, Type type, BindFlags bindflags);

        void init(TiledTexture::SharedConstPtr tiledTex);


        UINT GetTileTexelWidth() const { return mTileShape.WidthInTexels; }
        UINT GetTileTexelHeight() const { return mTileShape.HeightInTexels; }


        uint32_t mWidth = 0;
        uint32_t mHeight = 0;
        uint32_t mMipLevels = 0;
        uint32_t mArraySize = 0;
        ResourceFormat mFormat = ResourceFormat::Unknown;

        // Tiles
        D3D12_TILE_SHAPE mTileShape;
        D3D12_PACKED_MIP_INFO mPackedMipInfo;
        UINT mNumTilesTotal;
        std::vector<D3D12_SUBRESOURCE_TILING> mTiling;

        // handle to descriptor heap for uav descriptor
        DescriptorHeapHandle mUavHeap;

        friend class Device;
    };
}
*/
