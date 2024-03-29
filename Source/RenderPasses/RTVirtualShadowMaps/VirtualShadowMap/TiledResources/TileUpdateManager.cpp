#include "stdafx.h"
#include "TileUpdateManager.h"

#include <glm/gtc/type_ptr.hpp>

#include "../Utils/d3dx12.h"


namespace Falcor {

    TileUpdateManager::SharedPtr TileUpdateManager::createTileUpdateManager(const std::vector<FeedbackTexture::SharedPtr>& feedbackTex, const std::vector<TiledTexture::SharedPtr>& tiledTex, UINT
        heapSize, RenderContext* renderContext, UINT numPreAllocatedMips)
    {
        auto pManager = SharedPtr(new TileUpdateManager(feedbackTex, tiledTex, heapSize, renderContext, numPreAllocatedMips));
        return pManager;
    }

    void TileUpdateManager::processFeedback(UINT startIndex, UINT endIndex)
    {
        for (UINT i = startIndex; i < endIndex; ++i)
        {
            readAndApplyFeedback(i);
        }
    }

    void TileUpdateManager::readAndApplyFeedback(UINT shadowMapIndex)
    {
        // tiled texture: resource barrier  uav -> copy source
        // readback buffer: already in copy dest
        mpRenderContext->resourceBarrier(mFeedbackTextures[shadowMapIndex].get(), Resource::State::CopySource);


        for (UINT mipLevel = 0; mipLevel < mNumUsedMipsForFeedback; ++mipLevel)
        {
            resolveFeedback(shadowMapIndex, mipLevel);
        }


        // read feedback texture and add tiles to pending tile list
        for (UINT subresourceIndex = 0; subresourceIndex < mNumUsedMipsForFeedback; ++subresourceIndex)
        {
            UINT8* pReadbackBufferData = mMappedShadowMaps[shadowMapIndex][subresourceIndex];

            auto tiling = mShadowMaps[shadowMapIndex]->getTiling();
            UINT width = tiling[subresourceIndex].WidthInTiles;
            UINT height = tiling[subresourceIndex].HeightInTiles;
            const auto rowPitch = mLayouts[subresourceIndex].Footprint.RowPitch;
                       

            for (UINT y = 0; y < height; ++y)
            {
                for (UINT x = 0; x < width; ++x)
                {
                    UINT value = pReadbackBufferData[x];
                    //UINT value = mTextureReadbackPtr[y * rowPitch + x];
                    if (value)
                    {
                        // only add tile if not already resident
                        if (mTileMappingStates[shadowMapIndex].isNotResident(x, y, subresourceIndex))
                        {
                            AddLoadingUpdate(shadowMapIndex, x, y, subresourceIndex);
                        }
                        // reset time duration for eviction
                        else
                        {
                            //set current time stamp
                            mTileMappingStates[shadowMapIndex].setTimeStamp(x, y, subresourceIndex, std::chrono::steady_clock::now());
                        }

                    }
                    else
                    {
                        // check possible eviction if tile is resident
                        if (mTileMappingStates[shadowMapIndex].isResident(x, y, subresourceIndex))
                        {
                            auto begin = mTileMappingStates[shadowMapIndex].getTimeStamp(x, y, subresourceIndex);
                            auto end = std::chrono::steady_clock::now();
                            // check if tile should be evicted (not used for mEvictionTime seconds)
                            if (std::chrono::duration_cast<std::chrono::seconds>(end - begin).count() > mEvictionTime)
                            {
                                AddEvictionUpdate(shadowMapIndex, x, y, subresourceIndex);
                            }

                        }
                    }

                }

                pReadbackBufferData += rowPitch;

            }

        }


        // tiled texture: transition from resolve source -> uav (Falcor already inserts UAV barrier between each dispatch)
    }


    // Update Tile Mapping for every shadow map that used feedback
    void TileUpdateManager::updateTiles(UINT startIndex, UINT endIndex)
    {
        for (UINT shadowMapIndex = startIndex; shadowMapIndex < endIndex; ++shadowMapIndex) {

            if (!mPendingTiles[shadowMapIndex].empty())
            {
                // Map pending tiles
                mapTiles(shadowMapIndex);
                mPendingTiles[shadowMapIndex].clear();
            }


            if (!mEvictTiles[shadowMapIndex].empty())
            {
                // clear tiles that are unmapped to zero, so that all unmapped memory is always zero
                clearUnmappedTiles(shadowMapIndex);
                // evict pending tiles
                unmapTiles(shadowMapIndex);
                // update heap manager state
                mHeapAllocator.updateEvictedTiles(mHeapOffsetClearList);
                mHeapOffsetClearList.clear();
                mEvictTiles[shadowMapIndex].clear();
            }
        }
    }

