#pragma once

#include <memory>

#include "Core/API/Formats.h"
#include "Core/API/Texture.h"

namespace Falcor
{

    class FeedbackTexture : public Texture
    {
    public:
        using SharedPtr = std::shared_ptr<FeedbackTexture>;
        using SharedConstPtr = std::shared_ptr<const FeedbackTexture>;
        using WeakPtr = std::weak_ptr<FeedbackTexture>;
        using WeakConstPtr = std::weak_ptr<const FeedbackTexture>;

        ~FeedbackTexture() override;

        static SharedPtr createFeedbackTexture(uint32_t pairedTexwidth, uint32_t pairedTexHeight, uint32_t tileWidth, uint32_t tileHeight, uint32_t mipLevels = kMaxPossible);

        UINT getTileWidth() const { return mTileWidth; }
        UINT getTileHeight() const { return mTileHeight; }

    protected:

        FeedbackTexture(uint32_t width, uint32_t height, uint32_t depth, uint32_t arraySize, uint32_t mipLevels, uint32_t sampleCount, ResourceFormat format, Type type, BindFlags bindFlags, uint32_t tileWidth, uint32_t tileHeight);

        void init();

        UINT mTileWidth;
        UINT mTileHeight;

    };

}
