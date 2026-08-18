import SwiftUI
import simd

// One orthographic plane: the window/levelled HU image, an optional colored mask
// overlay, the shared crosshair, a slice scrubber, and the interaction layer.
// Navigation (3D-Slicer-style): click to locate (recenters all panes), mouse
// wheel scrolls slices, left-drag adjusts window/level. On the Segment tab the
// active tool owns the canvas instead: paint/erase brush along a drag (with a
// cursor ring), or region-grow seed on a click. A header button maximizes this
// pane to fill the viewport.
struct SlicePane: View {
    @EnvironmentObject var model: VolumeModel
    @EnvironmentObject var markup: MarkupModel
    let axis: Int
    var segment: SegmentationModel? = nil
    var isFocused: Bool = false
    var onToggleFocus: (() -> Void)? = nil

    // Live cursor position over this pane (for the brush ring), and paint-stroke
    // bookkeeping so a drag is one continuous, gap-free, single-undo stroke.
    @State private var pointer: CGPoint?
    @State private var lastPaintPixel: (px: Int, py: Int)?
    @State private var strokeActive = false

    // Per-pane zoom: 1 = fit, up to 8x, driven by a right-button drag. `zoomAnchor`
    // is the cursor point where the drag began, so the view magnifies toward what
    // is under the cursor. `pan` slides the zoomed image (middle-button / scroll-
    // wheel-button drag) and resets to zero when the pane returns to fit. Each pane
    // transforms independently (3D-Slicer behavior).
    @State private var zoom: CGFloat = 1
    @State private var zoomAnchor: CGPoint = .zero
    @State private var pan: CGSize = .zero
    @State private var showHelp = false
    private static let maxZoom: CGFloat = 8