    // clear each feedback map
    void TileUpdateManager::clearFeedback() const
    {
        for (UINT feedbackIndex = 0; feedbackIndex < mNumShadowMaps; ++feedbackIndex)
        {
            for (UINT i = 0; i < mNumUsedMipsForFeedback; ++i) {
                mpRenderContext->clearRtv(mFeedbackTextures[feedbackIndex]->getRTV(i).get(), float4(0));
            }
        }
    }

    void TileUpdateManager::clearUnmappedTiles(UINT shadowMapIndex) const
    {
        auto clear = float4(0);
        std::vector<std::vector<D3D12_RECT>> clearRectangles;
        clearRectangles.resize(mNumUsedMipsForFeedback);
        auto tiling = mShadowMaps[shadowMapIndex]->getTiling();
        auto tileWidth = mShadowMaps[shadowMapIndex]->getTileTexelWidth();
        auto tileHeight = mShadowMaps[shadowMapIndex]->getTileTexelHeight();

        for (const auto& tileCoord : mEvictTiles[shadowMapIndex])
        {
            // create rectangle that contains the tile
            auto left = tileWidth * tileCoord.X;
            auto top = tileHeight * tileCoord.Y;
            auto rect = CD3DX12_RECT(left, top, left + tileWidth, top + tileHeight);
            clearRectangles[tileCoord.Subresource].emplace_back(rect);
        }

        for (UINT i = 0; i < mNumUsedMipsForFeedback; ++i)
        {
            UINT clearRectCount = static_cast<UINT>(clearRectangles[i].size());

            // only use clear if there is at least one new tile in this mip level
            if (clearRectCount) {
                auto pUav = mShadowMaps[shadowMapIndex]->getUAV(i);
                UavHandle uav = pUav->getApiHandle();
                // feedback tex needs to be in unordered access state to be cleared
                mpRenderContext->resourceBarrier(mShadowMaps[shadowMapIndex].get(), Resource::State::UnorderedAccess);

                D3D12DescriptorSet::Layout layout;
                layout.addRange(ShaderResourceType::TextureUav, 0, 1);
                auto pSet = D3D12DescriptorSet::create(gpDevice->getD3D12GpuDescriptorPool(), layout);
                auto dstHandle = pSet->getCpuHandle(0);

                D3D12DescriptorSet::CpuHandle cpuHandle = uav->getCpuHandle(0);
                D3D12DescriptorSet::GpuHandle gpuHandle = pSet->getGpuHandle(0);
                gpDevice->getApiHandle()->CopyDescriptorsSimple(1, dstHandle, cpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

                //
                mpRenderContext->clearUAV(pUav.get(), float4(0));

                //mpRenderContext->getLowLevelData()->getCommandList()->ClearUnorderedAccessViewFloat(gpuHandle, cpuHandle, pUav->getResource()->getApiHandle(), value_ptr(clear), clearRectCount, &clearRectangles[i][0]);
                //mpRenderContext->setPendingCommands(true);

                /* if clearing more than x rects at once -> nvwgf2umx.dll: "Stack cookie instrumentation code detected a stack-based buffer overrun."
                 * also using rects have no effect
                constexpr int numMaxClearsAtOnce = 64;
                int toClear = clearRectCount;
                int currentIndex = 0;
                while (toClear > 0) {
                    UINT clearCount = std::min(numMaxClearsAtOnce, toClear);
                    mpRenderContext->getLowLevelData()->getCommandList()->ClearUnorderedAccessViewFloat(gpuHandle, cpuHandle, pUav->getResource()->getApiHandle(), value_ptr(clear), clearCount, &clearRectangles[i][currentIndex]);
                    toClear -= clearCount;
                    currentIndex += clearCount;
                }*/
            }
        }

    }


    TileUpdateManager::TileUpdateManager(const std::vector<FeedbackTexture::SharedPtr>& feedbackTextures, const std::vector<TiledTexture::SharedPtr>& shadowMaps, UINT
        heapSizeInTiles, RenderContext* renderContext, uint numPreAlloactedMips) :
        mFeedbackTextures(feedbackTextures), mShadowMaps(shadowMaps), mNumShadowMaps(static_cast<UINT>(shadowMaps.size())),
        mNumTilesMappedToMemory(0), mpRenderContext(renderContext), mHeapAllocator(heapSizeInTiles)
    {
        auto tiling = mShadowMaps[0]->getTiling();

        mNumStandardMips = mShadowMaps[0]->getPackedMipInfo().NumStandardMips;

        mNumUsedMipsForFeedback = mNumStandardMips - numPreAlloactedMips;

        mTileMappingStates.resize(mNumShadowMaps);
        for (auto& states : mTileMappingStates)
        {
            states.init(mNumUsedMipsForFeedback, tiling);
        }

        mLayouts.resize(mNumUsedMipsForFeedback);
        mPendingTiles.resize(mNumShadowMaps);
        mEvictTiles.resize(mNumShadowMaps);
        mReadbackBuffers.resize(mNumShadowMaps);
        for (auto& buf : mReadbackBuffers)
        {
            buf.resize(mNumUsedMipsForFeedback);
        }

        // create readback heaps for every feedback tex
        for (UINT feedbackIndex = 0; feedbackIndex < mNumShadowMaps; ++feedbackIndex) {

            // create readback heap for every standard mip level
            for (UINT subresourceIndex = 0; subresourceIndex < mNumUsedMipsForFeedback; ++subresourceIndex)
            {
                const auto srcCopyDesc = mFeedbackTextures[feedbackIndex]->getApiHandle()->GetDesc();
                UINT64 totalResourceSize = 0;
                UINT64 fpRowPitch = 0;
                UINT fpRowCount = 0;
                D3D12_PLACED_SUBRESOURCE_FOOTPRINT mipFootprint;
                // Get the rowcount, pitch and size of the current mip
                gpDevice->getApiHandle()->GetCopyableFootprints(&srcCopyDesc, subresourceIndex, 1, 0,
                    &mipFootprint,
                    &fpRowCount,
                    &fpRowPitch,
                    &totalResourceSize);

                D3D12_PLACED_SUBRESOURCE_FOOTPRINT bufferFootprint = {};
                bufferFootprint.Footprint.Width = mipFootprint.Footprint.Width;
                bufferFootprint.Footprint.Height = mipFootprint.Footprint.Height;
                bufferFootprint.Footprint.Depth = 1;
                // rowpitch must be mutiple of 256
                bufferFootprint.Footprint.RowPitch = static_cast<UINT>((fpRowPitch + 255) & ~0xFFu);
                bufferFootprint.Footprint.Format = mipFootprint.Footprint.Format;

                // save buffer footprint
                mLayouts[subresourceIndex] = bufferFootprint;

                D3D12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(static_cast<UINT64>(bufferFootprint.Footprint.RowPitch) * tiling[subresourceIndex].HeightInTiles);
                const auto resolvedHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);

                FALCOR_D3D_CALL(gpDevice->getApiHandle()->CreateCommittedResource(
                    &resolvedHeapProperties,
                    D3D12_HEAP_FLAG_NONE,
                    &bufferDesc,
                    D3D12_RESOURCE_STATE_COPY_DEST,
                    nullptr,
                    IID_PPV_ARGS(&mReadbackBuffers[feedbackIndex][subresourceIndex])));
                mReadbackBuffers[feedbackIndex][subresourceIndex]->SetName(L"ReadbackBuffer");
            }
        }

        // create fences
        mReadbackFence.resize(mNumShadowMaps);
        for (auto& fence : mReadbackFence)
        {
            fence = GpuFence::create();
        }


        // Map packed mips for each shadow map
        for (uint shadowMapIndex = 0; shadowMapIndex < mNumShadowMaps; ++shadowMapIndex) {

            // start at first packed mip subresource, region size is then number of packed mips
            auto numPackedMipTiles = mShadowMaps[shadowMapIndex]->getPackedMipInfo().NumTilesForPackedMips;
            if (numPackedMipTiles <= 0)
            {
                continue;
            }

            D3D12_TILED_RESOURCE_COORDINATE packedMipStartCoordinate{ 0,0,0,mNumStandardMips };
            D3D12_TILE_REGION_SIZE packedMipSize{ numPackedMipTiles, false, 0,0,0 };

            auto packedMipHeapIndices = mHeapAllocator.getFreeHeapIndices(numPackedMipTiles);
            auto oneTileCount = std::vector<UINT>(numPackedMipTiles, 1);
            auto rangeFlags = std::vector<D3D12_TILE_RANGE_FLAGS>(numPackedMipTiles, D3D12_TILE_RANGE_FLAG_NONE);

            mpRenderContext->getLowLevelData()->getCommandQueue()->UpdateTileMappings(mShadowMaps[shadowMapIndex]->getApiHandle(),
                1,
                &packedMipStartCoordinate,
                &packedMipSize,
                mHeapAllocator.getHeapHandle(),
                numPackedMipTiles,
                &rangeFlags[0],
                &packedMipHeapIndices[0],
                &oneTileCount[0],
                D3D12_TILE_MAPPING_FLAG_NONE);

            // clear all packed mip tiles
            for (UINT i = mNumStandardMips; i < mShadowMaps[shadowMapIndex]->getMipCount(); ++i)
            {
                mpRenderContext->clearUAV(mShadowMaps[shadowMapIndex]->getUAV(i).get(), float4(0));
            }

            // count mapped mip tiles
            mNumTilesMappedToMemory += numPackedMipTiles;
        }


        mMappedShadowMaps.resize(mNumShadowMaps);
        // get mapping for each shadow map
        for (size_t shadowMapIndex = 0; shadowMapIndex < mNumShadowMaps; ++shadowMapIndex)
        {
            for (UINT subresourceIndex = 0; subresourceIndex < mNumUsedMipsForFeedback; ++subresourceIndex)
            {
                UINT8* pReadbackBufferData = nullptr;

                FALCOR_D3D_CALL(mReadbackBuffers[shadowMapIndex][subresourceIndex]->Map(0, nullptr, reinterpret_cast<void**>(&pReadbackBufferData)));
                mMappedShadowMaps[shadowMapIndex][subresourceIndex] = pReadbackBufferData;
            }
        }

        if (numPreAlloactedMips)
        {
            preAllocateMips(numPreAlloactedMips);
        }

    }

