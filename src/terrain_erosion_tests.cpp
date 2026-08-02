#include "terrain_erosion.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <stdexcept>

static void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

class TemporaryFile {
public:
    explicit TemporaryFile(const char* suffix)
    {
        const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path()
            / ("terrain_erosion_" + std::to_string(unique) + suffix);
    }
    ~TemporaryFile()
    {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
    std::filesystem::path path;
};

int main()
{
    try {
        HeightField plane(8, 8);
        for (uint32_t y = 0; y < plane.height(); ++y)
            for (uint32_t x = 0; x < plane.width(); ++x) plane.at(x, y) = 2.0f * x + 3.0f * y;
        const TerrainSample sample = plane.sample(2.25f, 4.5f);
        require(std::abs(sample.height - 18.0f) < 1e-5f, "bilinear height sampling failed");
        require(std::abs(sample.gradientX - 2.0f) < 1e-5f && std::abs(sample.gradientY - 3.0f) < 1e-5f, "gradient sampling failed");

        TemporaryFile asciiFile("_ascii.pgm");
        {
            std::ofstream output(asciiFile.path);
            output << "P2\n# I/O regression fixture\n2 2\n15\n0 5 10 15\n";
        }
        const HeightField asciiHeightmap = HeightField::loadPgm(asciiFile.path, 3.0f);
        require(asciiHeightmap.width() == 2 && asciiHeightmap.height() == 2,
            "P2 heightmap dimensions were not loaded");
        require(std::abs(asciiHeightmap.at(1, 1) - 3.0f) < 1e-5f,
            "P2 heightmap samples were not normalized");

        TemporaryFile binaryFile("_binary.pgm");
        {
            std::ofstream output(binaryFile.path, std::ios::binary);
            output << "P5\n2 2\n255\n";
            const std::array<unsigned char, 4> pixels {0, 85, 170, 255};
            output.write(reinterpret_cast<const char*>(pixels.data()), pixels.size());
        }
        const HeightField binaryHeightmap = HeightField::loadPgm(binaryFile.path, 6.0f);
        require(std::abs(binaryHeightmap.at(1, 0) - 2.0f) < 1e-5f
                && std::abs(binaryHeightmap.at(0, 1) - 4.0f) < 1e-5f,
            "P5 heightmap samples were not loaded");

        HeightField first = HeightField::procedural(64, 64, 42);
        HeightField second = HeightField::procedural(64, 64, 42);
        require(first.values() == second.values(), "fixed-seed terrain generation is not deterministic");
        const float edgeAverage = (first.at(0, 0) + first.at(63, 0)
            + first.at(0, 63) + first.at(63, 63)) * 0.25f;
        require(first.at(32, 32) > edgeAverage + 8.0f,
            "procedural terrain does not contain a clear central mountain peak");

        const ErosionParameters gentle = erosionParametersForPreset(ErosionPreset::Gentle);
        const ErosionParameters balanced = erosionParametersForPreset(ErosionPreset::Balanced);
        const ErosionParameters aggressive = erosionParametersForPreset(ErosionPreset::Aggressive);
        require(gentle.erosionRate < balanced.erosionRate
                && balanced.erosionRate < aggressive.erosionRate,
            "erosion presets are not ordered by intensity");
        require(std::string(erosionPresetName(ErosionPreset::Balanced)) == "Balanced",
            "erosion preset name is incorrect");

        HeightField realtimeTerrain(32, 32);
        for (uint32_t y = 0; y < realtimeTerrain.height(); ++y)
            for (uint32_t x = 0; x < realtimeTerrain.width(); ++x)
                realtimeTerrain.at(x, y) = 20.0f - 0.30f * x - 0.10f * y;
        ErosionParameters realtimeParameters;
        realtimeParameters.evaporationRate = 0.03f;
        RealtimeErosionSimulator realtime(realtimeParameters, 32, 30.0f, 77);
        realtime.setMaxDroplets(32);
        realtime.setSpawnRate(30.0f);
        require(realtime.maxDroplets() == 32 && realtime.spawnRate() == 30.0f, "realtime controls were not applied");
        const std::vector<float> beforeRealtime = realtimeTerrain.values();
        realtime.update(realtimeTerrain, 0.1f);
        require(!realtime.droplets().empty(), "realtime simulator did not spawn droplets");
        require(realtime.totalSteps() > 0, "realtime simulator did not advance physics");
        realtime.setSpawnRate(0.0f);
        for (int frame = 0; frame < 300; ++frame) realtime.update(realtimeTerrain, 1.0f / 60.0f);
        require(realtimeTerrain.values() != beforeRealtime, "visible realtime droplets did not modify terrain");
        require(realtime.totalEroded() > 0.0, "realtime erosion amount was not recorded");
        require(realtime.totalDeposited() > 0.0, "remaining sediment was not deposited");
        require(realtime.droplets().empty(), "evaporated droplets remained active too long");
        const double realtimeInitialMass = std::accumulate(beforeRealtime.begin(), beforeRealtime.end(), 0.0);
        const double realtimeMassError = realtimeTerrain.totalHeight() + realtime.carriedSediment()
            + realtime.totalOutflow() - realtimeInitialMass;
        require(std::abs(realtimeMassError) < 0.05, "realtime terrain/sediment mass is not conserved");

        HeightField flatTerrain(16, 16, 2.0f);
        const std::vector<float> flatBefore = flatTerrain.values();
        RealtimeErosionSimulator flatDrops(realtimeParameters, 1, 20.0f, 5);
        flatDrops.update(flatTerrain, 0.1f);
        require(flatDrops.droplets().empty(), "a droplet remained active on flat ground");
        require(flatTerrain.values() == flatBefore, "a stopped droplet modified flat ground");
        std::cout << "terrain erosion tests passed\n"
                  << "steps=" << realtime.totalSteps()
                  << " eroded=" << realtime.totalEroded()
                  << " deposited=" << realtime.totalDeposited()
                  << " massError=" << realtimeMassError << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "terrain erosion tests failed: " << error.what() << '\n';
        return 1;
    }
}
