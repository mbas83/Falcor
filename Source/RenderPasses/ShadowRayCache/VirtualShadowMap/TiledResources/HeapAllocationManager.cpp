#include "stdafx.h"
#include "HeapAllocationManager.h"

#include "../Utils/d3dx12.h"

namespace Falcor {


    HeapAllocationManager::HeapAllocationManager(UINT heapsizeInTiles) : mUsedHeapTiles(heapsizeInTiles, false), mHeapSize(heapsizeInTiles)
    {
        const UINT heapSize = heapsizeInTiles * D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES;

        CD3DX12_HEAP_DESC heapDesc(heapSize, D3D12_HEAP_TYPE_DEFAULT, 0, D3D12_HEAP_FLAG_DENY_BUFFERS | D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES);

        FALCOR_D3D_CALL(gpDevice->getApiHandle()->CreateHeap(&heapDesc, IID_PPV_ARGS(&mTileHeap)));
        mTileHeap->SetName(L"TileHeap");
    }

    std::vector<UINT> HeapAllocationManager::getFreeHeapIndices(UINT numTiles)
    {
        std::vector<UINT> indices;
        uint currentHeapIndex = 0;


        while (numTiles > 0)
        {
            if (mUsedHeapTiles[currentHeapIndex])
            {
                // heap tile already in use, check next
                currentHeapIndex++;

                assert(currentHeapIndex < mHeapSize);
            }
            else
            {
                indices.push_back(currentHeapIndex);
                mUsedHeapTiles[currentHeapIndex] = true;
                numTiles--;
            }

        }

        return indices;
    }

    void HeapAllocationManager::updateEvictedTiles(const std::vector<UINT>& evictedTileOffsets)
    {
        for (const auto offset : evictedTileOffsets)
        {
            mUsedHeapTiles[offset] = false;
        }
    }


    HeapAllocationManager::~HeapAllocationManager()
    {
        gpDevice->releaseResource(mTileHeap);
    }
}
