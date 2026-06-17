import SwiftUI

// One orthographic plane: the window/levelled HU image, an optional colored mask
// overlay, the shared crosshair, a slice scrubber, and the interaction layer.
// Navigation (3D-Slicer-style): click to locate (recenters all panes), mouse
// wheel scrolls slices, left-drag adjusts window/level. On the Segment tab the
// active tool owns the click instead (region-grow seeds).
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
// left-drag = window/level. Segment: the active tool owns the click (region-grow
// seeds). Mouse-wheel slice scroll is handled separately by ScrollCatcher.
private struct InteractionModifier: ViewModifier {
    let model: VolumeModel
    let axis: Int
    let segment: SegmentationModel?
    let container: CGSize

    func body(content: Content) -> some View {
        if let seg = segment {
            content.gesture(
                DragGesture(minimumDistance: 0)
                    .onEnded { value in handleSegment(seg, at: value.location) }
            )
        } else {
            content
                .windowLevelDrag(model)
                .simultaneousGesture(
                    SpatialTapGesture(coordinateSpace: .local)
                        .onEnded { value in handleLocate(at: value.location) }
                )
        }
    }

    private func handleLocate(at point: CGPoint) {
        guard let img = model.images[axis],
              let (px, py) = SliceCoordinates.pixel(
                forTap: point, container: container,
                imageWidth: img.width, imageHeight: img.height,
                aspect: model.physicalAspect(axis)),
              let voxel = model.voxel(onAxis: axis, px: px, py: py) else { return }
        model.jump(to: voxel)
    }

    private func handleSegment(_ seg: SegmentationModel, at point: CGPoint) {
        guard seg.tool == .regionGrow else { return }
        guard let img = model.images[axis] else { return }
        guard let (px, py) = SliceCoordinates.pixel(
            forTap: point, container: container,
            imageWidth: img.width, imageHeight: img.height,
            aspect: model.physicalAspect(axis)) else { return }
        seg.seedRegionGrow(axis: axis, px: px, py: py)
    }
}
