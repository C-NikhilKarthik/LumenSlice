import SwiftUI

// Segment-tab controls: pick a tool, tune it, and see the mask grow. Threshold is
// live (debounced); region-grow seeds on a click in the canvas. Paint/erase +
// undo arrive in P1b.
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
                Section("Tool") {
                    Picker("Tool", selection: $seg.tool) {
                        ForEach(SegTool.allCases) { tool in
                            Label(tool.title, systemImage: tool.icon).tag(tool)
                        }
                    }
                    .pickerStyle(.segmented)
                    .labelsHidden()
                }

                switch seg.tool {
                case .threshold:
                    thresholdSection
                case .regionGrow:
                    regionGrowSection
                }

                Section("Mask") {
                    Toggle("Show overlay", isOn: $seg.showOverlay)
                    LabeledContent("Voxels", value: seg.voxelCount.formatted())
                    Button(role: .destructive) {
                        seg.clear()
                    } label: {
                        Label("Clear mask", systemImage: "trash")
                            .frame(maxWidth: .infinity)
                    }
                    .buttonStyle(.bordered)
                    .controlSize(.small)
                    .disabled(seg.voxelCount == 0)
                }

                Section {
                    Text("Build a 3D surface from this mask in the 3D tab.")
                        .font(.caption2)
                        .foregroundStyle(.secondary)
                }
            }
        }
        .formStyle(.grouped)
        .scrollContentBackground(.hidden)
    }

    private var thresholdSection: some View {
        Section("Threshold (HU)") {
            Text("Label every voxel in this HU range. Drag to update live.")
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
