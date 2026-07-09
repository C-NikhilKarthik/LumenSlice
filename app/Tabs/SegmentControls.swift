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
        case .paint, .erase: brushSection
        }
    }

    private var thresholdSection: some View {
        Section("Threshold (HU)") {
            Text("Label every voxel in this HU range into the active segment. "
                 + "Drag to update live.")
                .font(.caption2)
                .foregroundStyle(.secondary)
            huRow("Low", value: $seg.thresholdLo)
            huRow("High", value: $seg.thresholdHi)
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
        Section("Region grow") {
            Text("Click a structure in any slice to flood-fill connected voxels "
                 + "within the tolerance of the clicked voxel.")
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
            Text("Paint seed strokes on two or more segments — include a background "
                 + "segment to contain the growth — then grow them competitively to "
                 + "fill the region between the seeds.")
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
            Button {
                seg.growFromSeeds()
            } label: {
                Label("Grow from seeds", systemImage: "aqi.medium")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(.bordered)
            .controlSize(.small)
            .disabled(seg.voxelCount == 0)
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

    private func huRow(_ title: String, value: Binding<Float>) -> some View {
        let lo = model.huLo
        let hi = max(model.huHi, lo + 1)
        return VStack(alignment: .leading, spacing: 4) {
            HStack {
                Text(title)
                Spacer()
                TextField(title, value: value,
                          format: .number.precision(.fractionLength(0)))
                    .labelsHidden()
                    .frame(width: 64)
                    .multilineTextAlignment(.trailing)
                    .textFieldStyle(.roundedBorder)
            }
            Slider(value: value, in: lo...hi)
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

// One segment list row: visibility eye, colour well, editable name, voxel count,
// active selection, and delete. Tapping the row (outside the controls) makes it
// active. Name edits commit on Return / focus loss to avoid per-keystroke churn.
private struct SegmentListRow: View {
    let row: SegmentRow
    let isActive: Bool
    let seg: SegmentationModel

    @State private var editingName: String = ""
    @State private var showColors = false
    @FocusState private var nameFocused: Bool

    var body: some View {
        HStack(spacing: 8) {
            Button {
                seg.setVisible(row.id, !row.visible)
            } label: {
                Image(systemName: row.visible ? "eye.fill" : "eye.slash")
                    .foregroundStyle(row.visible ? .primary : .secondary)
            }
            .buttonStyle(.borderless)

            // A plain swatch + popover palette, NOT SwiftUI's ColorPicker — the
            // latter drives the shared NSColorPanel, which lingers and re-opens on
            // every launch.
            Button {
                showColors.toggle()
            } label: {
                Circle()
                    .fill(row.color)
                    .frame(width: 15, height: 15)
                    .overlay(Circle().strokeBorder(.white.opacity(0.35), lineWidth: 1))
            }
            .buttonStyle(.borderless)
            .popover(isPresented: $showColors, arrowEdge: .bottom) {
                ColorPalettePopover(selected: row.color) { picked in
                    seg.setColor(row.id, picked)
                    showColors = false
                }
            }

            TextField("Name", text: $editingName)
                .textFieldStyle(.plain)
                .focused($nameFocused)
                .onSubmit { commitName() }
                .onChange(of: nameFocused) { focused in if !focused { commitName() } }

            Spacer(minLength: 4)

            Text(row.voxels.formatted())
                .font(.caption2.monospacedDigit())
                .foregroundStyle(.secondary)

            Button {
                seg.removeSegment(row.id)
            } label: {
                Image(systemName: "trash")
                    .foregroundStyle(.secondary)
            }
            .buttonStyle(.borderless)
        }
        .padding(.vertical, 2)
        .padding(.horizontal, 6)
        .background(isActive ? Color.accentColor.opacity(0.18) : Color.clear,
                    in: RoundedRectangle(cornerRadius: 6))
        .contentShape(Rectangle())
        .onTapGesture { seg.setActive(row.id) }
        .onAppear { editingName = row.name }
        .onChange(of: row.name) { newName in if !nameFocused { editingName = newName } }
    }

    // Explicitly main-actor: older Swift toolchains (the CI runner) don't infer
    // MainActor isolation for a View's non-body method, so the call to the
    // @MainActor `seg.rename` would otherwise fail to build there.
    @MainActor
    private func commitName() {
        if editingName != row.name { seg.rename(row.id, to: editingName) }
    }
}

// A small grid of preset colour swatches shown in a popover. Replaces SwiftUI's
// ColorPicker so we never touch the shared NSColorPanel (which otherwise lingers
// and re-opens on every launch).
private struct ColorPalettePopover: View {
    let selected: Color
    let onPick: (Color) -> Void

    private let columns = Array(repeating: GridItem(.fixed(26), spacing: 8), count: 4)

    var body: some View {
        LazyVGrid(columns: columns, spacing: 8) {
            ForEach(Array(SegmentationModel.paletteColors.enumerated()), id: \.offset) {
                _, color in
                Button {
                    onPick(color)
                } label: {
                    Circle()
                        .fill(color)
                        .frame(width: 22, height: 22)
                        .overlay(
                            Circle().strokeBorder(.primary.opacity(0.6), lineWidth: 2)
                                .opacity(color == selected ? 1 : 0))
                }
                .buttonStyle(.borderless)
            }
        }
        .padding(12)
    }
}
