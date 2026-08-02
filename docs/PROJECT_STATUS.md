# Project status against the May–August 2026 schedule

Status date: 2 August 2026. This document maps repository evidence to the supplied project schedule. It records implementation status, not retrospective claims about when uncommitted work was originally performed.

| Phase | Scheduled work | Repository evidence | Status |
|---|---|---|---|
| 1 — Theory & environment | Physics formulas; Vulkan base pipeline | Formula summary in `src/PHASES_1_5.md`; Vulkan/SDL initialization and render pipeline | Implementation complete; literature citations still belong in the dissertation |
| 2 — Input & mesh | Heightmap loader; I/O validation; rendering | P2/P5 loader, procedural fallback, indexed mesh and normals | Complete |
| 3 — Particle physics | Droplet state, spawn, gradient, gravity, velocity and visual debugging | `RealtimeErosionSimulator` and visible water mesh | Complete |
| 4 — Core algorithm | Capacity, erosion and deposition | Radius-weighted erosion, bilinear deposition and sediment capacity | Complete |
| 5 — Validation & debug | Lifecycle management; physical-model checks | Fixed lifetime/outflow handling, determinism and mass-conservation tests | Complete |
| 6 — Tuning & GUI | Controls, presets and profiling baseline | ImGui controls, three presets, CPU stage timings and GPU frame timestamps | Complete |
| 7 — Data & benchmark | CPU/GPU benchmark | Reproducible CPU CSV benchmark and GPU rendering baseline | Partial: a like-for-like GPU erosion backend does not yet exist |
| 8 — Writing & finalization | Dissertation sections, proofreading and submission | Scheduled from 3 August onward | Not started in code repository |

## Evidence rules

- Git commit timestamps reflect when changes are actually committed.
- Milestone messages may reference the planned phase, but do not claim an earlier implementation date.
- A CPU/GPU erosion speedup should only be reported after both backends execute equivalent physics with matched inputs and an agreed numerical tolerance.
