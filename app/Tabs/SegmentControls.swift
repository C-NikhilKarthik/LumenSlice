import SwiftUI

// Segment-tab controls: a multi-segment list (colour / visibility / name / active),
// the active tool (threshold / grow / paint / erase) with its tuning and
// undo/redo. Threshold is live (debounced); grow seeds on a click;
// paint/erase drag a brush in the canvas.
struct SegmentControls: View {
    @EnvironmentObject var model: VolumeModel
    @EnvironmentObject var seg: SegmentationModel

    var body: some View {
        Form {
            if !model.hasVolume {
                Section {
                    Text("Open a DICOM folder to segment.")
                        .font(.callout)
                        .foregroundStyle(.secondary)
                }
            } else {
                segmentsSection
                toolSection
                toolDetailSection
                maskSection
                growSeedsSection
                editSection
            }
        }
        .formStyle(.grouped)
        .scrollContentBackground(.hidden)
    }

    // MARK: - Segments

    private var segmentsSection: some View {
        Section {
            ForEach(seg.segments) { row in
                SegmentListRow(row: row,
                               isActive: row.id == seg.activeID,
                               seg: seg)
            }
            if seg.segments.isEmpty {
                Text("No segments. Add one to start.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
        } header: {
            HStack {
                Text("Segments")
                Spacer()
                Button {
                    seg.addSegment()
                } label: {
                    Label("Add", systemImage: "plus.circle.fill")
                        .labelStyle(.iconOnly)
                }
                .buttonStyle(.borderless)
            }
        }
    }

    // MARK: - Tool

    private var toolSection: some View {
        Section("Tool") {
            Picker("Tool", selection: $seg.tool) {
                ForEach(SegTool.allCases) { tool in
                    Label(tool.title, systemImage: tool.icon).tag(tool)
                }
            }
            .pickerStyle(.segmented)
            .labelsHidden()
            .disabled(seg.activeID == 0)
        }
    }

    @ViewBuilder private var toolDetailSection: some View {
        switch seg.tool {
        case .threshold: thresholdSection
        case .regionGrow: regionGrowSection
        case .levelTrace: levelTraceSection
        case .paint, .erase: brushSection
        }
    }

    private var thresholdSection: some View {
        let lo = model.huLo
        let hi = max(model.huHi, lo + 1)
        return Section {
            // Shared HU-range control (same one the Visualize tab uses for W/L).
            HURangeControl(low: $seg.thresholdLo, high: $seg.thresholdHi,
                           bounds: lo...hi)
                .padding(.top, 2)
            HStack(spacing: 6) {
                presetButton("Bone", lo: 300, hi: 3000)
                presetButton("Soft", lo: 40, hi: 80)
                presetButton("Lung", lo: -900, hi: -400)
            }
            .padding(.top, 2)
            Button {
                seg.applyOtsu()
            } label: {
                Label("Otsu auto-threshold", systemImage: "wand.and.stars")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(.bordered)
            .controlSize(.small)
            .disabled(seg.activeID == 0)
            Button {
                seg.commitThreshold()
            } label: {
                Label("Apply threshold to 3D", systemImage: "checkmark.circle.fill")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(.borderedProminent)
            .controlSize(.small)
            .disabled(seg.activeID == 0 || seg.isBusy)
        } header: {
            InfoHeader("Threshold (HU)",
                       help: "Preview this HU range in the three slice views. Apply "
                           + "commits it to the active segment and 3D.")
        }
    }

    private var maskSection: some View {
        Section {
            if seg.maskActive {
                // Active-mask indicator + edit / turn-off controls.
                HStack(spacing: 6) {
                    Image(systemName: "circle.fill")
                        .font(.system(size: 8))
                        .foregroundStyle(.green)
                    Text("Painting limited to \(Int(seg.maskRange.lowerBound)) to "
                         + "\(Int(seg.maskRange.upperBound)) HU")
                        .font(.caption)
                    Spacer()
                }
                HStack(spacing: 8) {
                    // Adjust: drop the mask and return to Threshold with the same
                    // range loaded, so the user can retune and re-apply in one step.
                    Button {
                        seg.editMaskRange()
                    } label: {
                        Label("Adjust range", systemImage: "slider.horizontal.3")
                            .frame(maxWidth: .infinity)
                    }
                    .buttonStyle(.bordered)
                    .controlSize(.small)
                    .disabled(seg.isBusy)
                    Button(role: .destructive) {
                        seg.deactivateMask()
                    } label: {
                        Label("Turn off", systemImage: "xmark.circle")
                            .frame(maxWidth: .infinity)
                    }
                    .buttonStyle(.bordered)
                    .controlSize(.small)
                    .disabled(seg.isBusy)
                }
            } else {
                Button {
                    seg.applyMask()
                } label: {
                    Label("Use as paint mask", systemImage: "paintbrush.pointed.fill")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent)
                .disabled(seg.activeID == 0 || seg.isBusy)
                Text("Limits paint and fill to the current threshold HU range, so you "
                     + "can brush freely without spilling into other tissue.")
                    .font(.caption2)
                    .foregroundStyle(.secondary)
            }
        } header: {
            InfoHeader("Mask",
                       help: "An editable-area mask confines paint and fill to a HU "
                           + "range. A badge on the canvas shows it is active.")
        }
    }

    private var regionGrowSection: some View {
        Section {
            VStack(alignment: .leading, spacing: 4) {
                HStack {
                    Text("Tolerance")
                    Spacer()
                    Text("± \(Int(seg.tolerance)) HU")
                        .foregroundStyle(.secondary)
                        .monospacedDigit()
                }
                Slider(value: $seg.tolerance, in: 1...1000, step: 1)
            }
        } header: {
            InfoHeader("Fill (flood)",
                       help: "Click a structure in any slice to flood-fill connected "
                           + "voxels within the tolerance of the clicked voxel. Each "
                           + "click fills; this is not the seed brush for Grow from "
                           + "seeds (use Paint for that).")
        }
    }

    private var levelTraceSection: some View {
        Section {
            Label("Click a bright structure on a slice.",
                  systemImage: "hand.point.up.left")
                .font(.caption)
                .foregroundStyle(.secondary)
        } header: {
            InfoHeader("Level Trace",
                       help: "Click a bright structure on any slice to select its whole "
                           + "level set: every connected pixel at or above the clicked "
                           + "HU is added to the active segment. Works on the clicked "
                           + "slice only.")
        }
    }

    private var brushSection: some View {
        let isErase = seg.tool == .erase
        return Section {
            VStack(alignment: .leading, spacing: 4) {
                HStack {
                    Text("Brush radius")
                    Spacer()
                    Text("\(seg.brushRadius) px")
                        .foregroundStyle(.secondary)
                        .monospacedDigit()
                }
                Slider(value: Binding(get: { Double(seg.brushRadius) },
                                      set: { seg.brushRadius = Int($0) }),
                       in: 1...80, step: 1)
            }
        } header: {
            InfoHeader(isErase ? "Erase brush" : "Paint brush",
                       help: isErase
                           ? "Drag over the slice to erase the active segment."
                           : "Drag over the slice to paint the active segment.")
        }
    }

    // MARK: - Grow from seeds

    private var growSeedsSection: some View {
        Section {
            VStack(alignment: .leading, spacing: 4) {
                HStack {
                    Text("Seed locality")
                    Spacer()
                    Text(String(format: "%.1f", seg.growSeedLocality))
                        .foregroundStyle(.secondary)
                        .monospacedDigit()
                }
                Slider(value: $seg.growSeedLocality, in: 0...10, step: 0.1)
            }
            // Gate exactly like Slicer: at least two segments must carry seeds.
            if !seg.canGrowFromSeeds {
                Label("Seed at least two segments to enable "
                      + "(\(seg.seededSegmentCount)/2 seeded).",
                      systemImage: "exclamationmark.triangle")
                    .font(.caption2)
                    .foregroundStyle(.secondary)
            }
            if seg.growPreviewActive {
                HStack(spacing: 8) {
                    Button { seg.applyGrowPreview() } label: {
                        Label("Apply", systemImage: "checkmark.circle.fill")
                            .frame(maxWidth: .infinity)
                    }
                    .buttonStyle(.borderedProminent)
                    Button { seg.cancelGrowPreview() } label: {
                        Label("Cancel", systemImage: "xmark.circle")
                            .frame(maxWidth: .infinity)
                    }
                    .buttonStyle(.bordered)
                }
            } else {
                Button { seg.initializeGrowPreview() } label: {
                    Label("Initialize preview", systemImage: "sparkles")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent)
                .controlSize(.small)
                .disabled(!seg.canGrowFromSeeds)
            }
        } header: {
            InfoHeader("Grow from seeds",
                       help: "Paint a seed in each region with a different segment. "
                           + "Initialize a preview, inspect the result through the "
                           + "slices, then apply it or cancel and add more seeds. Seed "
                           + "locality: higher values keep growth closer to the painted "
                           + "seeds.")
        }
    }

    // MARK: - Edit

    private var editSection: some View {
        Section("Edit") {
            HStack(spacing: 8) {
                Button {
                    seg.undo()
                } label: {
                    Label("Undo", systemImage: "arrow.uturn.backward")
                        .frame(maxWidth: .infinity)
                }
                .disabled(!seg.canUndo)
                Button {
                    seg.redo()
                } label: {
                    Label("Redo", systemImage: "arrow.uturn.forward")
                        .frame(maxWidth: .infinity)
                }
                .disabled(!seg.canRedo)
            }
            .buttonStyle(.bordered)
            .controlSize(.small)

            Toggle("Show overlay", isOn: $seg.showOverlay)
            LabeledContent("Total voxels", value: seg.voxelCount.formatted())
            Button(role: .destructive) {
                seg.clearActive()
            } label: {
                Label("Clear active segment", systemImage: "trash")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(.bordered)
            .controlSize(.small)
            .disabled(seg.activeID == 0)
        }
    }

    // MARK: - Helpers

    private func presetButton(_ name: String, lo: Float, hi: Float) -> some View {
        Button(name) {
            seg.thresholdLo = lo
            seg.thresholdHi = hi
        }
        .buttonStyle(.bordered)
        .controlSize(.small)
        .frame(maxWidth: .infinity)
    }
}
