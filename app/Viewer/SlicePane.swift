import SwiftUI

// One orthographic plane: the window/levelled HU image, an optional colored mask
// overlay, the shared crosshair, a slice scrubber, and the interaction layer.
// Navigation (3D-Slicer-style): click to locate (recenters all panes), mouse
// wheel scrolls slices, left-drag adjusts window/level. On the Segment tab the
// active tool owns the canvas instead: paint/erase brush along a drag (with a
// cursor ring), or region-grow seed on a click. A header button maximizes this
// pane to fill the viewport.
struct SlicePane: View {
    @EnvironmentObject var model: VolumeModel
    let axis: Int
    var segment: SegmentationModel? = nil
    var markups: MarkupsModel? = nil
    var isFocused: Bool = false
    var onToggleFocus: (() -> Void)? = nil

    // Live cursor position over this pane (for the brush ring), and paint-stroke
    // bookkeeping so a drag is one continuous, gap-free, single-undo stroke.
    @State private var pointer: CGPoint?
    @State private var lastPaintPixel: (px: Int, py: Int)?
    @State private var strokeActive = false

    // Per-pane zoom: 1 = fit, up to 8x, driven by a right-button drag. `zoomAnchor`
    // is the cursor point where the drag began, so the view magnifies toward what
    // is under the cursor. `pan` slides the zoomed image (middle-button drag) and
    // resets to zero when the pane returns to fit. Each pane transforms
    // independently (3D-Slicer behavior).
    @State private var zoom: CGFloat = 1
    @State private var zoomAnchor: CGPoint = .zero
    @State private var pan: CGSize = .zero
    private static let maxZoom: CGFloat = 8

    // Scissors rubber-band: the drag's start + current point in display coords.
    @State private var scissorsFrom: CGPoint?
    @State private var scissorsTo: CGPoint?

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

