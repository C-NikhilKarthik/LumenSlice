import SwiftUI

// Segment-tab controls: a multi-segment list (colour / visibility / name / active),
// the active tool (threshold / grow / paint / erase) with its tuning, island
// cleanup, and undo/redo. Threshold is live (debounced); grow seeds on a click;
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
                refineSection
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
        return Section("Threshold (HU)") {
            Text("Label every voxel in this HU window into the active segment. "
                 + "Drag either handle to update live.")
                .font(.caption2)
                .foregroundStyle(.secondary)
            HStack {
                // Typed values are clamped into the volume's HU range and kept
                // ordered (Low <= High), so a stray entry can't invert the window or
                // desync from the slider (the slider itself already clamps on drag).
                huField("Low", value: Binding(
                    get: { seg.thresholdLo },
                    set: { seg.thresholdLo = min(max($0, lo), seg.thresholdHi) }))
                Spacer()
                huField("High", value: Binding(
                    get: { seg.thresholdHi },
                    set: { seg.thresholdHi = max(min($0, hi), seg.thresholdLo) }))
            }
            RangeSlider(low: $seg.thresholdLo, high: $seg.thresholdHi, bounds: lo...hi)
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
        }
    }

    private var regionGrowSection: some View {
        Section("Fill (flood)") {
            Text("Click a structure in any slice to flood-fill connected voxels "
                 + "within the tolerance of the clicked voxel. Each click fills; "
                 + "this is not the seed brush for Grow from seeds (use Paint for that).")
                .font(.caption2)
                .foregroundStyle(.secondary)
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
        }
    }

    private var levelTraceSection: some View {
        Section("Level Trace") {
            Text("Click a bright structure on any slice to select its whole level "
                 + "set: every connected pixel at or above the clicked HU is added to "
                 + "the active segment. Works on the clicked slice only.")
                .font(.caption2)
                .foregroundStyle(.secondary)
        }
    }

    private var brushSection: some View {
        Section(seg.tool == .erase ? "Erase brush" : "Paint brush") {
            Text(seg.tool == .erase
                 ? "Drag over the slice to erase the active segment."
                 : "Drag over the slice to paint the active segment.")
                .font(.caption2)
                .foregroundStyle(.secondary)
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
        }
    }

    // MARK: - Refine (margin + smooth)

    private var refineSection: some View {
        Section("Refine") {
            Text("Grow/shrink the active segment by one voxel, or smooth its edges.")
                .font(.caption2)
                .foregroundStyle(.secondary)
            HStack(spacing: 8) {
                Button { seg.shrinkMargin() } label: {
                    Label("Shrink", systemImage: "minus.magnifyingglass")
                        .frame(maxWidth: .infinity)
                }
                Button { seg.growMargin() } label: {
                    Label("Grow", systemImage: "plus.magnifyingglass")
                        .frame(maxWidth: .infinity)
                }
            }
            Button { seg.smooth() } label: {
                Label("Smooth edges", systemImage: "wand.and.rays")
                    .frame(maxWidth: .infinity)
            }
        }
        .disabled(seg.activeID == 0)
        .buttonStyle(.bordered)
        .controlSize(.small)
    }

    // MARK: - Grow from seeds

    private var growSeedsSection: some View {
        Section("Grow from seeds") {
            Text("With the Paint tool, dab a seed inside two or more segments — one "
                 + "per structure plus a background — then click Grow. Growth only "
                 + "happens when you click; nothing grows while you paint.")
                .font(.caption2)
                .foregroundStyle(.secondary)
            VStack(alignment: .leading, spacing: 4) {
                HStack {
                    Text("Iterations")
                    Spacer()
                    Text("\(seg.growSeedIters)")
                        .foregroundStyle(.secondary)
                        .monospacedDigit()
                }
                Slider(value: Binding(get: { Double(seg.growSeedIters) },
                                      set: { seg.growSeedIters = Int($0) }),
                       in: 5...100, step: 1)
            }
            // Gate exactly like Slicer: at least two segments must carry seeds.
            if !seg.canGrowFromSeeds {
                Label("Seed at least two segments to enable "
                      + "(\(seg.seededSegmentCount)/2 seeded).",
                      systemImage: "exclamationmark.triangle")
                    .font(.caption2)
                    .foregroundStyle(.secondary)
            }
            Button {
                seg.growFromSeeds()
            } label: {
                Label("Grow from seeds", systemImage: "aqi.medium")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(.bordered)
            .controlSize(.small)
            .disabled(!seg.canGrowFromSeeds)
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

    // A compact "Low: [ 150 ]" numeric field. The slider itself is the two-thumb
    // RangeSlider above these fields; these just allow typing an exact HU value.
    private func huField(_ title: String, value: Binding<Float>) -> some View {
        HStack(spacing: 6) {
            Text(title).foregroundStyle(.secondary)
            TextField(title, value: value,
                      format: .number.precision(.fractionLength(0)))
                .labelsHidden()
                .frame(width: 64)
                .multilineTextAlignment(.trailing)
                .textFieldStyle(.roundedBorder)
        }
    }

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
