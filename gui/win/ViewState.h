// Shared, plain-data view state for the Windows Qt shell.
//
// This is presentation state only — it is owned by MainWindow and read by the
// slice views for rendering and hit-testing. It holds a *borrowed* LumenVolume*
// (owned by MainWindow's BridgeVolume); the views never free it. Keeping this in
// one struct lets the three SliceViews and the control panels stay in sync
// without threading state through every constructor.
#pragma once

#include "lumen_bridge.h"

namespace lumenwin {

class MarkupModel;

// Active segmentation tool. Mirrors the macOS SegTool enum.
enum class Tool {
    None = 0,     // no active tool: left-drag adjusts window/level
    Threshold,    // HU window fill (applied from the panel, not the canvas)
    RegionGrow,   // click a voxel to flood-fill within tolerance
    LevelTrace,   // click to add the iso-level region on that slice
    Paint,        // drag to paint a brush into the active segment
    Erase,        // drag to erase the active segment
};

struct ViewState {
    // Borrowed, not owned (MainWindow's BridgeVolume owns the handle).
    LumenVolume* volume = nullptr;

    // Window/level transfer function (HU).
    float level = 40.0f;
    float window = 400.0f;

    // Per-axis current slice index (LUMEN_AXIS_AXIAL / CORONAL / SAGITTAL).
    int sliceIndex[3] = {0, 0, 0};

    // Shared focus voxel for the crosshair / slice-intersection lines.
    int focus[3] = {0, 0, 0};

    // Overlays.
    bool showOverlay = true;      // colored segmentation mask
    bool showCrosshair = true;    // crosshair lines at the focus voxel
    bool showOrientationLabels = true;

    // Active tool + tuning (only meaningful while the Segment tab is active).
    bool segmentInteractive = false;  // true when segment editing is enabled
    Tool tool = Tool::None;
    int brushRadius = 12;             // paint/erase disk radius, slice pixels
    float tolerance = 100.0f;         // region-grow HU tolerance

    // Markups: when placing, a plain left-click on a slice drops a point.
    bool markupPlacing = false;
    MarkupModel* markups = nullptr;   // borrowed, for slice-overlay drawing

    // A heavy mask operation is running on a worker thread; slice views skip the
    // segmentation-mask overlay so they don't read the mask while it is mutated.
    bool busy = false;

    bool hasVolume() const { return volume != nullptr; }
};

}  // namespace lumenwin
