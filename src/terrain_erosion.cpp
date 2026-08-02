#include "terrain_erosion.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <numeric>
#include <stdexcept>

namespace {
// Fixed integration constants are implementation details, not erosion controls.
constexpr uint32_t kMaximumDropletLifetime = 480;
constexpr float kDirectionInertia = 0.08f;
constexpr float kGravity = 4.0f;
constexpr float kMinimumSedimentCapacity = 0.001f;
constexpr float kInitialWater = 1.0f;
constexpr float kInitialSpeed = 1.0f;
constexpr float kMovementScale = 0.12f;
constexpr float kMaximumStep = 0.15f;
constexpr float kMinimumDownhillDrop = 0.0002f;

std::string nextPgmToken(std::istream& input)
{
    std::string token;
    while (input >> token) {
        if (!token.empty() && token[0] == '#') {
            std::string ignored;
            std::getline(input, ignored);
            continue;
        }
        return token;
    }
    throw std::runtime_error("Unexpected end of PGM file");
}

float smoothStep(float value)
{
    value = std::clamp(value, 0.0f, 1.0f);
    return value * value * (3.0f - 2.0f * value);
}

}

HeightField::HeightField(uint32_t width, uint32_t height, float value)
    : _width(width), _height(height), _values(static_cast<size_t>(width) * height, value)
{
    if (width < 2 || height < 2) throw std::invalid_argument("Height field must be at least 2x2");
}

HeightField HeightField::loadPgm(const std::filesystem::path& path, float heightScale)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Could not open heightmap: " + path.string());
    const std::string magic = nextPgmToken(input);
    if (magic != "P2" && magic != "P5") throw std::runtime_error("Heightmap must be a P2 or P5 PGM file");
    const uint32_t width = static_cast<uint32_t>(std::stoul(nextPgmToken(input)));
    const uint32_t height = static_cast<uint32_t>(std::stoul(nextPgmToken(input)));
    const uint32_t maxValue = static_cast<uint32_t>(std::stoul(nextPgmToken(input)));
    if (maxValue == 0 || maxValue > 65535) throw std::runtime_error("Unsupported PGM sample range");

    HeightField result(width, height);
    if (magic == "P2") {
        for (float& value : result._values) value = std::stof(nextPgmToken(input)) / maxValue * heightScale;
    } else {
        input.get();
        for (float& value : result._values) {
            uint32_t sample = static_cast<unsigned char>(input.get());
            if (maxValue > 255) sample = (sample << 8u) | static_cast<unsigned char>(input.get());
            if (!input) throw std::runtime_error("Truncated PGM pixel data");
            value = static_cast<float>(sample) / maxValue * heightScale;
        }
    }
    return result;
}

HeightField HeightField::procedural(uint32_t width, uint32_t height, uint32_t seed)
{
    HeightField result(width, height);
    std::mt19937 random(seed);
    const float minimumDimension = static_cast<float>(std::min(width, height));
    constexpr float minimumHeight = 0.5f;
    constexpr float maximumHeight = 14.0f;
    constexpr float mountainRadiusScale = 0.58f;
    constexpr float summitSharpness = 12.0f;

    std::uniform_real_distribution<float> centerOffset(-0.035f, 0.035f);
    std::uniform_real_distribution<float> phase(0.0f, 6.2831853f);
    const float centerX = static_cast<float>(width - 1) * (0.5f + centerOffset(random));
    const float centerY = static_cast<float>(height - 1) * (0.5f + centerOffset(random));
    const float mountainRadius = minimumDimension * mountainRadiusScale;
    const float ridgePhaseA = phase(random);
    const float ridgePhaseB = phase(random);

    for (uint32_t y = 0; y < height; ++y) for (uint32_t x = 0; x < width; ++x) {
        const float dx = (static_cast<float>(x) - centerX) / mountainRadius;
        const float dy = (static_cast<float>(y) - centerY) / mountainRadius;
        const float radialDistance = std::sqrt(dx * dx + dy * dy);
        const float angle = std::atan2(dy, dx);

        const float mountainBody = std::pow(smoothStep(1.0f - radialDistance), 0.72f);
        const float summit = std::exp(-summitSharpness * radialDistance * radialDistance);
        const float ridgeMask = mountainBody * smoothStep(radialDistance / 0.18f);
        const float ridges = ridgeMask * (
            0.055f * std::sin(5.0f * angle + 11.0f * radialDistance + ridgePhaseA)
            + 0.030f * std::sin(9.0f * angle - 17.0f * radialDistance + ridgePhaseB));
        const float broadVariation = mountainBody * 0.025f
            * std::sin(dx * 13.0f + ridgePhaseB) * std::cos(dy * 11.0f + ridgePhaseA);

        const float normalizedHeight = std::clamp(
            0.78f * mountainBody + 0.22f * summit + ridges + broadVariation,
            0.0f, 1.0f);
        result.at(x, y) = minimumHeight
            + normalizedHeight * (maximumHeight - minimumHeight);
    }
    return result;
}

