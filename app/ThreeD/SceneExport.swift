import AppKit
import UniformTypeIdentifiers

extension UTType {
    // STL has no system-provided UTType; derive one from the extension so the save
    // panel tags the file correctly.
    static let stl = UTType(filenameExtension: "stl") ?? .data
}

// Shared save helpers for the 3D view and the Export tab, so the save-panel + PNG
// encoding path lives in one place instead of being duplicated per call site.
enum SceneExport {
    // Present a save panel for `name` with a single allowed type; nil if cancelled.
    static func savePanel(name: String, type: UTType) -> URL? {
        let panel = NSSavePanel()
        panel.allowedContentTypes = [type]
        panel.nameFieldStringValue = name
        panel.canCreateDirectories = true
        return panel.runModal() == .OK ? panel.url : nil
    }

    // Encode an NSImage as PNG and write it. Returns false on any encode/write error.
    @discardableResult
    static func writePNG(_ image: NSImage, to url: URL) -> Bool {
        guard let tiff = image.tiffRepresentation,
              let rep = NSBitmapImageRep(data: tiff),
              let png = rep.representation(using: .png, properties: [:]) else { return false }
        do { try png.write(to: url); return true } catch { return false }
    }
}
