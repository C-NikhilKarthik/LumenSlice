import SwiftUI

// Visualize-tab controls: open, status, volume info, patient/study summary, and
// the window/level controls (numeric fields + steppers + coarse sliders + presets).
// Extracted from ContentView's sidebar Form during the tab-shell refactor; the
// drag-on-image gesture remains the primary W/L control (see WindowLevelDrag).
struct VisualizeControls: View {
    @EnvironmentObject var model: VolumeModel
    @State private var showingInspector = false

    var body: some View {
        Form {
            Section {
                Button {
                    chooseFolder(model)
                } label: {
                    Label("Open DICOM Folder…", systemImage: "folder")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent)
                .controlSize(.large)
            }

            Section("Status") {
                Text(model.status)
                    .font(.callout)
                    .foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }

            if model.hasVolume {
                Section("Volume") {
                    LabeledContent("Dimensions",
                                   value: "\(model.width) × \(model.height) × \(model.depth)")
                    LabeledContent("Spacing", value: String(
                        format: "%.2f / %.2f / %.2f mm",
                        model.spacing.x, model.spacing.y, model.spacing.z))
                    LabeledContent("HU range", value: String(
                        format: "%.0f … %.0f", model.huLo, model.huHi))
                }

                if let meta = model.metadata {
                    Section("Patient / Study") {
                        let m = meta.meta
                        if !m.patientName.isEmpty {
                            LabeledContent("Patient", value: m.patientName)
                        }
                        if !m.patientId.isEmpty {
                            LabeledContent("ID", value: m.patientId)
                        }
                        if !m.modality.isEmpty {
                            LabeledContent("Modality", value: m.modality)
                        }
                        if !m.studyDate.isEmpty {
                            LabeledContent("Study date", value: m.studyDate)
                        }
                        if !m.studyDescription.isEmpty {
                            LabeledContent("Study", value: m.studyDescription)
                        }
                        Button {
                            showingInspector = true
                        } label: {
                            Label("Inspect all metadata…", systemImage: "list.bullet.rectangle")
                                .frame(maxWidth: .infinity)
                        }
                        .buttonStyle(.bordered)
                        .controlSize(.small)
                    }
                }

                Section("Window / Level (HU)") {
                    Text("Drag on a slice to adjust, or set exact values here.")
                        .font(.caption2)
                        .foregroundStyle(.secondary)
                    // Ranges always span the current value so a preset (e.g. Bone
                    // L400/W1500) outside the volume's HU span doesn't get snapped
                    // back the moment the user touches the slider or stepper.
                    wlRow("Level", value: $model.level, range: levelRange, step: 10)
                    wlRow("Window", value: $model.window, range: windowRange, step: 10)
                    HStack(spacing: 6) {
                        preset("Bone", level: 400, window: 1500)
                        preset("Soft", level: 40, window: 400)
                        preset("Lung", level: -600, window: 1500)
                    }
                    .padding(.top, 2)
                }

                Section("Overlays") {
                    Toggle("Crosshair lines", isOn: $model.showCrosshair)
                    Toggle("Orientation labels", isOn: $model.showOrientationLabels)
                }
            }
        }
        .formStyle(.grouped)
        .scrollContentBackground(.hidden)   // let the glass show through
        .sheet(isPresented: $showingInspector) {
            if let meta = model.metadata {
                MetadataInspector(metadata: meta)
            }
        }
    }

    // W/L control ranges, widened to always contain the current value (presets can
    // set values outside the volume's HU span; without this the first slider touch
    // would snap them back).
    private var levelRange: ClosedRange<Float> {
        min(model.huLo, model.level)...max(model.huHi, model.level, model.huLo + 1)
    }
    private var windowRange: ClosedRange<Float> {
        1...max(model.huHi - model.huLo, model.window, 2)
    }

    // Precise W/L control: an editable numeric field + stepper (arrow keys nudge
    // by `step`) over a coarse slider. The drag-on-image gesture is the fast path;
    // this is for exact values and small adjustments.
    private func wlRow(_ title: String, value: Binding<Float>,
                       range: ClosedRange<Float>, step: Float) -> some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack {
                Text(title)
                Spacer()
                TextField(title, value: value,
                          format: .number.precision(.fractionLength(0)))
                    .labelsHidden()
                    .frame(width: 60)
                    .multilineTextAlignment(.trailing)
                    .textFieldStyle(.roundedBorder)
                Stepper(title, value: value, in: range, step: step)
                    .labelsHidden()
            }
            Slider(value: value, in: range)
        }
    }

    private func preset(_ name: String, level: Float, window: Float) -> some View {
        Button(name) {
            model.setWindowLevel(level: level, window: window)
        }
        .buttonStyle(.bordered)
        .controlSize(.small)
        .frame(maxWidth: .infinity)
    }
}
