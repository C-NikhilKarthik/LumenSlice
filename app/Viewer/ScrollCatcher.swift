import SwiftUI
import AppKit

// Captures mouse-wheel / two-finger scroll over a slice pane and reports discrete
// slice steps. SwiftUI has no scroll-wheel gesture, so this wraps an NSView that
// installs a local scroll-event monitor. The view is transparent to mouse clicks
// (hitTest returns nil), so tap-to-locate and window/level drag still reach the
// SwiftUI layer underneath.
struct ScrollCatcher: NSViewRepresentable {
    let onStep: (Int) -> Void // signed number of slices to move

    func makeNSView(context: Context) -> ScrollCatchNSView {
        let view = ScrollCatchNSView()
        view.onStep = onStep
        return view
    }

    func updateNSView(_ view: ScrollCatchNSView, context: Context) {
        view.onStep = onStep
    }

    static func dismantleNSView(_ view: ScrollCatchNSView, coordinator: ()) {
        view.teardown()
    }
}

final class ScrollCatchNSView: NSView {
    var onStep: ((Int) -> Void)?
    private var monitor: Any?
    private var accumulated: CGFloat = 0
    private let threshold: CGFloat = 8

    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        guard monitor == nil, window != nil else { return }
        monitor = NSEvent.addLocalMonitorForEvents(matching: .scrollWheel) { [weak self] event in
            guard let self, let win = self.window, event.window == win else { return event }
            let p = self.convert(event.locationInWindow, from: nil)
            guard self.bounds.contains(p) else { return event }
            self.handle(event)
            return nil // consume so a parent scroll view doesn't also react
        }
    }

    private func handle(_ event: NSEvent) {
        // Precise (trackpad) deltas are small + continuous; line (mouse wheel)
        // deltas are coarse. Scale lines up so one notch ~= one slice.
        accumulated += event.scrollingDeltaY * (event.hasPreciseScrollingDeltas ? 1 : 10)
        while abs(accumulated) >= threshold {
            let step = accumulated > 0 ? 1 : -1
            accumulated -= CGFloat(step) * threshold
            onStep?(step)
        }
    }

    func teardown() {
        if let monitor { NSEvent.removeMonitor(monitor) }
        monitor = nil
    }

    deinit { teardown() }

    // Transparent to mouse clicks/drags — only the scroll monitor uses this view.
    override func hitTest(_ point: NSPoint) -> NSView? { nil }
}
