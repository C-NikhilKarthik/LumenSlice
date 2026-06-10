import SwiftUI
import AppKit
import Foundation

// A SwiftPM executable isn't a bundled .app, so we nudge the process into a
// regular foreground GUI app and bring its window to front on launch.
final class AppDelegate: NSObject, NSApplicationDelegate {
    func applicationDidFinishLaunching(_ notification: Notification) {
        NSApp.setActivationPolicy(.regular)
        bringToFront()
        // The window may not exist on the very first runloop tick; retry shortly.
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.2) { self.bringToFront() }
    }

    func applicationDidBecomeActive(_ notification: Notification) {
        NSApp.windows.forEach { $0.makeKeyAndOrderFront(nil) }
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        true
    }

    private func bringToFront() {
        NSApp.activate(ignoringOtherApps: true)
        NSApp.windows.forEach { $0.makeKeyAndOrderFront(nil) }
    }
}

@main
struct LumenSliceApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var appDelegate
    @StateObject private var model = VolumeModel()

    init() {
        // When running from a distributed .app bundle, DCMTK can't find its data
        // dictionary at the Homebrew path. Point it at the copy we bundle in
        // Resources (required to parse Implicit-VR DICOM). Must run before the
        // first DICOM load. In dev (swift run) the file isn't present, so DCMTK
        // falls back to its compiled-in default path.
        if let res = Bundle.main.resourcePath {
            let dic = res + "/dicom.dic"
            if FileManager.default.fileExists(atPath: dic) {
                setenv("DCMDICTPATH", dic, 1)
            }
        }
    }

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(model)
                .frame(minWidth: 1000, minHeight: 660)
                .onAppear {
                    // Auto-load a folder passed on the command line.
                    let args = CommandLine.arguments
                    if args.count > 1 {
                        model.load(path: args[1])
                    }
                }
        }
        .windowStyle(.titleBar)
        .windowToolbarStyle(.unified)
    }
}
