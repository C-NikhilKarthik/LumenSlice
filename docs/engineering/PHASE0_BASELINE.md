# Phase 0 Baseline

Date: 2026-07-30

This is the first reproducible baseline before changing the volume, rendering, or
segmentation engine.

## Verified commands

| Command | Result |
|---|---|
| `swift run SegTest` | Passed: all C++ segmentation tests |
| `swift test` | Passed: 21 Swift tests, 0 failures |
| `swift build --product LumenBench` | Passed: benchmark target compiles |
| `git status --short` | Clean before this report |

The C++ test target covers coordinate mapping, thresholding, region growing,
painting, overlays, marching cubes, STL output, multi-segment isolation, islands,
undo/redo, margin, smoothing, grow-from-seeds, scissors, and level tracing.

## Build observations

The local Homebrew DCMTK static libraries may be built for the host macOS SDK
(macOS 26 on the development machine) while the package targets macOS 14. On
that host the linker emits a compatibility warning for the static objects; this
does not fail the build. The package no longer advertises macOS 13 support. A
release intended for macOS 14 should be built on macOS 14 or with DCMTK rebuilt
for the macOS 14 deployment target; the macOS CI runner is macOS 14.

The initial sandboxed SwiftPM invocation could not access the system compiler cache.
Running the focused targets with normal SwiftPM cache access succeeded; this is an
environment issue, not a source failure.

## Baseline gaps

- No checked-in DICOM fixture was found, so ingestion and slice-render timings are
  not yet recorded here.
- The repository contains synthetic-series tooling, but a real representative
  dataset is needed for compressed transfer syntaxes, orientation, spacing, and
  large-volume performance checks.
- Existing tests are correctness-focused. They do not yet assert latency, peak
  memory, allocations, frame pacing, or cancellation behavior.

## Next phase

Phase 1 should run `LumenBench` against representative datasets. It measures slice
extraction for all three axes, mask overlay, threshold, and mesh snapshot/generation
in both debug and release builds. The benchmark reports dimensions, spacing, voxel
count, and elapsed time. Load timing and peak working-set measurement should be
added once the dataset matrix is checked in.
