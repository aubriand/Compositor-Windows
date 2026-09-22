#pragma once

#include "RasterImage.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Platform-neutral counterparts for the portable subset of Compositor's
// CanvasDocument / ImageLayer / LayerTransform model. No Win32 or Apple types.
enum class LayerSampling { Nearest, Smooth, High };
enum class LayerBlendMode { Normal };

struct LayerTransformModel {
    double x = 0;
    double y = 0;
    double width = 1;
    double height = 1;
    double rotation = 0;
    bool flipX = false;
    bool flipY = false;
    LayerSampling sampling = LayerSampling::High;

    bool valid() const {
        return std::isfinite(x) && std::isfinite(y) && std::isfinite(width) &&
               std::isfinite(height) && std::isfinite(rotation) &&
               width >= 1 && width <= 300000 && height >= 1 && height <= 300000 &&
               std::abs(x) <= 1000000 && std::abs(y) <= 1000000;
    }
};

struct DocumentLayer {
    std::string id;
    std::string name;
    RasterImage image;
    LayerTransformModel transform;
    std::optional<std::string> parentID;
    bool isGroup = false;
    bool visible = true;
    double opacity = 1.0;
    LayerBlendMode blendMode = LayerBlendMode::Normal;
};

struct PortableDocument {
    uint32_t width = 0;
    uint32_t height = 0;
    double resolution = 72.0;
    std::vector<DocumentLayer> layers; // Bottom to top, like CanvasDocument.

    bool valid() const {
        if (width == 0 || height == 0 || width > 30000 || height > 30000 || layers.size() > 10000) return false;
        if (!std::isfinite(resolution) || resolution < 1 || resolution > 9600) return false;

        std::unordered_map<std::string, const DocumentLayer*> byID;
        for (const auto& layer : layers) {
            if (layer.id.empty() || !layer.transform.valid() || !std::isfinite(layer.opacity) ||
                layer.opacity < 0 || layer.opacity > 1 || !byID.emplace(layer.id, &layer).second) return false;
            if (layer.isGroup && !layer.image.empty()) return false;
        }
        for (const auto& layer : layers) {
            std::unordered_set<std::string> seen{layer.id};
            auto parent = layer.parentID;
            size_t depth = 0;
            while (parent) {
                if (++depth > 64 || !seen.insert(*parent).second) return false;
                auto it = byID.find(*parent);
                if (it == byID.end() || !it->second->isGroup) return false;
                parent = it->second->parentID;
            }
        }
        return true;
    }
};

inline std::vector<RasterLayer> renderLayers(const PortableDocument& document) {
    std::unordered_map<std::string, const DocumentLayer*> byID;
    for (const auto& layer : document.layers) byID[layer.id] = &layer;

    std::vector<RasterLayer> result;
    for (const auto& layer : document.layers) {
        if (layer.isGroup || layer.image.empty() || layer.blendMode != LayerBlendMode::Normal) continue;

        bool visible = layer.visible;
        double opacity = layer.opacity;
        auto parent = layer.parentID;
        size_t depth = 0;
        while (parent && depth++ < 64) {
            auto it = byID.find(*parent);
            if (it == byID.end()) { visible = false; break; }
            visible = visible && it->second->visible;
            opacity *= it->second->opacity;
            parent = it->second->parentID;
        }
        if (!visible || opacity <= 0) continue;

        // PoC 3 intentionally supports the same first transform subset as the
        // previous raster spike: translated, 1:1 raster layers. Rotation,
        // resampling and flips remain renderer work for the production port.
        RasterLayer raster;
        raster.image = layer.image;
        raster.x = static_cast<int>(std::lround(layer.transform.x));
        raster.y = static_cast<int>(std::lround(layer.transform.y));
        raster.opacity = static_cast<float>(std::clamp(opacity, 0.0, 1.0));
        raster.visible = true;
        result.push_back(std::move(raster));
    }
    return result;
}

inline RasterImage renderDocument(const PortableDocument& document) {
    if (!document.valid()) return {};
    return compositeLayers(document.width, document.height, renderLayers(document));
}
