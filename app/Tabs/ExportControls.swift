import SwiftUI
import AppKit
import UniformTypeIdentifiers

// Export-tab controls: save the 3D surface as a binary STL, or the current axial
// slice as a PNG. Honors what's actually available (mesh / volume loaded).
struct ExportControls: View {
    @EnvironmentObject var model: VolumeModel
    @EnvironmentObject var mesh: MeshModel
    @State private var message: String?

    var body: some View {
        Form {
            Section("3D mesh") {
                Button {
                    exportSTL()
                } label: {
                    Label("Export STL…", systemImage: "square.and.arrow.up")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent)
                .controlSize(.large)
                .disabled(mesh.triangleCount == 0)

                if mesh.triangleCount == 0 {
                    Text("Generate a 3D surface first (3D tab).")
                        .font(.caption2)
                        .foregroundStyle(.secondary)
                } else {
                    Text("\(mesh.triangleCount.formatted()) triangles ready, in mm.")
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
        panel.nameFieldStringValue = "LumenSlice.stl"
        if let stl = UTType(filenameExtension: "stl") {
            panel.allowedContentTypes = [stl]
        }
        guard panel.runModal() == .OK, let url = panel.url else { return }
        message = mesh.exportSTL(to: url)
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