float& HeightField::at(uint32_t x, uint32_t y) { return _values.at(static_cast<size_t>(y) * _width + x); }
float HeightField::at(uint32_t x, uint32_t y) const { return _values.at(static_cast<size_t>(y) * _width + x); }

TerrainSample HeightField::sample(float x, float y) const
{
    x = std::clamp(x, 0.0f, static_cast<float>(_width - 1));
    y = std::clamp(y, 0.0f, static_cast<float>(_height - 1));
    const uint32_t x0 = std::min(static_cast<uint32_t>(x), _width - 2);
    const uint32_t y0 = std::min(static_cast<uint32_t>(y), _height - 2);
    const float tx = x - x0;
    const float ty = y - y0;
    const float h00 = at(x0, y0), h10 = at(x0 + 1, y0);
    const float h01 = at(x0, y0 + 1), h11 = at(x0 + 1, y0 + 1);
    TerrainSample result;
    result.height = h00 * (1 - tx) * (1 - ty) + h10 * tx * (1 - ty) + h01 * (1 - tx) * ty + h11 * tx * ty;
    result.gradientX = (h10 - h00) * (1 - ty) + (h11 - h01) * ty;
    result.gradientY = (h01 - h00) * (1 - tx) + (h11 - h10) * tx;
    return result;
}

double HeightField::totalHeight() const { return std::accumulate(_values.begin(), _values.end(), 0.0); }

const char* erosionPresetName(ErosionPreset preset)
{
    switch (preset) {
    case ErosionPreset::Gentle: return "Gentle";
    case ErosionPreset::Balanced: return "Balanced";
    case ErosionPreset::Aggressive: return "Aggressive";
    }
    throw std::invalid_argument("Unknown erosion preset");
}

ErosionParameters erosionParametersForPreset(ErosionPreset preset)
{
    switch (preset) {
    case ErosionPreset::Gentle:
        return {.capacityFactor = 3.5f, .erosionRate = 0.28f, .depositionRate = 0.14f,
            .evaporationRate = 0.018f, .erosionRadius = 1.20f};
    case ErosionPreset::Balanced:
        return {};
    case ErosionPreset::Aggressive:
        return {.capacityFactor = 9.0f, .erosionRate = 0.82f, .depositionRate = 0.05f,
            .evaporationRate = 0.006f, .erosionRadius = 0.55f};
    }
    throw std::invalid_argument("Unknown erosion preset");
}

RealtimeErosionSimulator::RealtimeErosionSimulator(ErosionParameters parameters, uint32_t maxDroplets,
    float spawnRate, uint32_t seed)
    : _parameters(parameters), _capacity(maxDroplets), _maxDroplets(maxDroplets), _spawnRate(spawnRate), _random(seed)
{
    if (maxDroplets == 0 || spawnRate < 0.0f) throw std::invalid_argument("Invalid realtime erosion settings");
    _droplets.reserve(maxDroplets);
}

void RealtimeErosionSimulator::setSpawnRate(float dropletsPerSecond)
{
    _spawnRate = std::clamp(dropletsPerSecond, 0.0f, 10000.0f);
    if (_spawnRate == 0.0f) _spawnAccumulator = 0.0f;
}

