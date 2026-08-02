#pragma once

#include "terrain_erosion.h"
#include <vk_types.h>

#include <span>

namespace terrainvis {

struct Settings {
    float verticalScale = 2.6f;
    float movingDropRadius = 0.20f;
    glm::vec3 terrainLowColor {0.08f, 0.22f, 0.08f};
    glm::vec3 terrainHighColor {0.65f, 0.52f, 0.30f};
    glm::vec3 dropColor {0.08f, 0.55f, 1.0f};
};

void buildTerrainVertices(const HeightField& terrain, const Settings& settings, std::span<Vertex> output);
uint32_t buildDropletVertices(const HeightField& terrain, std::span<const DropletState> droplets,
    const Settings& settings, std::span<Vertex> output);

}
