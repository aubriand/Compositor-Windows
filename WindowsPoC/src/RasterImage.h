#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

struct RasterImage {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> pixels;

    RasterImage() = default;

    RasterImage(uint32_t w, uint32_t h)
        : width(w), height(h), pixels(static_cast<size_t>(w) * h * 4, 0) {}

    bool empty() const {
        return width == 0 || height == 0 || pixels.empty();
    }

    uint8_t* pixel(uint32_t x, uint32_t y) {
        return pixels.data() + (static_cast<size_t>(y) * width + x) * 4;
    }

    const uint8_t* pixel(uint32_t x, uint32_t y) const {
        return pixels.data() + (static_cast<size_t>(y) * width + x) * 4;
    }
};

struct RasterLayer {
    RasterImage image;
    int x = 0;
    int y = 0;
    float opacity = 1.0f;
    bool visible = true;
};

inline RasterImage compositeLayers(uint32_t width, uint32_t height, const std::vector<RasterLayer>& layers) {
    RasterImage output(width, height);

    for (const auto& layer : layers) {
        if (!layer.visible || layer.image.empty() || layer.opacity <= 0.0f) continue;

        const float opacity = std::clamp(layer.opacity, 0.0f, 1.0f);
        for (uint32_t sy = 0; sy < layer.image.height; ++sy) {
            const int dy = layer.y + static_cast<int>(sy);
            if (dy < 0 || dy >= static_cast<int>(height)) continue;

            for (uint32_t sx = 0; sx < layer.image.width; ++sx) {
                const int dx = layer.x + static_cast<int>(sx);
                if (dx < 0 || dx >= static_cast<int>(width)) continue;

                const uint8_t* src = layer.image.pixel(sx, sy);
                uint8_t* dst = output.pixel(static_cast<uint32_t>(dx), static_cast<uint32_t>(dy));

                const float srcAlpha = (src[3] / 255.0f) * opacity;
                if (srcAlpha <= 0.0f) continue;

                const float dstAlpha = dst[3] / 255.0f;
                const float outAlpha = srcAlpha + dstAlpha * (1.0f - srcAlpha);

                if (outAlpha <= 0.0f) {
                    dst[0] = dst[1] = dst[2] = dst[3] = 0;
                    continue;
                }

                for (int channel = 0; channel < 3; ++channel) {
                    const float srcStraight = src[3] ? static_cast<float>(src[channel]) / src[3] : 0.0f;
                    const float dstStraight = dst[3] ? static_cast<float>(dst[channel]) / dst[3] : 0.0f;
                    const float outStraight = (
                        srcStraight * srcAlpha +
                        dstStraight * dstAlpha * (1.0f - srcAlpha)
                    ) / outAlpha;
                    dst[channel] = static_cast<uint8_t>(std::clamp(outStraight * outAlpha * 255.0f, 0.0f, 255.0f));
                }

                dst[3] = static_cast<uint8_t>(std::clamp(outAlpha * 255.0f, 0.0f, 255.0f));
            }
        }
    }

    return output;
}