void RealtimeErosionSimulator::setMaxDroplets(uint32_t maxDroplets)
{
    _maxDroplets = std::clamp(maxDroplets, 1u, _capacity);
    if (_droplets.size() > _maxDroplets) _droplets.resize(_maxDroplets);
}

void RealtimeErosionSimulator::setErosionParameters(ErosionParameters parameters)
{
    parameters.erosionRate = std::clamp(parameters.erosionRate, 0.0f, 1.0f);
    parameters.depositionRate = std::clamp(parameters.depositionRate, 0.0f, 1.0f);
    parameters.evaporationRate = std::clamp(parameters.evaporationRate, 0.0f, 0.99f);
    parameters.capacityFactor = std::max(parameters.capacityFactor, 0.0f);
    parameters.erosionRadius = std::max(parameters.erosionRadius, 0.1f);
    _parameters = parameters;
}

double RealtimeErosionSimulator::carriedSediment() const
{
    double sediment = 0.0;
    for (const DropletState& drop : _droplets) sediment += drop.sediment;
    return sediment;
}

void RealtimeErosionSimulator::reset()
{
    _droplets.clear();
    _spawnAccumulator = 0.0f;
    _stepAccumulator = 0.0f;
    _totalSpawned = 0;
    _totalSteps = 0;
    _totalEroded = 0.0;
    _totalDeposited = 0.0;
    _totalOutflow = 0.0;
}

void RealtimeErosionSimulator::spawn(HeightField& terrain)
{
    if (_droplets.size() >= _maxDroplets) return;
    std::uniform_real_distribution<float> x(1.0f, terrain.width() - 2.001f);
    std::uniform_real_distribution<float> y(1.0f, terrain.height() - 2.001f);
    DropletState drop{};
    drop.x = x(_random); drop.y = y(_random);
    drop.speed = kInitialSpeed;
    drop.water = kInitialWater;
    drop.alive = true;
    _droplets.push_back(drop);
    ++_totalSpawned;
}

void RealtimeErosionSimulator::deposit(HeightField& terrain, float x, float y, float amount)
{
    const uint32_t x0 = static_cast<uint32_t>(x), y0 = static_cast<uint32_t>(y);
    const float tx = x - x0, ty = y - y0;
    terrain.at(x0, y0) += amount * (1 - tx) * (1 - ty);
    terrain.at(x0 + 1, y0) += amount * tx * (1 - ty);
    terrain.at(x0, y0 + 1) += amount * (1 - tx) * ty;
    terrain.at(x0 + 1, y0 + 1) += amount * tx * ty;
    _totalDeposited += amount;
}

void RealtimeErosionSimulator::finishDroplet(HeightField& terrain, DropletState& drop, bool leftTerrain)
{
    if (leftTerrain) {
        _totalOutflow += drop.sediment;
    } else if (drop.sediment > 0.0f) {
        deposit(terrain, drop.x, drop.y, drop.sediment);
    }
    drop.sediment = 0.0f;
    drop.water = 0.0f;
    drop.alive = false;
}

float RealtimeErosionSimulator::erode(HeightField& terrain, float x, float y, float amount)
{
    float totalWeight = 0.0f;
    struct Cell { uint32_t x, y; float weight; };
    std::vector<Cell> cells;
    const float erosionRadius = std::max(_parameters.erosionRadius, 0.1f);
    const int radius = static_cast<int>(std::ceil(erosionRadius));
    for (int oy = -radius; oy <= radius; ++oy) for (int ox = -radius; ox <= radius; ++ox) {
        const int px = static_cast<int>(x) + ox, py = static_cast<int>(y) + oy;
        if (px < 0 || py < 0 || px >= static_cast<int>(terrain.width()) || py >= static_cast<int>(terrain.height())) continue;
        const float distance = std::hypot(px - x, py - y);
        const float weight = std::max(0.0f, erosionRadius - distance);
        if (weight > 0) { cells.push_back({static_cast<uint32_t>(px), static_cast<uint32_t>(py), weight}); totalWeight += weight; }
    }
    if (cells.empty()) {
        const uint32_t px = std::min(static_cast<uint32_t>(std::round(x)), terrain.width() - 1);
        const uint32_t py = std::min(static_cast<uint32_t>(std::round(y)), terrain.height() - 1);
        cells.push_back({px, py, 1.0f});
        totalWeight = 1.0f;
    }
    float removed = 0.0f;
    for (const Cell& cell : cells) {
        const float take = std::min(amount * cell.weight / totalWeight, std::max(terrain.at(cell.x, cell.y), 0.0f));
        terrain.at(cell.x, cell.y) -= take;
        removed += take;
    }
    _totalEroded += removed;
    return removed;
}

