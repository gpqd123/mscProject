# Droplet terrain erosion — phases 1–5

This implementation keeps the simulation independent from Vulkan so the physical model can be tested deterministically. Vulkan is responsible for displaying the resulting indexed terrain mesh.

## Physical model

At a droplet position `(x, y)`, the terrain height and gradient are bilinearly interpolated from four heightmap samples. Direction uses inertia:

`d' = normalize(d * inertia - gradient * (1 - inertia))`

After moving one grid unit, `deltaHeight = newHeight - oldHeight`. Sediment capacity is:

`capacity = max(-deltaHeight * speed * water * capacityFactor, minCapacity)`

If the droplet is moving uphill, it deposits enough sediment to climb where possible. If its sediment exceeds capacity it deposits a fraction of the excess. Otherwise, erosion is distributed over a radius-weighted brush. Speed and water are then updated:

`speed' = sqrt(max(0, speed^2 - deltaHeight * gravity))`

`water' = water * (1 - evaporationRate)`

The droplet terminates at the terrain boundary, at maximum lifetime, when direction/speed stalls, or when too little water remains. Sediment carried out of the modeled area is recorded, allowing the mass invariant below to be checked:

`final terrain + escaped sediment ~= initial terrain`

## Inputs and visualization

- If `assets/heightmap.pgm` exists, it is loaded as an 8-bit or 16-bit P2/P5 PGM image.
- Otherwise a deterministic 128×128 procedural heightmap is generated.
- Heights become a Vulkan indexed mesh with finite-difference normals.
- Vertex colors show the result: red/orange marks eroded droplet paths and blue marks deposition.

## Validation

Build and run the headless validation suite:

```powershell
cmake -S . --preset vs2022
cmake --build --preset vs2022-debug --target terrain_erosion_tests
.\bin\Debug\terrain_erosion_tests.exe
```

The suite validates bilinear sampling and gradients, fixed-seed determinism, both erosion and deposition branches, lifetime limits, and terrain/sediment mass conservation.

## Realtime flow

The renderer also owns a fixed-capacity realtime droplet pool. New droplets spawn at random interior positions, physics advances at a fixed 60 Hz, and terminated droplets are recycled. Persistently mapped terrain and water-particle vertex buffers are updated in place. Blue quads show active water while the underlying mesh deforms from erosion and deposition.
