import SwiftUI
import AppKit
import UniformTypeIdentifiers

// Export-tab controls: save the 3D surface as binary STL (choosing WHICH segments to
// include, fused into one file or one file per segment), or the current axial slice
// as a PNG. Honors what's actually available (segments / volume loaded).
struct ExportControls: View {
    @EnvironmentObject var model: VolumeModel
    @EnvironmentObject var mesh: MeshModel
    @EnvironmentObject var seg: SegmentationModel
    @State private var excluded: Set<Int> = []
    @State private var perSegmentFiles = false
    @State private var message: String?

    // Only non-empty segments can be surfaced; excluded ones are held out.
    private var exportable: [SegmentRow] { seg.segments.filter { $0.voxels > 0 } }
    private var included: [SegmentRow] { exportable.filter { !excluded.contains($0.id) } }

    var body: some View {
        Form {
            segmentSection
            meshSection
            sliceSection
            if let message {
                Section {
                    Text(message).font(.caption).foregroundStyle(.secondary)
                }
            }
        }
        .formStyle(.grouped)
        .scrollContentBackground(.hidden)
        .onChange(of: model.hasVolume) { _ in
            // Segment ids (the C++ label bytes) are reused across scans, so a stale
            // exclusion from a previous scan must not silently hide a new segment.
            excluded.removeAll()
            perSegmentFiles = false
            message = nil
        }
    }

    private var segmentSection: some View {
        Section("Segments to export") {
            if exportable.isEmpty {
                Text("No segmented structures yet. Segment a structure first.")
                    .font(.caption2)
                    .foregroundStyle(.secondary)
            } else {
                ForEach(exportable) { row in
                    Toggle(isOn: includeBinding(row.id)) {
                        HStack(spacing: 8) {
                            Circle().fill(row.color).frame(width: 12, height: 12)
                            Text(row.name)
                            Spacer()
                            Text("\(row.voxels.formatted()) vox")
                                .font(.caption2.monospacedDigit())
                                .foregroundStyle(.secondary)
                        }
                    }
                }
                if exportable.count > 1 {
                    HStack {
                        Button("All") { excluded.removeAll() }
                        Button("None") { excluded = Set(exportable.map { $0.id }) }
                    }
                    .buttonStyle(.borderless)
                    .controlSize(.small)
                    .font(.caption2)
                }
            }
        }
    }

    private var meshSection: some View {
        Section("3D mesh (STL)") {
            Toggle("One file per segment", isOn: $perSegmentFiles)
                .disabled(included.count < 2)
            Button {
                exportSTL()
            } label: {
                Label(perSegmentFiles ? "Export STL files…" : "Export STL…",
                      systemImage: "square.and.arrow.up")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(.borderedProminent)
            .controlSize(.large)
            .disabled(included.isEmpty || mesh.isGenerating)

            if mesh.isGenerating {
                caption("Generating… export is available once it finishes.")
            } else if included.isEmpty {
                caption("Select at least one segment to export.")
            } else {
                caption("\(included.count) segment(s) selected, exported in mm.")
            }
        }
    }

    private var sliceSection: some View {
        Section("Slice") {
            Button {
                exportAxialPNG()
            } label: {
                Label("Export axial PNG…", systemImage: "photo")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(.bordered)
            .disabled(!model.hasVolume || model.images[0] == nil)
        }
    }

    private func caption(_ text: String) -> some View {
        Text(text).font(.caption2).foregroundStyle(.secondary)
    }

    private func includeBinding(_ id: Int) -> Binding<Bool> {
        Binding(get: { !excluded.contains(id) },
                set: { on in if on { excluded.remove(id) } else { excluded.insert(id) } })
    }

    private func exportSTL() {
        let chosen = included
        let ids = chosen.map { $0.id }
        guard !ids.isEmpty else { return }
        // "One file per segment" only applies with 2+ segments; below that the toggle
        // is disabled, so a single segment always exports as one fused file.
        if perSegmentFiles && chosen.count >= 2 {
            let panel = NSOpenPanel()
            panel.canChooseDirectories = true
            panel.canChooseFiles = false
            panel.canCreateDirectories = true
            panel.prompt = "Export Here"
            guard panel.runModal() == .OK, let dir = panel.url else { return }
            let r = mesh.exportSTLPerSegment(into: dir, segments: chosen)
            if r.written == 0 {
                message = "STL export failed."
            } else if r.written < r.requested {
                message = "Saved \(r.written) of \(r.requested) STL files to "
                    + "\(dir.lastPathComponent) (\(r.requested - r.written) had no surface)."
            } else {
                message = "Saved \(r.written) STL file(s) to \(dir.lastPathComponent)."
            }
        } else {
            guard let url = SceneExport.savePanel(name: "LumenSlice.stl", type: .stl)
            else { return }
            message = mesh.exportSTL(to: url, ids: ids)
                ? "Saved \(url.lastPathComponent) (\(ids.count) segment(s))."
                : "STL export failed (the selection produced no surface)."
        }
    }

    private func exportAxialPNG() {
        guard let img = model.images[0],
              let url = SceneExport.savePanel(name: "axial.png", type: .png) else { return }
        let rep = NSBitmapImageRep(cgImage: img)
        if let data = rep.representation(using: .png, properties: [:]) {
            do {
                try data.write(to: url)
                message = "Saved \(url.lastPathComponent)."
            } catch {
                message = "PNG export failed: \(error.localizedDescription)"
            }
        }
    }
}