void RealtimeErosionSimulator::advance(HeightField& terrain, DropletState& drop, float timeScale)
{
    const TerrainSample oldSample = terrain.sample(drop.x, drop.y);
    drop.directionX = drop.directionX * kDirectionInertia
        - oldSample.gradientX * (1.0f - kDirectionInertia);
    drop.directionY = drop.directionY * kDirectionInertia
        - oldSample.gradientY * (1.0f - kDirectionInertia);
    const float length = std::hypot(drop.directionX, drop.directionY);
    if (length < 1e-6f) {
        finishDroplet(terrain, drop, false);
        return;
    }
    drop.directionX /= length;
    drop.directionY /= length;

    const float distance = std::min(drop.speed * timeScale * kMovementScale,
        kMaximumStep);
    const float nextX = drop.x + drop.directionX * distance;
    const float nextY = drop.y + drop.directionY * distance;
    if (nextX < 0 || nextY < 0 || nextX >= terrain.width() - 1 || nextY >= terrain.height() - 1) {
        finishDroplet(terrain, drop, true);
        return;
    }

    const float nextHeight = terrain.sample(nextX, nextY).height;
    const float deltaHeight = nextHeight - oldSample.height;

    // Only accept a real downhill step. Flat ground, a bump or a local minimum
    // ends the droplet immediately, so it cannot bounce around and edit the same pit.
    if (deltaHeight >= -kMinimumDownhillDrop) {
        finishDroplet(terrain, drop, false);
        return;
    }

    const float capacity = std::max(
        -deltaHeight * drop.speed * drop.water * _parameters.capacityFactor,
        kMinimumSedimentCapacity);
    if (drop.sediment > capacity) {
        const float amount = (drop.sediment - capacity) * _parameters.depositionRate;
        deposit(terrain, drop.x, drop.y, amount);
        drop.sediment -= amount;
    } else {
        const float amount = std::min(
            (capacity - drop.sediment) * _parameters.erosionRate, -deltaHeight);
        drop.sediment += erode(terrain, drop.x, drop.y, amount);
    }
    drop.speed = std::sqrt(std::max(0.0f,
        drop.speed * drop.speed - deltaHeight * kGravity));
    drop.water *= std::pow(1.0f - _parameters.evaporationRate, timeScale);
    drop.x = nextX;
    drop.y = nextY;
    ++drop.age;
    ++_totalSteps;
    if (drop.age >= kMaximumDropletLifetime
        || drop.water < 0.01f) {
        finishDroplet(terrain, drop, false);
    }
}

void RealtimeErosionSimulator::update(HeightField& terrain, float deltaSeconds)
{
    // Fixed 60 Hz physics makes the result stable when rendering FPS changes.
    deltaSeconds = std::clamp(deltaSeconds, 0.0f, 0.1f);
    _spawnAccumulator += deltaSeconds * _spawnRate;
    while (_spawnAccumulator >= 1.0f && _droplets.size() < _maxDroplets) { spawn(terrain); _spawnAccumulator -= 1.0f; }
    _stepAccumulator += deltaSeconds;
    constexpr float fixedStep = 1.0f / 60.0f;
    while (_stepAccumulator >= fixedStep) {
        for (DropletState& drop : _droplets) advance(terrain, drop, 1.0f);
        std::erase_if(_droplets, [](const DropletState& drop) { return !drop.alive; });
        _stepAccumulator -= fixedStep;
    }
}
