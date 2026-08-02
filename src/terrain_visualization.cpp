#include "terrain_visualization.h"

#include <algorithm>
#include <array>

namespace terrainvis {
namespace {

Vertex makeVertex(const glm::vec3& position, const glm::vec3& normal, const glm::vec3& color)
{
    Vertex vertex{};
    vertex.position = glm::vec4(position, 1.0f);
    vertex.normal = glm::vec4(normal, 0.0f);
    vertex.color = glm::vec4(color, 1.0f);
    return vertex;
}

}

void buildTerrainVertices(const HeightField& terrain, const Settings& settings, std::span<Vertex> output)
{
    const float halfWidth = (terrain.width() - 1) * 0.5f;
    const float halfHeight = (terrain.height() - 1) * 0.5f;
    for (uint32_t y = 0; y < terrain.height(); ++y) for (uint32_t x = 0; x < terrain.width(); ++x) {
        const size_t index = static_cast<size_t>(y) * terrain.width() + x;
        const float left = terrain.at(x > 0 ? x - 1 : x, y);
        const float right = terrain.at(x + 1 < terrain.width() ? x + 1 : x, y);
        const float down = terrain.at(x, y > 0 ? y - 1 : y);
        const float up = terrain.at(x, y + 1 < terrain.height() ? y + 1 : y);
        const float heightMix = std::clamp((terrain.at(x, y) + 2.0f) / 14.0f, 0.0f, 1.0f);
        Vertex& vertex = output[index];
        vertex.position = {x - halfWidth, terrain.at(x, y) * settings.verticalScale, y - halfHeight, 1.0f};
        vertex.normal = glm::vec4(glm::normalize(glm::vec3((left - right) * settings.verticalScale, 2.0f,
            (down - up) * settings.verticalScale)), 0.0f);
        vertex.color = glm::vec4(glm::mix(settings.terrainLowColor, settings.terrainHighColor, heightMix), 1.0f);
    }
}

uint32_t buildDropletVertices(const HeightField& terrain, std::span<const DropletState> droplets,
    const Settings& settings, std::span<Vertex> output)
{
    const uint32_t count = std::min<uint32_t>(static_cast<uint32_t>(droplets.size()),
        static_cast<uint32_t>(output.size() / 6));
    const float halfWidth = (terrain.width() - 1) * 0.5f;
    const float halfHeight = (terrain.height() - 1) * 0.5f;
    const float radius = settings.movingDropRadius;
    const std::array<glm::vec3, 6> offsets {{
        {0, radius * 1.6f, 0}, {0, -radius * 1.2f, 0},
        {-radius, 0, 0}, {radius, 0, 0}, {0, 0, -radius}, {0, 0, radius}}};

    for (uint32_t i = 0; i < count; ++i) {
        const DropletState& drop = droplets[i];
        const glm::vec3 center {drop.x - halfWidth,
            terrain.sample(drop.x, drop.y).height * settings.verticalScale + 0.32f,
            drop.y - halfHeight};
        for (uint32_t corner = 0; corner < offsets.size(); ++corner)
            output[i * offsets.size() + corner] = makeVertex(center + offsets[corner],
                glm::normalize(offsets[corner]), settings.dropColor);
    }
    return count;
}

}
