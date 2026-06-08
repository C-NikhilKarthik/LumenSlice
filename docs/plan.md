# Master Blueprint: Implementation Requirements

## 1. File Handling & Parsing Pipelines

- **Target Ingestion Mechanics:** Build a fast local folder crawler using `<filesystem>`. Filter files by verifying the standard 4-byte `"DICM"` signature pattern located at byte offset 128.
- **Sorting Protocol:** Sort raw string path arrays explicitly by extracting Patient Image Orientation `(0020,0037)` and Image Position `(0020,0032)` vectors. This guarantees true geometric slicing continuity along the Z-axis, regardless of random input file naming strings.

## 2. Segmentation Algorithmic Layouts

- **Multi-Threaded Density Extraction:** Implement the threshold routine using raw multi-threading primitives:

  ```cpp
  void ApplyThreshold(const float* source, uint8_t* dest, size_t size, float min_hu, float max_hu) {
      // Generated via multi-threaded std::jthread blocks or simple OpenMP parallel blocks
      for (size_t i = 0; i < size; ++i) {
          dest[i] = (source[i] >= min_hu && source[i] <= max_hu) ? 0x01 : 0x00;
      }
  }
  ```

- **Raycast Multi-Axis Vector Mapping:** Convert real viewport cursor positions into native 3D buffer space indices by scaling pixel dimensions against active viewport width/height dimensions multiplied by physical voxel spacing scales ($dx, dy, dz$).
- **Connected Level Tracing:** Execute seed-growing commands via a localized 3D Breadth-First Search (BFS) tracking queue structure. Compare target neighbor voxels using a 6-way adjacency index profile, matching density constraints within localized margin targets.

## 3. Geometry Generation

- **Marching Cubes Loop:** Evaluate the binary mask volume using independent $2\times2\times2$ local voxel groupings. Match surface bounds to standard Lorensen lookup arrays to generate accurate index positions and normal vectors.
- **Binary STL File Export:** Stream completed geometry matrices straight to disk without intermediate structural conversions. Ensure bytes follow standard structure layout constraints:
  - 80-byte header string.
  - 4-byte unsigned integer indicating the total triangle count.
  - 50-byte continuous packet chunks containing structural normal and vertex vector sets.