    var body: some View {
        let count = model.sliceCount(axis)
        VStack(spacing: 8) {
            HStack {
                Text(VolumeModel.axisNames[axis])
                    .font(.headline)
                Spacer()
                Text("\(model.sliceIndex[axis] + 1) / \(max(count, 1))")
                    .font(.caption)
                    .monospacedDigit()
                    .foregroundStyle(.secondary)
                if let onToggleFocus {
                    Button(action: onToggleFocus) {
                        Image(systemName: isFocused
                              ? "arrow.down.right.and.arrow.up.left"
                              : "arrow.up.left.and.arrow.down.right")
                    }
                    .buttonStyle(.borderless)
                    .help(isFocused ? "Restore layout" : "Maximize this view")
                }
            }

            imageArea

            HStack(spacing: 6) {
                Button { model.setSlice(axis, model.sliceIndex[axis] - 1) } label: {
                    Image(systemName: "chevron.up")
                }
                .help("Previous frame")
                .buttonStyle(.borderless)
                Slider(
                    value: Binding(
                        get: { Double(model.sliceIndex[axis]) },
                        set: { model.setSlice(axis, Int($0.rounded())) }),
                    in: 0...Double(max(count - 1, 1))
                )
                .controlSize(.small)
                .disabled(count <= 1)
                Button { model.setSlice(axis, model.sliceIndex[axis] + 1) } label: {
                    Image(systemName: "chevron.down")
                }
                .help("Next frame")
                .buttonStyle(.borderless)
            }
        }
        .padding(12)
        .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 14))
        .overlay(
            RoundedRectangle(cornerRadius: 14)
                .strokeBorder(Color.white.opacity(0.08))
        )
        .shadow(color: .black.opacity(0.25), radius: 8, y: 3)
    }

    private var imageArea: some View {
        GeometryReader { geo in
            let aspect = model.physicalAspect(axis)
            let container = geo.size
            // The one rect rendering, the crosshair, the brush AND hit-testing all
            // read - zoom flows through it so nothing desyncs when magnified.
            let display = SliceCoordinates.fittedRect(
                container: container, aspect: aspect, zoom: zoom, anchor: zoomAnchor,
                pan: pan)
            ZStack {
                RoundedRectangle(cornerRadius: 10)
                    .fill(.black)
                if let img = model.images[axis], let r = display {
                    Image(decorative: img, scale: 1)
                        .resizable()
                        .interpolation(.none)
                        .frame(width: r.width, height: r.height)
                        .position(x: r.midX, y: r.midY)
                }
                if let seg = segment, seg.showOverlay, let r = display {
                    MaskOverlay(store: seg.overlayStore, axis: axis, rect: r)
                }
                if model.showCrosshair || model.shiftActive {
                    let colors = PlaneColors.forPane(axis)
                    CrosshairOverlay(
                        point: crosshairPoint(container: container, aspect: aspect),
                        rect: display,
                        verticalColor: colors.vertical,
                        horizontalColor: colors.horizontal)
                }
                if model.showOrientationLabels {
                    OrientationLabels(axis: axis, rect: display)
                }
                brushRing(fitted: display)
                markupGeometry(container: container, aspect: aspect)
            }
            .overlay(alignment: .topTrailing) {
                HStack(spacing: 2) {
                    Button { showHelp.toggle() } label: {
                        Image(systemName: "info.circle")
                    }
                    .help("How to zoom, pan, and navigate this view")
                    .popover(isPresented: $showHelp, arrowEdge: .bottom) {
                        SliceHelpPopover()
                    }
                    Button { zoomBy(0.8, container: container) } label: {
                        Image(systemName: "minus.magnifyingglass")
                    }
                    .help("Zoom out")
                    Button { resetZoom() } label: {
                        Image(systemName: "arrow.counterclockwise")
                    }
                    .help("Reset zoom")
                    Button { zoomBy(1.25, container: container) } label: {
                        Image(systemName: "plus.magnifyingglass")
                    }
                    .help("Zoom in")
                }
                .font(.caption)
                .buttonStyle(.borderless)
                .foregroundStyle(.white)
                .padding(4)
                .background(.black.opacity(0.55), in: RoundedRectangle(cornerRadius: 6))
                .padding(8)
            }
            .clipShape(RoundedRectangle(cornerRadius: 10))
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .contentShape(Rectangle())
            .modifier(SliceInteraction(pane: self, container: container))
            .overlay(CanvasInputCatcher(
                onStep: { model.setSlice(axis, model.sliceIndex[axis] + $0) },
                onMove: { pointer = $0 },
                onMagnify: { applyMagnification($0) },
                onZoomBegin: { zoomAnchor = $0 },
                onZoom: { applyZoom($0) },
                onShiftLocate: { locate(at: $0, container: container) },
                onShiftChange: { model.setShiftActive($0) },
                onPan: { d in
                    // Clamp so the zoomed image can't be dragged entirely out of the
                    // pane: the offset is bounded to half the zoomed image size, so
                    // at least its centre stays reachable. No pan at fit (zoom == 1).
                    guard zoom > 1,
                          let base = SliceCoordinates.fittedRect(
                            container: container, aspect: aspect) else { return }
                    let maxX = base.width * zoom / 2
                    let maxY = base.height * zoom / 2
                    pan.width = min(maxX, max(-maxX, pan.width + d.width))
                    pan.height = min(maxY, max(-maxY, pan.height + d.height))
                }))
            .overlay(alignment: .bottomLeading) {
                Text("W \(Int(model.window))  L \(Int(model.level))")
                    .font(.caption2.monospacedDigit())
                    .padding(.horizontal, 6)
                    .padding(.vertical, 3)
                    .background(.black.opacity(0.5), in: Capsule())
                    .foregroundStyle(.white.opacity(0.9))
                    .padding(10)
            }
            .overlay {
                if let seg = segment, seg.isBusy {
                    ZStack {
                        Color.black.opacity(0.42)
                        ProgressView("Working…")
                            .tint(.white)
                            .foregroundStyle(.white)
                    }
                }
            }
        }
    }

    // The brush footprint: a ring sized to brushRadius (slice pixels) scaled into
    // display points, tinted by the active segment (red while erasing).
    @ViewBuilder private func brushRing(fitted: CGRect?) -> some View {
        if let seg = segment, seg.tool.isBrush, let p = pointer,
           let img = model.images[axis], let rect = fitted, rect.contains(p) {
            let scale = rect.width / CGFloat(img.width)
            let diameter = CGFloat(seg.brushRadius) * scale * 2
            Circle()
                .stroke(seg.tool == .erase ? Color.red : seg.activeColor, lineWidth: 1.5)
                .frame(width: diameter, height: diameter)
                .position(p)
                .allowsHitTesting(false)
        }
    }

    // Slicer-style slice representation: control points are shown when they lie
    // on the current plane, while a line/plane is clipped to that plane. This
    // keeps the representation useful even when its points were placed on other
    // orthogonal panes.
    @ViewBuilder private func markupGeometry(container: CGSize, aspect: CGFloat) -> some View {
        ForEach(markup.markups) { m in
            if m.visible { representation(m, container: container, aspect: aspect) }
        }
        pendingPreview(container: container, aspect: aspect)
        // Pending control points retain their dots while the dashed representation
        // follows the pointer during placement.
        ForEach(Array(markup.pending.enumerated()), id: \.offset) { _, v in
            if markup.onCurrentSlice(v, axis: axis),
               let pt = markupPoint(voxel: SIMD3<Float>(Float(v.x), Float(v.y), Float(v.z)),
                                    container: container, aspect: aspect) {
                // The in-progress point, filled in the colour the finished markup will
                // take (with a white ring for contrast on any tissue).
                Circle()
                    .fill(markup.pendingColor)
                    .frame(width: 10, height: 10)
                    .overlay(Circle().strokeBorder(.white, lineWidth: 1.5))
                    .position(pt)
                    .allowsHitTesting(false)
            }
        }
    }

    @ViewBuilder private func pendingPreview(container: CGSize, aspect: CGFloat) -> some View {
        if markup.placing, !markup.pending.isEmpty,
           let cursor = pendingCursorVoxel(container: container) {
            // During placement this is a rubber-band preview. Project the
            // world-space points into every orthogonal pane, rather than
            // clipping to the pane's current slice; otherwise the preview
            // disappears from the two non-active views.
            let previewVoxels = markup.kind == .plane && markup.pending.count >= 2
                ? Array(markup.pending.prefix(2)) + [cursor]
                : [markup.pending[0], cursor]
            let projected = previewVoxels.compactMap {
                markupPoint(voxel: SIMD3<Float>(Float($0.x), Float($0.y), Float($0.z)),
                            container: container, aspect: aspect)
            }
            let edgeStart = projected.dropLast().last
            let edgeEnd = projected.last
            if markup.kind == .plane, projected.count == 3 {
                PolygonShape(points: projected)
                    .fill(markup.pendingColor.opacity(0.06))
                    .overlay(PolygonShape(points: projected)
                        .stroke(markup.pendingColor.opacity(0.55),
                                style: StrokeStyle(lineWidth: 1.5, dash: [5, 4])))
                    .allowsHitTesting(false)
            } else if projected.count >= 2 {
                Path { path in
                    path.move(to: projected[projected.count - 2])
                    path.addLine(to: projected[projected.count - 1])
                }
                .stroke(markup.pendingColor.opacity(0.65),
                        style: StrokeStyle(lineWidth: 1.5, dash: [5, 4]))
                .allowsHitTesting(false)
            }
            if let start = edgeStart, let end = edgeEnd {
                Text(markup.liveLineMeasurementText(from: markup.pending.last!, to: cursor))
                    .font(.caption2.monospacedDigit())
                    .foregroundStyle(.white.opacity(0.9))
                    .padding(.horizontal, 5).padding(.vertical, 3)
                    .background(.black.opacity(0.58), in: Capsule())
                    .position(x: (start.x + end.x) * 0.5,
                              y: (start.y + end.y) * 0.5 - 14)
                    .allowsHitTesting(false)
            }
        }
    }

    private func pendingCursorVoxel(container: CGSize) -> SIMD3<Int>? {
        guard let pointer, let (px, py) = pixel(at: pointer, container: container) else {
            return nil
        }
        return model.voxel(onAxis: axis, px: px, py: py)
    }

    @ViewBuilder private func representation(_ m: MarkupModel.Markup,
                                              container: CGSize, aspect: CGFloat) -> some View {
        let points = sliceIntersections(kind: m.kind, voxels: m.voxels)
        let projected = points.compactMap {
            markupPoint(voxel: $0, container: container, aspect: aspect)
        }
        ForEach(Array(m.voxels.enumerated()), id: \.offset) { _, v in
            if markup.onCurrentSlice(v, axis: axis),
               let pt = markupPoint(voxel: SIMD3<Float>(v), container: container, aspect: aspect) {
                Circle().fill(markup.color(m)).frame(width: 9, height: 9)
                    .overlay(Circle().strokeBorder(.white, lineWidth: 1))
                    .position(pt).allowsHitTesting(false)
            }
        }
        ForEach(Array(projected.enumerated()), id: \.offset) { _, pt in
            Circle().fill(markup.color(m)).frame(width: 7, height: 7)
                .overlay(Circle().strokeBorder(.white, lineWidth: 1))
                .position(pt).allowsHitTesting(false)
        }
        if m.kind == .plane, m.voxels.allSatisfy({ markup.onCurrentSlice($0, axis: axis) }),
           projected.count == 3 {
            PolygonShape(points: projected)
                .fill(markup.color(m).opacity(0.18))
                .overlay(PolygonShape(points: projected).stroke(markup.color(m), lineWidth: 2))
                .allowsHitTesting(false)
        } else if projected.count >= 2 {
            Path { path in
                path.move(to: projected[0])
                path.addLine(to: projected[1])
            }
            .stroke(markup.color(m), style: StrokeStyle(lineWidth: 2, dash: [6, 4]))
            .allowsHitTesting(false)
        }
        if let label = markup.measurementText(m), let center = projected.first {
            Text(label).font(.caption2.monospacedDigit()).foregroundStyle(.white)
                .padding(.horizontal, 5).padding(.vertical, 3)
                .background(.black.opacity(0.65), in: Capsule())
                .position(x: center.x + 38, y: center.y - 14)
                .allowsHitTesting(false)
        }
    }

    private func sliceIntersections(kind: MarkupModel.Kind,
                                    voxels: [SIMD3<Int>]) -> [SIMD3<Float>] {
        let pts = voxels.map {
            SIMD3<Float>(Float($0.x), Float($0.y), Float($0.z))
        }
        guard pts.count >= 2 else { return [] }
        let slice = Float(model.sliceIndex[axis])
        func coord(_ p: SIMD3<Float>) -> Float {
            axis == 0 ? p.z : (axis == 1 ? p.y : p.x)
        }
        func edge(_ a: SIMD3<Float>, _ b: SIMD3<Float>) -> SIMD3<Float>? {
            let da = coord(a) - slice, db = coord(b) - slice
            if abs(da) < 0.001 { return a }
            if abs(db) < 0.001 { return b }
            guard da * db < 0 else { return nil }
            let t = da / (da - db)
            return a + (b - a) * t
        }
        let pairs: [(Int, Int)] = kind == .line ? [(0, 1)] : [(0, 1), (1, 2), (2, 0)]
        var result: [SIMD3<Float>] = []
        for (a, b) in pairs where a < pts.count && b < pts.count {
            if let hit = edge(pts[a], pts[b]), !result.contains(where: { simd_length($0 - hit) < 0.001 }) {
                result.append(hit)
            }
        }
        return result
    }

    private func markupPoint(voxel v: SIMD3<Float>, container: CGSize,
                             aspect: CGFloat) -> CGPoint? {
        guard let img = model.images[axis] else { return nil }
        let px: CGFloat, py: CGFloat
        switch axis {
        case 0: px = CGFloat(v.x); py = CGFloat(v.y)
        case 1: px = CGFloat(v.x); py = CGFloat(model.depth - 1) - CGFloat(v.z)
        default: px = CGFloat(v.y); py = CGFloat(model.depth - 1) - CGFloat(v.z)
        }
        return SliceCoordinates.point(forPixel: px, py, container: container,
                                      imageWidth: img.width, imageHeight: img.height,
                                      aspect: aspect, zoom: zoom, anchor: zoomAnchor,
                                      pan: pan)
    }

    fileprivate func placeMarkup(at point: CGPoint, container: CGSize) {
        guard let (px, py) = pixel(at: point, container: container),
              let voxel = model.voxel(onAxis: axis, px: px, py: py) else { return }
        markup.place(voxel)
    }

    private func crosshairPoint(container: CGSize, aspect: CGFloat) -> CGPoint? {
        guard let img = model.images[axis],
              let c = model.crosshairPixel(onAxis: axis) else { return nil }
        return SliceCoordinates.point(forPixel: c.px, c.py, container: container,
                                      imageWidth: img.width, imageHeight: img.height,
                                      aspect: aspect, zoom: zoom, anchor: zoomAnchor,
                                      pan: pan)
    }

    // Right-drag vertical delta (window coords, up positive) -> magnification. The
    // exponential makes each point of drag a constant percentage change, so zoom
    // feels even at every level; clamped to [1, maxZoom] (1 = fit, no zoom-out
    // below fit since there is no pan).
    private func applyZoom(_ dy: CGFloat) {
        let factor = CGFloat(exp(Double(dy) * 0.01))
        zoom = min(Self.maxZoom, max(1, zoom * factor))
        if zoom == 1 { pan = .zero } // back to fit: recenter, no stale offset
    }

    private func applyMagnification(_ delta: CGFloat) {
        zoomAnchor = pointer ?? .zero
        zoom = min(Self.maxZoom, max(1, zoom * CGFloat(exp(Double(delta)))))
        if zoom == 1 { pan = .zero }
    }

    private func zoomBy(_ factor: CGFloat, container: CGSize) {
        zoomAnchor = pointer ?? CGPoint(x: container.width / 2, y: container.height / 2)
        zoom = min(Self.maxZoom, max(1, zoom * factor))
        if zoom == 1 { pan = .zero }
    }

    private func resetZoom() {
        zoom = 1
        pan = .zero
    }

    // MARK: - Interaction helpers (called from SliceInteraction)

    fileprivate func pixel(at point: CGPoint, container: CGSize) -> (x: Int, y: Int)? {
        guard let img = model.images[axis] else { return nil }
        return SliceCoordinates.pixel(
            forTap: point, container: container,
            imageWidth: img.width, imageHeight: img.height,
            aspect: model.physicalAspect(axis), zoom: zoom, anchor: zoomAnchor,
            pan: pan)
    }

    fileprivate func locate(at point: CGPoint, container: CGSize) {
        guard let (px, py) = pixel(at: point, container: container),
              let voxel = model.voxel(onAxis: axis, px: px, py: py) else { return }
        model.jump(to: voxel)
    }

    fileprivate func brushChanged(_ seg: SegmentationModel, at point: CGPoint,
                                  container: CGSize) {
        pointer = point
        guard seg.tool.isBrush else { return }
        if !strokeActive { seg.beginStroke(); strokeActive = true; lastPaintPixel = nil }
        paintMove(seg, to: point, container: container)
    }

    fileprivate func brushEnded(_ seg: SegmentationModel, at point: CGPoint,
                                container: CGSize) {
        if seg.tool.isBrush {
            if !strokeActive { seg.beginStroke(); strokeActive = true; lastPaintPixel = nil }
            paintMove(seg, to: point, container: container)
            seg.endStroke()
            strokeActive = false
            lastPaintPixel = nil
        } else if seg.tool == .regionGrow {
            if let (px, py) = pixel(at: point, container: container) {
                seg.seedRegionGrow(axis: axis, px: px, py: py)
            }
        } else if seg.tool == .levelTrace {
            if let (px, py) = pixel(at: point, container: container) {
                seg.seedLevelTrace(axis: axis, px: px, py: py)
            }
        }
    }

    private func paintMove(_ seg: SegmentationModel, to point: CGPoint,
                           container: CGSize) {
        guard let (px, py) = pixel(at: point, container: container) else { return }
        seg.paintStroke(axis: axis, from: lastPaintPixel, to: (px, py))
        lastPaintPixel = (px, py)
    }
}