            Slider(
                value: Binding(
                    get: { Double(model.sliceIndex[axis]) },
                    set: { model.setSlice(axis, Int($0.rounded())) }),
                in: 0...Double(max(count - 1, 1))
            )
            .controlSize(.small)
            .disabled(count <= 1)
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
                scissorsRect
                if let markups {
                    MarkupOverlay(
                        markups: markups,
                        project: { projectVoxel($0, container: container, aspect: aspect) },
                        onSlice: { model.voxelOnSlice(axis, $0) })
                }
            }
            .clipShape(RoundedRectangle(cornerRadius: 10))
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .contentShape(Rectangle())
            .modifier(SliceInteraction(pane: self, container: container))
            .overlay(CanvasInputCatcher(
                onStep: { model.setSlice(axis, model.sliceIndex[axis] + $0) },
                onMove: { pointer = $0 },
                onZoomBegin: { zoomAnchor = $0 },
                onZoom: { applyZoom($0) },
                onShiftLocate: { locate(at: $0, container: container) },
                onShiftChange: { model.setShiftActive($0) },
                onPan: { d in
                    // Clamp so the zoomed image can't be dragged entirely out of the
                    // pane: the offset is bounded to half the zoomed image size, so
                    // at least its centre stays reachable.
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
        }
    }

    // The brush footprint: a ring sized to brushRadius (slice pixels) scaled into
    // display points, tinted by the active segment (red while erasing).
    // The scissors rubber-band rectangle, drawn while dragging; colour signals
    // which side gets erased (red = inside, orange = outside).
    @ViewBuilder private var scissorsRect: some View {
        if let seg = segment, seg.tool == .scissors,
           let a = scissorsFrom, let b = scissorsTo {
            let rect = CGRect(x: min(a.x, b.x), y: min(a.y, b.y),
                              width: abs(a.x - b.x), height: abs(a.y - b.y))
            Rectangle()
                .stroke(seg.scissorsEraseInside ? Color.red : Color.orange,
                        style: StrokeStyle(lineWidth: 1.5, dash: [5]))
                .frame(width: rect.width, height: rect.height)
                .position(x: rect.midX, y: rect.midY)
                .allowsHitTesting(false)
        }
    }

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

    private func crosshairPoint(container: CGSize, aspect: CGFloat) -> CGPoint? {
        guard let img = model.images[axis],
              let c = model.crosshairPixel(onAxis: axis) else { return nil }
        return SliceCoordinates.point(forPixel: c.px, c.py, container: container,
                                      imageWidth: img.width, imageHeight: img.height,
                                      aspect: aspect, zoom: zoom, anchor: zoomAnchor,
                                      pan: pan)
    }

    // Project a voxel to a point on this pane (for markup glyphs), honouring the
    // pane's current zoom / anchor / pan through the shared coordinate mapping.
    private func projectVoxel(_ voxel: SIMD3<Int>, container: CGSize,
                              aspect: CGFloat) -> CGPoint? {
        guard let img = model.images[axis],
              let p = model.slicePixel(onAxis: axis, voxel: voxel) else { return nil }
        return SliceCoordinates.point(forPixel: p.px, p.py, container: container,
                                      imageWidth: img.width, imageHeight: img.height,
                                      aspect: aspect, zoom: zoom, anchor: zoomAnchor,
                                      pan: pan)
    }

    // Right-drag vertical delta (window coords, up positive) -> magnification. The
    // exponential makes each point of drag a constant percentage change, so zoom
    // feels even at every level; clamped to [1, maxZoom]. Returning to fit clears
    // any pan so the image re-centers.
    private func applyZoom(_ dy: CGFloat) {
        let factor = CGFloat(exp(Double(dy) * 0.01))
        zoom = min(Self.maxZoom, max(1, zoom * factor))
        if zoom == 1 { pan = .zero }
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

    fileprivate func placeMarkup(_ markups: MarkupsModel, at point: CGPoint,
                                 container: CGSize) {
        guard let (px, py) = pixel(at: point, container: container),
              let voxel = model.voxel(onAxis: axis, px: px, py: py) else { return }
        markups.addPoint(voxel)
    }

    fileprivate func brushChanged(_ seg: SegmentationModel, at point: CGPoint,
                                  container: CGSize) {
        pointer = point
        if seg.tool == .scissors {
            if scissorsFrom == nil { scissorsFrom = point }
            scissorsTo = point
            return
        }
        guard seg.tool.isBrush else { return }
        if !strokeActive { seg.beginStroke(); strokeActive = true; lastPaintPixel = nil }
        paintMove(seg, to: point, container: container)
    }

    fileprivate func brushEnded(_ seg: SegmentationModel, at point: CGPoint,
                                container: CGSize) {
        if seg.tool == .scissors {
            applyScissors(seg, at: point, container: container)
            return
        }
        if seg.tool.isBrush {
            if !strokeActive { seg.beginStroke(); strokeActive = true; lastPaintPixel = nil }
            paintMove(seg, to: point, container: container)
            seg.endStroke()
            strokeActive = false
            lastPaintPixel = nil
        } else if seg.tool.isClickSeed {
            if let (px, py) = pixel(at: point, container: container) {
                if seg.tool == .levelTrace {
                    seg.seedLevelTrace(axis: axis, px: px, py: py)
                } else {
                    seg.seedRegionGrow(axis: axis, px: px, py: py)
                }
            }
        }
    }

    private func applyScissors(_ seg: SegmentationModel, at point: CGPoint,
                               container: CGSize) {
        defer { scissorsFrom = nil; scissorsTo = nil }
        guard let start = scissorsFrom else { return }
        // A stray click (no real drag) would make a zero-area rect - which with
        // "Outside" selected erases the entire slice. Require a real drag, and a
        // non-degenerate pixel rectangle, before cutting.
        guard hypot(point.x - start.x, point.y - start.y) >= 6 else { return }
        guard let a = pixel(at: start, container: container),
              let b = pixel(at: point, container: container),
              a.x != b.x, a.y != b.y else { return }
        seg.scissorsCut(axis: axis, x0: a.x, y0: a.y, x1: b.x, y1: b.y)
    }

    private func paintMove(_ seg: SegmentationModel, to point: CGPoint,
                           container: CGSize) {
        guard let (px, py) = pixel(at: point, container: container) else { return }
        seg.paintStroke(axis: axis, from: lastPaintPixel, to: (px, py))
        lastPaintPixel = (px, py)
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
        if let markups = pane.markups {
            // Markups tab: click drops a point on the active markup; W/L drag stays.
            content
                .windowLevelDrag(pane.model)
                .simultaneousGesture(
                    SpatialTapGesture(coordinateSpace: .local)
                        .onEnded { pane.placeMarkup(markups, at: $0.location,
                                                    container: container) }
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
