#pragma once

#include <vector>

#include "Core/API/D3D12/FalcorD3D12.h"
#include "Core/API/GpuFence.h"
#include "FeedbackTexture.h"
#include "TiledTexture.h"
#include "HeapAllocationManager.h"

#include <chrono>

namespace Falcor {

    class TileUpdateManager
    {
    public:
        using SharedPtr = std::shared_ptr<TileUpdateManager>;
        using SharedConstPtr = std::shared_ptr<const TileUpdateManager>;
        using WeakPtr = std::weak_ptr<TileUpdateManager>;
        using WeakConstPtr = std::weak_ptr<const TileUpdateManager>;

        static SharedPtr createTileUpdateManager(const std::vector<FeedbackTexture::SharedPtr>& feedbackTex, const std::vector<TiledTexture::SharedPtr>& tiledTex, UINT
            heapSize, RenderContext* renderContext);

        // read feedback and add tile request for all shadow maps
        void processFeedback();

        // update tile mapping
        void updateTiles();

        // clear feedback texture
        void clearFeedback() const;

        // return size of heap
        auto getHeapSizeInBytes() const { return mHeapAllocator.getHeapSizeInBytes(); }
        // return currently used memory of tile heap in Bytes
        auto getCurrentlyUsedMemory() const { return mNumTilesMappedToMemory * D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES;}

        // TODO: test copying texture data first
        std::unique_ptr<UINT[]> mTextureReadbackPtr;

    private:

        TileUpdateManager(const std::vector<FeedbackTexture::SharedPtr>& feedbackTextures, const std::vector<TiledTexture::SharedPtr>& shadowMaps, UINT
            heapSizeInTiles, RenderContext* renderContext);

        // copy data from feedback texture to readback buffer
        void resolveFeedback(UINT feedbackIndex, UINT subResourceIndex) const;

        // read feedback tex and add pending tile loads/evictions
        void readAndApplyFeedback(UINT shadowMapIndex);

        // add tile to load
        void AddLoadingUpdate(UINT shadowMapIndex, UINT x, UINT y, UINT subresourceIndex);
        // add tile to evict
        void AddEvictionUpdate(UINT shadowMapIndex, UINT x, UINT y, UINT subresourceIndex);

        // update mappings
        void mapTiles(UINT shadowMapIndex);
        void unmapTiles(UINT shadowMapIndex);

        // clear new mapped tiles to zero
        void clearNewTiles(UINT shadowMapIndex) const;

        // list to track tiles that should be (un)loaded for each shadow map
        std::vector<std::vector< D3D12_TILED_RESOURCE_COORDINATE>> mPendingTiles;
        std::vector<std::vector< D3D12_TILED_RESOURCE_COORDINATE>> mEvictTiles;

        // textures
        std::vector<FeedbackTexture::SharedPtr> mFeedbackTextures;
        std::vector<TiledTexture::SharedPtr> mShadowMaps;

        // number of shadow maps
        UINT mNumShadowMaps;

        // number of used mip levels
        UINT mNumStandardMips;

        // number of tiles currently mapped to memory
        UINT mNumTilesMappedToMemory;

        // readback heaps for every feedback tex and their respective mip level
        std::vector<std::vector<ResourceHandle>> mReadbackBuffers;

        // pointer to the Render Context
        RenderContext* mpRenderContext;

        // layouts of readback buffer (needed for CopyTextureRegion) same for all shadow maps
        std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> mLayouts;

        // fence to sync cpu with copy texture for each feedback tex
        std::vector<GpuFence::SharedPtr> mReadbackFence;

        friend class Device;


        class TileMappingState
        {
        public:

            using TimeStampType = std::chrono::steady_clock::time_point;

            // use all standard mip tiles
            void init(UINT numStandardMips, const std::vector<D3D12_SUBRESOURCE_TILING>& tiling);


            enum class Residency : BYTE
            {
                NotResident = 0,
                Resident = 1,
                Loading = 2,
                Evicting = 3,
            };

            bool isResident(UINT x, UINT y, UINT s) const { return (Residency::Resident == std::get<0>(mStateMap[s][y][x])); }
            bool isNotResident(UINT x, UINT y, UINT s) const { return !isResident(x, y, s); }

            void setResident(UINT x, UINT y, UINT s) { std::get<0>(mStateMap[s][y][x]) = Residency::Resident; }
            void setNotResident(UINT x, UINT y, UINT s) { std::get<0>(mStateMap[s][y][x]) = Residency::NotResident; }

            auto getTimeStamp(UINT x, UINT y, UINT s) const { return std::get<1>(mStateMap[s][y][x]); }
            void setTimeStamp(UINT x, UINT y, UINT s, TimeStampType time) { std::get<1>(mStateMap[s][y][x]) = time; }

            UINT getHeapOffset(UINT x, UINT y, UINT s) const { return std::get<2>(mStateMap[s][y][x]); }
            void setHeapOffset(UINT x, UINT y, UINT s, UINT offset) { std::get<2>(mStateMap[s][y][x]) = offset; }

        private:
            template<typename T> using TileStateRow = std::vector<T>;
            template<typename T> using TileStateMip = std::vector<TileStateRow<T>>;
            template<typename T> using TileMap = std::vector<TileStateMip<T>>;

            // store residency, timestamp and offset (position) in tile heap
            TileMap<std::tuple<Residency, TimeStampType, UINT>> mStateMap;
        };

        // track state of tiles for each shadow map
        std::vector<TileMappingState> mTileMappingStates;

        // time delay for eviction in seconds
        UINT mEvictionTime = 5;

        // manager for tile heap
        HeapAllocationManager mHeapAllocator;

        // tracks which heap tiles are cleared to change state of heap allocation manager
        std::vector<UINT> mHeapOffsetClearList;
    };



}