private struct PolygonShape: Shape {
    let points: [CGPoint]

    func path(in rect: CGRect) -> Path {
        var path = Path()
        guard let first = points.first else { return path }
        path.move(to: first)
        for point in points.dropFirst() { path.addLine(to: point) }
        path.closeSubpath()
        return path
    }
}

// The mask overlay, in its own observed view so a paint stroke (which republishes
// only OverlayStore) re-renders just this layer and not the whole control panel.
private struct MaskOverlay: View {
    @ObservedObject var store: OverlayStore
    let axis: Int
    let rect: CGRect

    var body: some View {
        if let ov = store.images[axis] {
            Image(decorative: ov, scale: 1)
                .resizable()
                .interpolation(.none)
                .frame(width: rect.width, height: rect.height)
                .position(x: rect.midX, y: rect.midY)
                .allowsHitTesting(false)
        }
    }
}

// Picks the canvas interaction. Visualize: click = locate (linked navigation),
// left-drag = window/level. Segment: the active tool owns the canvas — paint/erase
// drags the brush, region-grow seeds on a click, threshold has no canvas action.
private struct SliceInteraction: ViewModifier {
    let pane: SlicePane
    let container: CGSize

    func body(content: Content) -> some View {
        if pane.markup.placing {
            // Markup placement takes the canvas: each click drops one point.
            content.gesture(
                SpatialTapGesture(coordinateSpace: .local)
                    .onEnded { pane.placeMarkup(at: $0.location, container: container) }
            )
        } else if let seg = pane.segment {
            content.gesture(
                DragGesture(minimumDistance: 0, coordinateSpace: .local)
                    .onChanged { pane.brushChanged(seg, at: $0.location, container: container) }
                    .onEnded { pane.brushEnded(seg, at: $0.location, container: container) }
            )
        } else {
            content
                .windowLevelDrag(pane.model)
                .simultaneousGesture(
                    SpatialTapGesture(coordinateSpace: .local)
                        .onEnded { pane.locate(at: $0.location, container: container) }
                )
        }
    }
}

// The "how do I move around this view" cheat-sheet shown from the info (i) button on
// each pane — surfaced because panning a zoomed slice (⌥ + drag) isn't discoverable
// on a trackpad, where there's no middle mouse button.
private struct SliceHelpPopover: View {
    private struct Row: Identifiable {
        let id = UUID(); let keys: String; let action: String
    }
    private let rows = [
        Row(keys: "Scroll", action: "Change slice"),
        Row(keys: "Pinch / right-drag / ± buttons", action: "Zoom in and out"),
        Row(keys: "⌥ Option + drag", action: "Pan (when zoomed in)"),
        Row(keys: "Click", action: "Recenter all three views here"),
        Row(keys: "Shift + move", action: "Crosshair cross-reference"),
    ]

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Navigation")
                .font(.headline)
            ForEach(rows) { row in
                HStack(alignment: .firstTextBaseline, spacing: 10) {
                    Text(row.keys)
                        .font(.caption.weight(.semibold))
                        .frame(width: 190, alignment: .leading)
                    Text(row.action)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }
        }
        .padding(14)
        .frame(width: 340)
    }
}
