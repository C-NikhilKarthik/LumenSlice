import SwiftUI
import AppKit
import UniformTypeIdentifiers

// Folder selection + drag-drop, shared by the shell and the empty state.
// Extracted verbatim from ContentView during the tab-shell refactor.

@MainActor
func chooseFolder(_ model: VolumeModel) {
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
func handleDrop(_ providers: [NSItemProvider], _ model: VolumeModel) -> Bool {
    guard let provider = providers.first else { return false }
    _ = provider.loadObject(ofClass: URL.self) { url, _ in
        guard let url else { return }
        Task { @MainActor in model.load(path: url.path) }
    }
    return true
}
