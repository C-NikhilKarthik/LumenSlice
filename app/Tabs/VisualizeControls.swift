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
                    Text("Level = brightness (the HU shown as mid-gray). "
                         + "Window = contrast (the HU span mapped black→white). "
                         + "Drag the handles to set the visible HU range, or drag "
                         + "on a slice.")
                        .font(.caption2)
                        .foregroundStyle(.secondary)
                    // The SAME two-thumb control the Segment tab uses for threshold:
                    // a window IS a range (min..max visible HU), so dragging the
                    // handles sets level = midpoint and window = width. Min/Max fields
                    // type exact edges.
                    HURangeControl(low: wlLow, high: wlHigh, bounds: wlBounds,
                                   step: 10, lowLabel: "Min", highLabel: "Max")
                    // The derived level/window the range maps to.
                    Text("Level \(Int(model.level)) · Window \(Int(model.window)) HU")
                        .font(.caption2.monospacedDigit())
                        .foregroundStyle(.secondary)
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

    // Window/level expressed as its visible HU range (Min = level − window/2,
    // Max = level + window/2) so the shared two-thumb HURangeControl can drive it.
    // Moving either edge recomputes level (the midpoint) and window (the width) in a
    // single refresh; the edges are kept at least 1 HU apart so window can't hit 0.
    private var wlLow: Binding<Float> {
        Binding(
            get: { model.level - model.window / 2 },
            set: { newMin in
                let hi = model.level + model.window / 2
                let lo = min(newMin, hi - 1)
                model.setWindowLevel(level: (lo + hi) / 2, window: max(1, hi - lo))
            })
    }
    private var wlHigh: Binding<Float> {
        Binding(
            get: { model.level + model.window / 2 },
            set: { newMax in
                let lo = model.level - model.window / 2
                let hi = max(newMax, lo + 1)
                model.setWindowLevel(level: (lo + hi) / 2, window: max(1, hi - lo))
            })
    }

    // Usable band for the window edges. A CT's raw HU span is enormous and mostly
    // unused, so cap to a clinically useful range that still covers every preset
    // (Lung reaches ~-1350, Bone ~+1150) - clamped to the volume, and widened to
    // include the current window so a handle never parks off the track.
    private var wlBounds: ClosedRange<Float> {
        let curLo = model.level - model.window / 2
        let curHi = model.level + model.window / 2
        let lo = min(max(model.huLo, -1400), curLo)
        let hi = max(min(model.huHi, 1600), curHi)
        return lo...hi
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
