import SwiftUI

// One orthographic plane: the window/levelled HU image, an optional colored mask
// overlay, a slice scrubber, and the interaction layer. On the Visualize tab the
// left-drag adjusts window/level; on the Segment tab (when `segment` is non-nil)
// the active tool owns the canvas instead — region-grow seeds on a click.
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
            ZStack {
                RoundedRectangle(cornerRadius: 10)
                    .fill(.black)
                if let img = model.images[axis] {
                    Image(decorative: img, scale: 1)
                        .resizable()
                        .interpolation(.none)
                        .aspectRatio(model.physicalAspect(axis), contentMode: .fit)
                        .padding(8)
                }
                // Mask overlay (Segment tab): premultiplied RGBA, registers
                // pixel-exact because it uses the same aspect + padding as the HU
                // image. Non-interactive so it never steals the seed gesture.
                if let seg = segment, seg.showOverlay, let ov = seg.overlays[axis] {
                    Image(decorative: ov, scale: 1)
                        .resizable()
                        .interpolation(.none)
                        .aspectRatio(model.physicalAspect(axis), contentMode: .fit)
                        .padding(8)
                        .allowsHitTesting(false)
                }
            }
            .clipShape(RoundedRectangle(cornerRadius: 10))
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .contentShape(Rectangle())
            .modifier(InteractionModifier(model: model, axis: axis,
                                          segment: segment, container: geo.size))
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
}

// Picks the canvas interaction: window/level drag on Visualize, or the active
// segmentation tool on Segment. Kept as a modifier so the two gesture kinds don't
// fight (only one is installed at a time).
private struct InteractionModifier: ViewModifier {
    let model: VolumeModel
    let axis: Int
    let segment: SegmentationModel?
    let container: CGSize

    func body(content: Content) -> some View {
        if let seg = segment {
            content.gesture(
                // minimumDistance 0 so a plain click registers as a seed.
                DragGesture(minimumDistance: 0)
                    .onEnded { value in
                        handleSegment(seg, at: value.location)
                    }
            )
        } else {
            content.windowLevelDrag(model)
        }
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