    void TileUpdateManager::resolveFeedback(UINT feedbackIndex, UINT subResourceIndex) const
    {
        auto copyDest = mReadbackBuffers[feedbackIndex][subResourceIndex];

        // destination is a buffer and uses footprint
        auto dstData = CD3DX12_TEXTURE_COPY_LOCATION(copyDest, mLayouts[subResourceIndex]);
        // source is feedback data and uses mipmap index
        auto srcData = CD3DX12_TEXTURE_COPY_LOCATION(mFeedbackTextures[feedbackIndex]->getApiHandle(), subResourceIndex);

        mpRenderContext->getLowLevelData()->getCommandList()->CopyTextureRegion(&dstData, 0, 0, 0, &srcData, nullptr);
    }

    void TileUpdateManager::AddLoadingUpdate(UINT shadowMapIndex, UINT x, UINT y, UINT subresourceIndex)
    {
        mPendingTiles[shadowMapIndex].emplace_back(D3D12_TILED_RESOURCE_COORDINATE{ x,y,0,subresourceIndex });
        mTileMappingStates[shadowMapIndex].setResident(x, y, subresourceIndex);
        // set current time stamp
        mTileMappingStates[shadowMapIndex].setTimeStamp(x, y, subresourceIndex, std::chrono::steady_clock::now());
    }

