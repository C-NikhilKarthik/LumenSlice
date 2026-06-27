import SwiftUI
import AppKit

// Captures the low-level pointer input over a slice pane that SwiftUI gestures
// can't see, and reports it as intent:
//   - mouse-wheel / two-finger scroll -> discrete slice steps (onStep)
//   - live pointer position           -> the paint brush cursor (onMove)
//   - right-button drag               -> zoom (onZoomBegin sets the anchor,
//                                         onZoom feeds vertical drag deltas)
//   - Shift held + hover              -> cross-reference (onShiftLocate recenters,
//                                         onShiftChange toggles the crosshair)
// SwiftUI has no scroll-wheel or right-drag gesture and its hover tracking is
// unreliable under an overlapping NSView, so this wraps an NSView with local event
// monitors. The view is transparent to clicks (hitTest returns nil), so
// tap-to-locate, window/level drag, and paint drags still reach SwiftUI underneath;
// only the right-drag (zoom) is consumed, which also suppresses the context menu.
struct CanvasInputCatcher: NSViewRepresentable {
    let onStep: (Int) -> Void              // signed number of slices to move
    var onMove: ((CGPoint?) -> Void)?      // pointer in view coords, nil when outside
    var onZoomBegin: ((CGPoint) -> Void)?  // right-down point (zoom anchor)
    var onZoom: ((CGFloat) -> Void)?       // right-drag dy: up (+) = zoom in
    var onShiftLocate: ((CGPoint) -> Void)? // Shift+move point over the pane
    var onShiftChange: ((Bool) -> Void)?   // Shift key pressed/released

    func makeNSView(context: Context) -> CanvasInputNSView {
        let view = CanvasInputNSView()
        apply(to: view)
        return view
    }

    func updateNSView(_ view: CanvasInputNSView, context: Context) {
        apply(to: view)
    }

    private func apply(to view: CanvasInputNSView) {
        view.onStep = onStep
        view.onMove = onMove
        view.onZoomBegin = onZoomBegin
        view.onZoom = onZoom
        view.onShiftLocate = onShiftLocate
        view.onShiftChange = onShiftChange
    }

    static func dismantleNSView(_ view: CanvasInputNSView, coordinator: ()) {
        view.teardown()
    }
}

final class CanvasInputNSView: NSView {
    var onStep: ((Int) -> Void)?
    var onMove: ((CGPoint?) -> Void)?
    var onZoomBegin: ((CGPoint) -> Void)?
    var onZoom: ((CGFloat) -> Void)?
    var onShiftLocate: ((CGPoint) -> Void)?
    var onShiftChange: ((Bool) -> Void)?
    private var scrollMonitor: Any?
    private var moveMonitor: Any?
    private var zoomMonitor: Any?
    private var flagsMonitor: Any?
    private var accumulated: CGFloat = 0
    private let threshold: CGFloat = 8
    // A right-drag that began inside this pane keeps zooming even if the cursor
    // wanders out, so the gesture doesn't break at the pane edge.
    private var isZooming = false
    private var lastZoomY: CGFloat = 0

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
                let inside = self.bounds.contains(p)
                self.onMove?(inside ? p : nil)
                // Cross-reference is a hover gesture (no button), so only .mouseMoved
                // with Shift held triggers it - this never collides with a W/L
                // left-drag or a paint stroke, which are .leftMouseDragged.
                if inside, event.type == .mouseMoved,
                   event.modifierFlags.contains(.shift) {
                    self.onShiftLocate?(p)
                }
                return event // never consume pointer movement
            }
        }
        if zoomMonitor == nil {
            zoomMonitor = NSEvent.addLocalMonitorForEvents(
                matching: [.rightMouseDown, .rightMouseDragged, .rightMouseUp]) {
                [weak self] event in
                guard let self, let w = self.window, event.window == w else { return event }
                return self.handleRightMouse(event)
            }
        }
        if flagsMonitor == nil {
            flagsMonitor = NSEvent.addLocalMonitorForEvents(matching: .flagsChanged) {
                [weak self] event in
                guard let self, let w = self.window, event.window == w else { return event }
                self.onShiftChange?(event.modifierFlags.contains(.shift))
                return event
            }
        }
    }

    // Right-drag = zoom. Down inside the pane sets the anchor and starts the
    // gesture; each drag reports the vertical delta in window coords (up is
    // positive). Consumed so macOS shows no context menu mid-gesture.
    private func handleRightMouse(_ event: NSEvent) -> NSEvent? {
        switch event.type {
        case .rightMouseDown:
            let p = convert(event.locationInWindow, from: nil)
            guard bounds.contains(p) else { return event }
            isZooming = true
            lastZoomY = event.locationInWindow.y
            onZoomBegin?(p)
            return nil
        case .rightMouseDragged:
            guard isZooming else { return event }
            let y = event.locationInWindow.y
            onZoom?(y - lastZoomY)
            lastZoomY = y
            return nil
        case .rightMouseUp:
            guard isZooming else { return event }
            isZooming = false
            return nil
        default:
            return event
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
        if let zoomMonitor { NSEvent.removeMonitor(zoomMonitor) }
        if let flagsMonitor { NSEvent.removeMonitor(flagsMonitor) }
        scrollMonitor = nil
        moveMonitor = nil
        zoomMonitor = nil
        flagsMonitor = nil
    }

    deinit { teardown() }

    // Transparent to mouse clicks/drags — only the event monitors use this view.
    override func hitTest(_ point: NSPoint) -> NSView? { nil }
}
