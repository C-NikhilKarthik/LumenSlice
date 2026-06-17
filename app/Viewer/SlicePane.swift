import SwiftUI

// One orthographic plane: the window/levelled HU image, an optional colored mask
// overlay, the shared crosshair, a slice scrubber, and the interaction layer.
// Navigation (3D-Slicer-style): click to locate (recenters all panes), mouse
// wheel scrolls slices, left-drag adjusts window/level. On the Segment tab the
// active tool owns the canvas instead: paint/erase brush along a drag (with a
// cursor ring), or region-grow seed on a click.
struct SlicePane: View {
    @EnvironmentObject var model: VolumeModel
    let axis: Int
    var segment: SegmentationModel? = nil

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
            ZStack {
                RoundedRectangle(cornerRadius: 10)
                    .fill(.black)
                if let img = model.images[axis] {
                    Image(decorative: img, scale: 1)
                        .resizable()
                        .interpolation(.none)
                        .aspectRatio(aspect, contentMode: .fit)
                        .padding(8)
                }
                if let seg = segment, seg.showOverlay, let ov = seg.overlays[axis] {
                    Image(decorative: ov, scale: 1)
                        .resizable()
                        .interpolation(.none)
                        .aspectRatio(aspect, contentMode: .fit)
                        .padding(8)
                        .allowsHitTesting(false)
                }
                let colors = PlaneColors.forPane(axis)
                let fitted = SliceCoordinates.fittedRect(container: container, aspect: aspect)
                CrosshairOverlay(
                    point: crosshairPoint(container: container, aspect: aspect),
                    rect: fitted,
                    verticalColor: colors.vertical,
                    horizontalColor: colors.horizontal)
                OrientationLabels(axis: axis, rect: fitted)
            }
            .clipShape(RoundedRectangle(cornerRadius: 10))
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .contentShape(Rectangle())
            .modifier(InteractionModifier(model: model, axis: axis,
                                          segment: segment, container: container))
            .overlay(ScrollCatcher { step in
                model.setSlice(axis, model.sliceIndex[axis] + step)
            })
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

    private func crosshairPoint(container: CGSize, aspect: CGFloat) -> CGPoint? {
        guard let img = model.images[axis],
              let c = model.crosshairPixel(onAxis: axis) else { return nil }
        return SliceCoordinates.point(forPixel: c.px, c.py, container: container,
                                      imageWidth: img.width, imageHeight: img.height,
                                      aspect: aspect)
    }
}

// Picks the canvas interaction. Visualize: click = locate (linked navigation),
// left-drag = window/level. Segment: the active tool owns the canvas — paint/erase
// drags the brush (with a cursor ring), region-grow seeds on a click, threshold has
// no canvas action. Mouse-wheel slice scroll is handled separately by ScrollCatcher.
private struct InteractionModifier: ViewModifier {
    let model: VolumeModel
    let axis: Int
    let segment: SegmentationModel?
    let container: CGSize

    @State private var brushLocation: CGPoint?
    @State private var strokeActive = false

    func body(content: Content) -> some View {
        if let seg = segment {
            content
                .gesture(segmentDrag(seg))
                .onContinuousHover { phase in
                    guard seg.tool.isBrush else { brushLocation = nil; return }
                    switch phase {
                    case .active(let p): brushLocation = p
                    case .ended: brushLocation = nil
                    }
                }
                .overlay { brushRing(seg) }
        } else {
            content
                .windowLevelDrag(model)
                .simultaneousGesture(
                    SpatialTapGesture(coordinateSpace: .local)
                        .onEnded { value in handleLocate(at: value.location) }
                )
        }
    }

    private func segmentDrag(_ seg: SegmentationModel) -> some Gesture {
        DragGesture(minimumDistance: 0, coordinateSpace: .local)
            .onChanged { value in
                brushLocation = value.location
                guard seg.tool.isBrush else { return }
                if !strokeActive { seg.beginStroke(); strokeActive = true }
                paint(seg, at: value.location)
            }
            .onEnded { value in
                if seg.tool.isBrush {
                    if !strokeActive { seg.beginStroke() }
                    paint(seg, at: value.location)
                    seg.endStroke()
                    strokeActive = false
                } else if seg.tool == .regionGrow {
                    seed(seg, at: value.location)
                }
            }
    }

    // The brush footprint: a ring sized to brushRadius (slice pixels) scaled into
    // display points, tinted by the active segment (red while erasing).
    @ViewBuilder private func brushRing(_ seg: SegmentationModel) -> some View {
        if seg.tool.isBrush, let loc = brushLocation, let img = model.images[axis],
           let rect = SliceCoordinates.fittedRect(
            container: container, aspect: model.physicalAspect(axis)),
           rect.contains(loc) {
            let scale = rect.width / CGFloat(img.width)
            let diameter = CGFloat(seg.brushRadius) * scale * 2
            Circle()
                .stroke(seg.tool == .erase ? Color.red : seg.activeColor,
                        lineWidth: 1.5)
                .frame(width: diameter, height: diameter)
                .position(loc)
                .allowsHitTesting(false)
        }
    }

    private func pixel(at point: CGPoint) -> (x: Int, y: Int)? {
        guard let img = model.images[axis] else { return nil }
        return SliceCoordinates.pixel(
            forTap: point, container: container,
            imageWidth: img.width, imageHeight: img.height,
            aspect: model.physicalAspect(axis))
    }

    private func handleLocate(at point: CGPoint) {
        guard let (px, py) = pixel(at: point),
              let voxel = model.voxel(onAxis: axis, px: px, py: py) else { return }
        model.jump(to: voxel)
    }

    private func paint(_ seg: SegmentationModel, at point: CGPoint) {
        guard let (px, py) = pixel(at: point) else { return }
        seg.paintAt(axis: axis, px: px, py: py)
    }

    private func seed(_ seg: SegmentationModel, at point: CGPoint) {
        guard let (px, py) = pixel(at: point) else { return }
        seg.seedRegionGrow(axis: axis, px: px, py: py)
    }
}