    void TileUpdateManager::AddEvictionUpdate(UINT shadowMapIndex, UINT x, UINT y, UINT subresourceIndex)
    {
        mEvictTiles[shadowMapIndex].emplace_back(D3D12_TILED_RESOURCE_COORDINATE{ x,y,0,subresourceIndex });
        mTileMappingStates[shadowMapIndex].setNotResident(x, y, subresourceIndex);
        // remove from heap tile manager state
        mHeapOffsetClearList.push_back(mTileMappingStates[shadowMapIndex].getHeapOffset(x, y, subresourceIndex));
    }

    void TileUpdateManager::mapTiles(UINT shadowMapIndex)
    {
        // map pending tiles one for one
        auto numTiles = static_cast<UINT>(mPendingTiles[shadowMapIndex].size());
        auto rangeFlag = std::vector<D3D12_TILE_RANGE_FLAGS>(numTiles, D3D12_TILE_RANGE_FLAG_NONE);
        auto oneTileCount = std::vector<UINT>(numTiles, 1);

        // get offset of free heap tiles and updates state of the heap allocator
        auto heapOffsets = mHeapAllocator.getFreeHeapIndices(numTiles);

        // set heap offset in tile state
        for (UINT i = 0; i < numTiles; ++i)
        {
            mTileMappingStates[shadowMapIndex].setHeapOffset(mPendingTiles[shadowMapIndex][i].X, mPendingTiles[shadowMapIndex][i].Y, mPendingTiles[shadowMapIndex][i].Subresource, heapOffsets[i]);
        }

        mpRenderContext->getLowLevelData()->getCommandQueue()->UpdateTileMappings(mShadowMaps[shadowMapIndex]->getApiHandle(),
            numTiles,
            &mPendingTiles[shadowMapIndex][0],
            nullptr, // defaults to size 1 tile
            mHeapAllocator.getHeapHandle(),
            numTiles,
            &rangeFlag[0],
            &heapOffsets[0],
            &oneTileCount[0],
            D3D12_TILE_MAPPING_FLAG_NONE);

        //count mapped tiles
        mNumTilesMappedToMemory += numTiles;
    }

