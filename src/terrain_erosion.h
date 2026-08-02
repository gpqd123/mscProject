#pragma once

#include <cstdint>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

struct TerrainSample {
    float height{};
    float gradientX{};
    float gradientY{};
};

class HeightField {
public:
    HeightField() = default;
    HeightField(uint32_t width, uint32_t height, float value = 0.0f);

    static HeightField loadPgm(const std::filesystem::path& path, float heightScale = 20.0f);
    static HeightField procedural(uint32_t width, uint32_t height, uint32_t seed = 1);

    uint32_t width() const { return _width; }
    uint32_t height() const { return _height; }
    bool empty() const { return _values.empty(); }
    float& at(uint32_t x, uint32_t y);
    float at(uint32_t x, uint32_t y) const;
    TerrainSample sample(float x, float y) const;
    double totalHeight() const;
    const std::vector<float>& values() const { return _values; }

private:
    uint32_t _width{};
    uint32_t _height{};
    std::vector<float> _values;
};

struct ErosionParameters {
    // The five user-facing quantities that define erosion behaviour.
    float capacityFactor = 6.0f;
    float erosionRate = 0.55f;
    float depositionRate = 0.08f;
    float evaporationRate = 0.01f;
    float erosionRadius = 0.75f;
};

enum class ErosionPreset : uint8_t {
    Gentle,
    Balanced,
    Aggressive,
};

const char* erosionPresetName(ErosionPreset preset);
ErosionParameters erosionParametersForPreset(ErosionPreset preset);

struct DropletState {
    float x{};
    float y{};
    float directionX{};
    float directionY{};
    float speed{};
    float water{};
    float sediment{};
    uint32_t age{};
    bool alive = true;
};

// The renderer advances this fixed-capacity droplet pool at a fixed 60 Hz.
class RealtimeErosionSimulator {
public:
    RealtimeErosionSimulator(ErosionParameters parameters, uint32_t maxDroplets = 4096,
        float spawnRate = 600.0f, uint32_t seed = 1);

    void update(HeightField& terrain, float deltaSeconds);
    void reset();
    const std::vector<DropletState>& droplets() const { return _droplets; }
    uint64_t totalSpawned() const { return _totalSpawned; }
    uint64_t totalSteps() const { return _totalSteps; }
    double totalEroded() const { return _totalEroded; }
    double totalDeposited() const { return _totalDeposited; }
    double totalOutflow() const { return _totalOutflow; }
    double carriedSediment() const;
    float spawnRate() const { return _spawnRate; }
    uint32_t maxDroplets() const { return _maxDroplets; }
    void setSpawnRate(float dropletsPerSecond);
    void setMaxDroplets(uint32_t maxDroplets);
    void setErosionParameters(ErosionParameters parameters);

private:
    void spawn(HeightField& terrain);
    void advance(HeightField& terrain, DropletState& drop, float timeScale);
    void deposit(HeightField& terrain, float x, float y, float amount);
    float erode(HeightField& terrain, float x, float y, float amount);
    void finishDroplet(HeightField& terrain, DropletState& drop, bool leftTerrain);

    ErosionParameters _parameters;
    uint32_t _capacity;
    uint32_t _maxDroplets;
    float _spawnRate;
    float _spawnAccumulator{};
    float _stepAccumulator{};
    std::mt19937 _random;
    std::vector<DropletState> _droplets;
    uint64_t _totalSpawned{};
    uint64_t _totalSteps{};
    double _totalEroded{};
    double _totalDeposited{};
    double _totalOutflow{};
};
