import SwiftUI
import AppKit
import UniformTypeIdentifiers

// Export-tab controls: save the 3D surface as a binary STL, or the current axial
// slice as a PNG. Honors what's actually available (mesh / volume loaded).
struct ExportControls: View {
    @EnvironmentObject var model: VolumeModel
    @EnvironmentObject var mesh: MeshModel
    @EnvironmentObject var seg: SegmentationModel
    @State private var message: String?

    // The segments that will go into the STL: visible + non-empty. Toggling a
    // segment's eye in the list below includes/excludes it from the export.
    private var exportIDs: [Int] {
        seg.segments.filter { $0.visible && $0.voxels > 0 }.map(\.id)
    }

    var body: some View {
        Form {
            // Choose which segments to export by toggling their eye — the STL is the
            // union of the visible, non-empty ones (mirrors the 3D tab).
            if !seg.segments.isEmpty {
                Section {
                    ForEach(seg.segments) { row in
                        SegmentListRow(row: row,
                                       isActive: row.id == seg.activeID,
                                       seg: seg)
                    }
                } header: {
                    InfoHeader("Segments",
                               help: "The STL is the union of the visible, non-empty "
                                   + "segments. Toggle the eye to include or exclude a "
                                   + "segment.")
                }
            }

            Section("3D mesh") {
                Button {
                    exportSTL()
                } label: {
                    Label("Export STL…", systemImage: "square.and.arrow.up")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent)
                .controlSize(.large)
                .disabled(exportIDs.isEmpty || mesh.isGenerating)

                if mesh.isGenerating {
                    Text("Generating… export is available once it finishes.")
                        .font(.caption2)
                        .foregroundStyle(.secondary)
                } else if exportIDs.isEmpty {
                    Text("Nothing to export. Segment a structure and make it visible.")
                        .font(.caption2)
                        .foregroundStyle(.secondary)
                } else {
                    Text("\(exportIDs.count) segment\(exportIDs.count == 1 ? "" : "s") "
                         + "will be exported, in mm.")
                        .font(.caption2)
                        .foregroundStyle(.secondary)
                }
            }

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

            if let message {
                Section {
                    Text(message)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }
        }
        .formStyle(.grouped)
        .scrollContentBackground(.hidden)
    }

    private func exportSTL() {
        let panel = NSSavePanel()
        panel.nameFieldStringValue = "SurgNetra.stl"
        if let stl = UTType(filenameExtension: "stl") {
            panel.allowedContentTypes = [stl]
        }
        guard panel.runModal() == .OK, let url = panel.url else { return }
        message = mesh.exportSTL(to: url, ids: exportIDs)
            ? "Saved \(url.lastPathComponent)."
            : "STL export failed."
    }

    private func exportAxialPNG() {
        guard let img = model.images[0] else { return }
        let panel = NSSavePanel()
        panel.nameFieldStringValue = "axial.png"
        panel.allowedContentTypes = [.png]
        guard panel.runModal() == .OK, let url = panel.url else { return }
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
