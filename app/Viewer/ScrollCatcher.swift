import SwiftUI
import AppKit

// Captures mouse-wheel / two-finger scroll over a slice pane (reported as discrete
// slice steps) AND the live pointer position over the pane (for the paint brush
// cursor). SwiftUI has no scroll-wheel gesture and its hover tracking is unreliable
// under an overlapping NSView, so this wraps an NSView with local event monitors.
// The view is transparent to mouse clicks (hitTest returns nil), so tap-to-locate,
// window/level drag, and paint drags still reach the SwiftUI layer underneath.
struct CanvasInputCatcher: NSViewRepresentable {
    let onStep: (Int) -> Void          // signed number of slices to move
    var onMove: ((CGPoint?) -> Void)?  // pointer in view coords, nil when outside

    func makeNSView(context: Context) -> CanvasInputNSView {
        let view = CanvasInputNSView()
        view.onStep = onStep
        view.onMove = onMove
        return view
    }

    func updateNSView(_ view: CanvasInputNSView, context: Context) {
        view.onStep = onStep
        view.onMove = onMove
    }

    static func dismantleNSView(_ view: CanvasInputNSView, coordinator: ()) {
        view.teardown()
    }
}

final class CanvasInputNSView: NSView {
    var onStep: ((Int) -> Void)?
    var onMove: ((CGPoint?) -> Void)?
    private var scrollMonitor: Any?
    private var moveMonitor: Any?
    private var accumulated: CGFloat = 0
    private let threshold: CGFloat = 8

    // Flip so our coordinate origin is top-left, matching SwiftUI's overlay space:
    // a point reported here lines up with where the brush ring is drawn.
    override var isFlipped: Bool { true }

    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        guard let win = window else { return }
        win.acceptsMouseMovedEvents = true

        if scrollMonitor == nil {
            scrollMonitor = NSEvent.addLocalMonitorForEvents(matching: .scrollWheel) {
                [weak self] event in
                guard let self, let w = self.window, event.window == w else { return event }
                let p = self.convert(event.locationInWindow, from: nil)
                guard self.bounds.contains(p) else { return event }
                self.handleScroll(event)
                return nil // consume so a parent scroll view doesn't also react
            }
        }
        if moveMonitor == nil {
            moveMonitor = NSEvent.addLocalMonitorForEvents(
                matching: [.mouseMoved, .leftMouseDragged]) { [weak self] event in
                guard let self, let w = self.window, event.window == w else { return event }
                let p = self.convert(event.locationInWindow, from: nil)
                self.onMove?(self.bounds.contains(p) ? p : nil)
                return event // never consume pointer movement
            }
        }
    }

    private func handleScroll(_ event: NSEvent) {
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
        if let scrollMonitor { NSEvent.removeMonitor(scrollMonitor) }
        if let moveMonitor { NSEvent.removeMonitor(moveMonitor) }
        scrollMonitor = nil
        moveMonitor = nil
    }

    deinit { teardown() }

    // Transparent to mouse clicks/drags — only the event monitors use this view.
    override func hitTest(_ point: NSPoint) -> NSView? { nil }
}
