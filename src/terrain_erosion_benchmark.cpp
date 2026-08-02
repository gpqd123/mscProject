#include "terrain_erosion.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct BenchmarkCase {
    uint32_t terrainSize;
    uint32_t maxDroplets;
    float spawnRate;
};

struct Result {
    BenchmarkCase test;
    uint32_t frames;
    double simulatedSeconds;
    double wallMilliseconds;
    uint64_t totalSteps;
    uint64_t totalSpawned;
    double eroded;
    double deposited;
    double massError;
};

Result runCase(const BenchmarkCase& test, uint32_t frames)
{
    HeightField terrain = HeightField::procedural(test.terrainSize, test.terrainSize, 2026);
    const double initialMass = terrain.totalHeight();
    RealtimeErosionSimulator simulator(
        erosionParametersForPreset(ErosionPreset::Balanced), test.maxDroplets, test.spawnRate, 7391);

    constexpr float fixedFrame = 1.0f / 60.0f;
    for (uint32_t frame = 0; frame < 30; ++frame) simulator.update(terrain, fixedFrame);

    const uint64_t initialSteps = simulator.totalSteps();
    const auto start = std::chrono::steady_clock::now();
    for (uint32_t frame = 0; frame < frames; ++frame) simulator.update(terrain, fixedFrame);
    const auto end = std::chrono::steady_clock::now();

    const double wallMilliseconds = std::chrono::duration<double, std::milli>(end - start).count();
    const double massError = terrain.totalHeight() + simulator.carriedSediment()
        + simulator.totalOutflow() - initialMass;
    return {test, frames, frames * static_cast<double>(fixedFrame), wallMilliseconds,
        simulator.totalSteps() - initialSteps, simulator.totalSpawned(), simulator.totalEroded(),
        simulator.totalDeposited(), massError};
}

void writeHeader(std::ostream& output)
{
    output << "backend,terrain_size,max_droplets,spawn_rate,frames,simulated_seconds,"
              "wall_ms,update_us,steps_per_second,total_steps,total_spawned,eroded,deposited,mass_error\n";
}

void writeResult(std::ostream& output, const Result& result)
{
    const double updateMicroseconds = result.wallMilliseconds * 1000.0 / result.frames;
    const double stepsPerSecond = result.wallMilliseconds > 0.0
        ? result.totalSteps * 1000.0 / result.wallMilliseconds
        : 0.0;
    output << std::fixed << std::setprecision(6)
           << "cpu," << result.test.terrainSize << ',' << result.test.maxDroplets << ','
           << result.test.spawnRate << ',' << result.frames << ',' << result.simulatedSeconds << ','
           << result.wallMilliseconds << ',' << updateMicroseconds << ',' << stepsPerSecond << ','
           << result.totalSteps << ',' << result.totalSpawned << ',' << result.eroded << ','
           << result.deposited << ',' << result.massError << '\n';
}

uint32_t parsePositiveInteger(std::string_view value, std::string_view option)
{
    size_t consumed = 0;
    const unsigned long parsed = std::stoul(std::string(value), &consumed);
    if (consumed != value.size() || parsed == 0 || parsed > 1000000)
        throw std::invalid_argument("Invalid value for " + std::string(option));
    return static_cast<uint32_t>(parsed);
}

}

int main(int argc, char** argv)
{
    try {
        uint32_t frames = 600;
        std::filesystem::path csvPath;
        for (int index = 1; index < argc; ++index) {
            const std::string_view argument = argv[index];
            if (argument == "--frames" && index + 1 < argc) {
                frames = parsePositiveInteger(argv[++index], "--frames");
            } else if (argument == "--csv" && index + 1 < argc) {
                csvPath = argv[++index];
            } else if (argument == "--help") {
                std::cout << "Usage: terrain_erosion_benchmark [--frames N] [--csv PATH]\n";
                return 0;
            } else {
                throw std::invalid_argument("Unknown or incomplete option: " + std::string(argument));
            }
        }

        const std::vector<BenchmarkCase> cases {
            {64, 256, 120.0f},
            {128, 1024, 600.0f},
            {256, 4096, 1200.0f},
        };
        std::vector<Result> results;
        results.reserve(cases.size());
        for (const BenchmarkCase& test : cases) results.push_back(runCase(test, frames));

        writeHeader(std::cout);
        for (const Result& result : results) writeResult(std::cout, result);

        if (!csvPath.empty()) {
            if (csvPath.has_parent_path()) std::filesystem::create_directories(csvPath.parent_path());
            std::ofstream csv(csvPath);
            if (!csv) throw std::runtime_error("Could not open CSV output: " + csvPath.string());
            writeHeader(csv);
            for (const Result& result : results) writeResult(csv, result);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "terrain erosion benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
