import SwiftUI
import AppKit
import UniformTypeIdentifiers

// MARK: - AppKit glass helpers

// Native macOS vibrancy (the translucent "glass" used by Finder/Mail sidebars).
struct VisualEffectView: NSViewRepresentable {
    var material: NSVisualEffectView.Material = .sidebar
    var blendingMode: NSVisualEffectView.BlendingMode = .behindWindow

    func makeNSView(context: Context) -> NSVisualEffectView {
        let view = NSVisualEffectView()
        view.material = material
        view.blendingMode = blendingMode
        view.state = .active
        return view
    }

    func updateNSView(_ view: NSVisualEffectView, context: Context) {
        view.material = material
        view.blendingMode = blendingMode
        view.state = .active
    }
}

// Reaches up to the hosting NSWindow to make the titlebar transparent so the
// sidebar glass flows all the way to the top edge.
struct WindowAccessor: NSViewRepresentable {
    func makeNSView(context: Context) -> NSView {
        let view = NSView()
        DispatchQueue.main.async {
            guard let window = view.window else { return }
            window.titlebarAppearsTransparent = true
            window.titleVisibility = .visible
            // NOTE: do NOT set isMovableByWindowBackground — it makes AppKit steal
            // drags from controls like the slice sliders and move the window instead.
            window.styleMask.insert(.fullSizeContentView)
            // A bare SwiftPM executable opens behind the launching app (Xcode/
            // Terminal). Pull our window to the front and focus it.
            window.center()
            window.makeKeyAndOrderFront(nil)
            NSApp.setActivationPolicy(.regular)
            NSApp.activate(ignoringOtherApps: true)
        }
        return view
    }
    func updateNSView(_ nsView: NSView, context: Context) {}
}

struct ContentView: View {
    @EnvironmentObject var model: VolumeModel
    @State private var dropTargeted = false

    var body: some View {
        NavigationSplitView {
            Sidebar()
                .navigationSplitViewColumnWidth(min: 248, ideal: 280, max: 340)
        } detail: {
            SliceBoard(dropTargeted: $dropTargeted)
                .navigationTitle("LumenSlice")
                .toolbar {
                    ToolbarItem(placement: .primaryAction) {
                        Button {
                            chooseFolder(model)
                        } label: {
                            Label("Open Folder", systemImage: "folder.badge.plus")
                        }
                    }
                }
                .toolbarBackground(.hidden, for: .windowToolbar)
                .onDrop(of: [.fileURL], isTargeted: $dropTargeted) { providers in
                    handleDrop(providers, model)
                }
        }
        .background(WindowAccessor())
    }
}

// MARK: - Sidebar

private struct Sidebar: View {
    @EnvironmentObject var model: VolumeModel
    @State private var showingInspector = false

    var body: some View {
        VStack(spacing: 0) {
            header
            Form {
                formContent
            }
            .formStyle(.grouped)
            .scrollContentBackground(.hidden) // let the glass show through
        }
        .background(
            VisualEffectView(material: .sidebar, blendingMode: .behindWindow)
                .ignoresSafeArea()
        )
        .sheet(isPresented: $showingInspector) {
            if let meta = model.metadata {
                MetadataInspector(metadata: meta)
            }
        }
    }

    private var header: some View {
        HStack(spacing: 11) {
            Image(systemName: "square.stack.3d.up.fill")
                .font(.system(size: 24, weight: .semibold))
                .foregroundStyle(.tint)
            VStack(alignment: .leading, spacing: 1) {
                Text("LumenSlice")
                    .font(.title3.weight(.semibold))
                Text("DICOM Viewer")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            Spacer()
        }
        .padding(.horizontal, 18)
        .padding(.top, 28)
        .padding(.bottom, 12)
    }

    @ViewBuilder private var formContent: some View {
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
                    wlRow("Level", value: $model.level,
                          range: model.huLo...max(model.huHi, model.huLo + 1), step: 10)
                    wlRow("Window", value: $model.window,
                          range: 1...max(model.huHi - model.huLo, 2), step: 10)
                    HStack(spacing: 6) {
                        preset("Bone", level: 400, window: 1500)
                        preset("Soft", level: 40, window: 400)
                        preset("Lung", level: -600, window: 1500)
                    }
                    .padding(.top, 2)
                }
            }
    }

    // Precise W/L control: an editable numeric field + stepper (arrow keys
    // nudge by `step`) over a coarse slider. The drag-on-image gesture is the
    // fast path; this is for exact values and small adjustments.
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