    void TileUpdateManager::unmapTiles(UINT shadowMapIndex)
    {
        // unmap to be evicted tiles
        auto numTiles = static_cast<UINT>(mEvictTiles[shadowMapIndex].size());
        auto rangeFlag = D3D12_TILE_RANGE_FLAG_NULL;

        mpRenderContext->getLowLevelData()->getCommandQueue()->UpdateTileMappings(mShadowMaps[shadowMapIndex]->getApiHandle(),
            numTiles,
            &mEvictTiles[shadowMapIndex][0],
            nullptr, // defaults to size 1 tile
            nullptr,
            1,
            &rangeFlag,
            nullptr,
            nullptr,
            D3D12_TILE_MAPPING_FLAG_NONE);

        //count unmapped tiles
        mNumTilesMappedToMemory -= numTiles;
    }


    void TileUpdateManager::TileMappingState::init(UINT numStandardMips, const std::vector<D3D12_SUBRESOURCE_TILING>& tiling)
    {
        mStateMap.resize(numStandardMips);

        for (UINT mipLevel = 0; mipLevel < numStandardMips; mipLevel++)
        {
            UINT width = tiling[mipLevel].WidthInTiles;
            UINT height = tiling[mipLevel].HeightInTiles;

            mStateMap[mipLevel].resize(height);

            for (auto& row : mStateMap[mipLevel])
            {
                row.assign(width, std::make_tuple(Residency::NotResident, std::chrono::steady_clock::now(), 0));
            }

        }

    }

    void TileUpdateManager::preAllocateMips(UINT numPreAllocatedMips)
    {
        // allocate numPreAllocatedMips for each texture
        for (size_t shadowMapIndex = 0; shadowMapIndex < mNumShadowMaps; ++shadowMapIndex)
        {
            for (UINT mipIndex = 0; mipIndex < numPreAllocatedMips; ++mipIndex)
            {
                UINT mipLevel = mNumStandardMips - mipIndex - 1;

                //mipIndex 0 -> highest mip (lowest resolution)
                // get offset of free heap tiles and updates state of the heap allocator
                auto tiling = mShadowMaps[shadowMapIndex]->getTiling()[mipLevel];
                UINT numTiles = tiling.HeightInTiles * tiling.HeightInTiles;
                auto heapOffsets = mHeapAllocator.getFreeHeapIndices(numTiles);

                D3D12_TILED_RESOURCE_COORDINATE mipStartCoordinate{ 0,0,0,mipLevel };

                auto oneTileCount = std::vector<UINT>(numTiles, 1);
                auto rangeFlags = std::vector<D3D12_TILE_RANGE_FLAGS>(numTiles, D3D12_TILE_RANGE_FLAG_NONE);
                D3D12_TILE_REGION_SIZE mipRegionSize{ numTiles, false, 0,0,0 };

                mpRenderContext->getLowLevelData()->getCommandQueue()->UpdateTileMappings(mShadowMaps[shadowMapIndex]->getApiHandle(),
                    1,
                    &mipStartCoordinate,
                    &mipRegionSize,
                    mHeapAllocator.getHeapHandle(),
                    numTiles,
                    &rangeFlags[0],
                    &heapOffsets[0],
                    &oneTileCount[0], //map a tile to a specified location in heap (can be the case that following tiles not consecutively in heap)
                    D3D12_TILE_MAPPING_FLAG_NONE);

                //count mapped tiles
                mNumTilesMappedToMemory += numTiles;


            }
        }
    }

}
