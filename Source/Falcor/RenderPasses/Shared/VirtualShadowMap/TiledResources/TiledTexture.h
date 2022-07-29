#pragma once

#include <memory>

#include "Core/API/Formats.h"
#include "Core/API/Texture.h"

namespace Falcor
{

    class TiledTexture : public Texture
    {
    public:
        using SharedPtr = std::shared_ptr<TiledTexture>;
        using SharedConstPtr = std::shared_ptr<const TiledTexture>;
        using WeakPtr = std::weak_ptr<TiledTexture>;
        using WeakConstPtr = std::weak_ptr<const TiledTexture>;

        ~TiledTexture() override;

        static SharedPtr create2DTiled(uint32_t width, uint32_t height, ResourceFormat format, uint32_t arraySize = 1, uint32_t mipLevels = kMaxPossible);

        UINT getTileTexelWidth() const { return mTileShape.WidthInTexels; }
        UINT getTileTexelHeight() const { return mTileShape.HeightInTexels; }

        UINT getNumTilesTotal() const { return  mNumTilesTotal; }

        D3D12_PACKED_MIP_INFO getPackedMipInfo() const { return mPackedMipInfo; }
        std::vector<D3D12_SUBRESOURCE_TILING> getTiling() const { return mTiling; }

    protected:
        TiledTexture(uint32_t width, uint32_t height, uint32_t depth, uint32_t arraySize, uint32_t mipLevels, uint32_t sampleCount, ResourceFormat format, Type type, BindFlags bindFlags);

        void init();

        // Tile Info
        D3D12_TILE_SHAPE mTileShape;
        D3D12_PACKED_MIP_INFO mPackedMipInfo;
        UINT mNumTilesTotal;
        std::vector<D3D12_SUBRESOURCE_TILING> mTiling;

    };

}
