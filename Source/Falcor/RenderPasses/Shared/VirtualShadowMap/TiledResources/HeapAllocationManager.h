#pragma once

namespace Falcor
{

    class HeapAllocationManager
    {
    public:

        // creates tile heap with specified size
        HeapAllocationManager(UINT heapsizeInTiles);

        ~HeapAllocationManager();

        // get indices for free heap tiles and updates this heap tile state
        [[nodiscard]] std::vector<UINT> getFreeHeapIndices(UINT numTiles);

        void updateEvictedTiles(const std::vector<UINT>& evictedTileOffsets);

        HeapHandle getHeapHandle() { return mTileHeap; }

    private:

        // store indices of currently used heap tiles
        std::vector<bool> mUsedHeapTiles;
        UINT mHeapSize;

        HeapHandle mTileHeap;
    };

}
