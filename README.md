# Droplet-Based Terrain Erosion Simulator

A C++20/Vulkan terrain visualizer with a deterministic, CPU-based hydraulic erosion model. Rain droplets move over a height field, carry sediment, erode slopes, deposit material, and are rendered on the deforming terrain in real time.

## Current capabilities

- P2/P5 PGM heightmap loading with a deterministic procedural fallback.
- Bilinear height/gradient sampling and an indexed terrain mesh.
- Fixed-step droplet simulation with erosion, deposition, evaporation, outflow, and mass tracking.
- Real-time Vulkan rendering with visible water droplets and an orbit camera.
- ImGui controls for rain, simulation speed, physics parameters, and Gentle/Balanced/Aggressive presets.
- CPU simulation/mesh timing and Vulkan timestamp-based GPU frame timing.
- Deterministic validation tests and a CSV-exporting CPU benchmark.

See [project status](docs/PROJECT_STATUS.md), [physics and validation notes](src/PHASES_1_5.md), and [benchmarking protocol](docs/BENCHMARKING.md).

## Requirements

- Windows with Visual Studio 2022 and the Desktop C++ workload.
- CMake 3.25 or newer.
- Vulkan SDK, including `glslangValidator`.
- A Vulkan 1.3-capable driver.

Dependencies used by the build are vendored under `third_party/`.

## Build

```powershell
cmake --preset vs2022
cmake --build --preset vs2022-debug --target hydraulic_erosion terrain_erosion_tests terrain_erosion_benchmark
```

The application and command-line tools are written to `bin/Debug` or `bin/Release`.

## Run

```powershell
.\bin\Debug\hydraulic_erosion.exe
```

The application looks for `assets/heightmap.pgm`. If it is absent or invalid, a seeded 128×128 mountain is generated. Right-click toggles between UI input and the orbit camera; use the mouse wheel to zoom.

## Validate and benchmark

```powershell
.\bin\Debug\terrain_erosion_tests.exe
.\bin\Release\terrain_erosion_benchmark.exe --frames 1800 --csv build\benchmark-results.csv
```

Benchmark CSV files are machine-specific evidence and should record the CPU/GPU/driver/build configuration alongside the result. The current erosion backend is CPU-only; GPU frame timing measures the Vulkan rendering pipeline and is not presented as a GPU erosion speedup.