// MARK: - Slice board (Axial large, Coronal + Sagittal below)

private struct SliceBoard: View {
    @EnvironmentObject var model: VolumeModel
    @Binding var dropTargeted: Bool

    var body: some View {
        VStack(spacing: 0) {
            if model.isLoading {
                VStack(spacing: 14) {
                    ProgressView()
                        .controlSize(.large)
                    Text(model.status)
                        .foregroundStyle(.secondary)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else if model.hasVolume {
                VStack(spacing: 12) {
                    SlicePane(axis: 0)
                        .frame(maxHeight: .infinity)
                    HStack(spacing: 12) {
                        SlicePane(axis: 1)
                        SlicePane(axis: 2)
                    }
                    .frame(maxHeight: .infinity)
                }
                .padding(14)
            } else {
                EmptyState()
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(Color(nsColor: .underPageBackgroundColor))
        .overlay {
            if dropTargeted {
                RoundedRectangle(cornerRadius: 12)
                    .strokeBorder(Color.accentColor, style: StrokeStyle(lineWidth: 3, dash: [10]))
                    .padding(8)
            }
        }
    }
}

private struct SlicePane: View {
    @EnvironmentObject var model: VolumeModel
    let axis: Int

    var body: some View {
        let count = model.sliceCount(axis)
        VStack(spacing: 8) {
            HStack {
                Text(VolumeModel.axisNames[axis])
                    .font(.headline)
                Spacer()
                Text("\(model.sliceIndex[axis] + 1) / \(max(count, 1))")
                    .font(.caption)
                    .monospacedDigit()
                    .foregroundStyle(.secondary)
            }

            ZStack {
                RoundedRectangle(cornerRadius: 10)
                    .fill(.black)
                if let img = model.images[axis] {
                    Image(decorative: img, scale: 1)
                        .resizable()
                        .interpolation(.none)
                        .aspectRatio(model.physicalAspect(axis), contentMode: .fit)
                        .padding(8)
                }
            }
            .clipShape(RoundedRectangle(cornerRadius: 10))
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .contentShape(Rectangle())       // whole pane is draggable, not just the pixels
            .windowLevelDrag(model)
            .overlay(alignment: .bottomLeading) {
                Text("W \(Int(model.window))  L \(Int(model.level))")
                    .font(.caption2.monospacedDigit())
                    .padding(.horizontal, 6)
                    .padding(.vertical, 3)
                    .background(.black.opacity(0.5), in: Capsule())
                    .foregroundStyle(.white.opacity(0.9))
                    .padding(10)
            }

            Slider(
                value: Binding(
                    get: { Double(model.sliceIndex[axis]) },
                    set: { model.setSlice(axis, Int($0.rounded())) }),
                in: 0...Double(max(count - 1, 1))
            )
            .controlSize(.small)
            .disabled(count <= 1)
        }
        .padding(12)
        .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 14))
        .overlay(
            RoundedRectangle(cornerRadius: 14)
                .strokeBorder(Color.white.opacity(0.08))
        )
        .shadow(color: .black.opacity(0.25), radius: 8, y: 3)
    }
}

private struct EmptyState: View {
    @EnvironmentObject var model: VolumeModel
    var body: some View {
        VStack(spacing: 14) {
            Image(systemName: "square.stack.3d.up.slash")
                .font(.system(size: 52, weight: .thin))
                .foregroundStyle(.secondary)
            Text("No volume loaded")
                .font(.title3.weight(.medium))
            Text("Drop a DICOM folder here, or use Open Folder.")
                .foregroundStyle(.secondary)
            Button("Open DICOM Folder…") { chooseFolder(model) }
                .buttonStyle(.borderedProminent)
                .controlSize(.large)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}

// MARK: - Folder selection / drag-drop helpers

@MainActor
private func chooseFolder(_ model: VolumeModel) {
    let panel = NSOpenPanel()
    panel.canChooseDirectories = true
    panel.canChooseFiles = false
    panel.allowsMultipleSelection = false
    panel.prompt = "Open"
    panel.message = "Choose a folder containing a DICOM series"
    if panel.runModal() == .OK, let url = panel.url {
        model.load(path: url.path)
    }
}

@MainActor
private func handleDrop(_ providers: [NSItemProvider], _ model: VolumeModel) -> Bool {
    guard let provider = providers.first else { return false }
    _ = provider.loadObject(ofClass: URL.self) { url, _ in
        guard let url else { return }
        Task { @MainActor in model.load(path: url.path) }
    }
    return true
}
