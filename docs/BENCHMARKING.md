# Benchmarking protocol

## CPU erosion baseline

Build the Release target and run a fixed number of 60 Hz updates:

```powershell
cmake --build --preset vs2022-release --target terrain_erosion_benchmark
.\bin\Release\terrain_erosion_benchmark.exe --frames 1800 --csv build\benchmark-results.csv
```

The executable runs deterministic 64×64, 128×128 and 256×256 cases. Its CSV reports wall time, mean update time, processed droplet steps per second, erosion/deposition totals and mass error. Thirty warm-up frames are excluded from timing.

Record these alongside every retained result:

- Git commit ID and Release/Debug configuration.
- CPU, GPU, driver and operating-system versions.
- Vulkan SDK and compiler versions.
- Whether other GPU- or CPU-heavy applications were active.
- At least five runs; report median and spread rather than the best run.

## GPU rendering baseline

The live UI uses Vulkan timestamp queries around the submitted frame. This is a real GPU duration for background compute, terrain/water drawing, transfer and ImGui work. It is useful for finding rendering bottlenecks, but it is not comparable to the CPU erosion update because the workloads differ.

## Requirements for a CPU/GPU erosion comparison

A valid future comparison must use the same source heightmap, random seed, droplet count, parameter preset and simulated duration. The GPU backend must implement the same capacity, erosion, deposition, evaporation and termination rules. Before timing, compare output terrain mass and an error metric such as RMSE against the CPU reference. Only results within the selected tolerance should be used to report speedup.
