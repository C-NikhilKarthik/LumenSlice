import SwiftUI
import AppKit

// MARK: - AppKit glass helpers
//
// Native macOS vibrancy + window chrome tweaks, shared by the shell. Extracted
// verbatim from the original ContentView during the tab-shell refactor.

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

// Reaches up to the hosting NSWindow to front the window on launch. We keep a
// STANDARD opaque titlebar (no fullSizeContentView, no transparency) so the toolbar
// reads as a solid top strip with a separator line and the workspace sits cleanly
// below it, rather than bleeding up behind the traffic-light buttons.
struct WindowAccessor: NSViewRepresentable {
    func makeNSView(context: Context) -> NSView {
        let view = NSView()
        DispatchQueue.main.async {
            guard let window = view.window else { return }
            // The window title is the app name and shows on the LEFT of the titlebar.
            // The toolbar's principal (center) item shows only the active tab name, so
            // "LumenSlice" appears exactly once.
            window.title = "LumenSlice"
            window.titleVisibility = .visible
            // NOTE: do NOT set isMovableByWindowBackground — it makes AppKit steal
            // drags from controls like the slice sliders and move the window instead.
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
