# C++ onboarding (for people who do not write C++ daily)

You do not need to be a C++ expert to work on LumenSlice. The core uses a small,
consistent set of idioms. This guide explains each one in plain terms and points at
the real file where you can see it. Read it once and the core stops looking foreign.

## 1. Headers vs. source: declare here, define there

C++ splits a class across two files:

- A **header** (`.hpp`) **declares** what exists: the class name, its method
  signatures, its fields. Think "the table of contents."
- A **source** file (`.cpp`) **defines** how those methods actually work. Think "the
  chapters."

Other files `#include` the header to learn what they can call; the source is
compiled once. Example: `src/segmentation/segment_editor.hpp` lists every method of
`SegmentEditor`; `segment_editor.cpp` implements them.

Every header starts with `#pragma once`. That just means "only include me once per
file" - it prevents double-definition errors. You will see it at the top of every
`.hpp`.

## 2. Namespaces: avoiding name clashes

All core code lives in `namespace lumen { ... }`. That is why you see `lumen::Volume`
or `lumen::SegmentEditor` from outside, and just `Volume` from inside the namespace.
It is the C++ version of a module prefix. Nothing more to it.

## 3. RAII and smart pointers: memory that frees itself

This is the single most important C++ idea here. **RAII** means "a resource is tied
to an object's lifetime": when the object goes away, its resource is released
automatically. You almost never write `new` or `delete` by hand.

The volume's pixel buffer is a `std::unique_ptr<float[]>` (`src/core/volume.h`):

```cpp
std::unique_ptr<float[]> voxel_buffer;  // owns the HU array
```

`unique_ptr` is a "smart pointer": it owns the array and frees it automatically when
the `Volume` is destroyed. No leak is possible, even if an error happens. When you
see `std::make_unique<float[]>(n)`, read it as "allocate n floats, owned safely."

Rule of thumb while editing this code: do not call `new`/`delete`. If you need to
own something, use `unique_ptr` (single owner) and let scope clean it up.

## 4. Values, references, and pointers

C++ lets you pass data three ways. The codebase uses them deliberately:

- **By value** (`int x`): a copy. Cheap for small things (ints, floats).
- **By const reference** (`const Volume& v`): "look at this, do not copy it, do not
  change it." Used everywhere for big objects. The `&` means reference; `const`
  means read-only.
- **By pointer** (`const Volume* v`): like a reference, but it can be null and can be
  re-pointed. The bridge uses pointers because C requires them, and `SegmentEditor`
  keeps a `const Volume* volume_` to say "I look at the scan but I do not own it."

If you see `&`, think "no copy." If you see `const`, think "read-only, safe to share."

## 5. `const` and `[[nodiscard]]`: promises the compiler checks

- A method marked `const` (e.g. `int segment_count() const`) promises not to change
  the object. The compiler enforces it. Getters are `const`; mutators are not.
- `[[nodiscard]]` on a return value means "you must use this." If you call a
  `[[nodiscard]]` function and ignore the result, the compiler warns you. It is used
  on getters and on anything where ignoring the answer would be a bug.

These are not decoration; they are guardrails the compiler verifies for free.

## 6. `enum class`: named constants with a type

`Axis` (`src/core/volume.h`) is an `enum class`:

```cpp
enum class Axis : int { Axial = 0, Coronal = 1, Sagittal = 2 };
```

You write `Axis::Axial`, not a bare `0`. It is type-safe (you cannot accidentally
pass a random int where an `Axis` is expected) and self-documenting.

## 7. Reading a class header

Headers in this repo follow one shape, so you can skim them fast:

```cpp
class SegmentEditor {
public:
    // 1. The public API: what callers are allowed to do. Read this first.
    void threshold(float low_hu, float high_hu);
    ...
private:
    // 2. The private state: the data the class protects. Read this last.
    LabelVolume mask_;
    ...
};
```

Public first (the contract), private last (the secrets). Member variables end with a
trailing underscore (`mask_`, `volume_`) so you can tell a field from a local at a
glance.

## 8. The bridge: how Swift calls C++

Swift cannot call C++ methods directly, so `src/bridge/` wraps the core in plain C.
Two tricks make this work:

- **`extern "C"`** tells the C++ compiler "expose these functions with C naming so C
  and Swift can find them." Every bridge function is inside an `extern "C"` block.
- **Opaque handle.** Swift holds a `LumenVolume*` but never sees inside it - the real
  struct is defined in a private header (`src/bridge/lumen_handle.hpp`). Swift just
  passes the pointer back to each C function. This is the same idea as a file handle:
  you hold a token, the library does the work.

So the flow is: Swift calls `lumen_seg_threshold(handle, lo, hi)` ->
`lumen_bridge_segment.cpp` forwards to `handle->editor.threshold(lo, hi)` -> the C++
`SegmentEditor` does the work. The bridge functions are deliberately one line each.

## 9. The build

There is no `make` or CMake to learn here. The project is built by Swift Package
Manager (`Package.swift` at the repo root), which compiles the C++ core and the
Swift app together into one binary. Useful commands:

```bash
swift build                 # compile everything
swift run SegTest           # run the C++ segmentation unit tests (fast)
swift run LumenSlice testdata/phantom   # launch the app on the sample volume
```

Warnings are turned up (`-Wall -Wextra -Wpedantic`). Keep the build warning-clean.

## 10. Adding a C++ unit test

Tests for the core live in `tests/cpp/seg_test.cpp`. They are plain functions that
use a `CHECK(condition, "message")` macro. To add one:

1. Write a `static void test_my_thing() { ... CHECK(...); }` function.
2. Call it from `main()`.
3. Run `swift run SegTest`.

No test framework to learn. If `CHECK` fails it prints the message and the run
returns non-zero. This is the fastest feedback loop in the project - use it
constantly while changing the core.

## 11. Where to go next

Read [DESIGN_PATTERNS.md](DESIGN_PATTERNS.md) to see how these idioms combine into
the patterns that shape the code (Facade, Strategy, Command/Memento), and to get the
step-by-step recipe for adding a new segmentation tool.
