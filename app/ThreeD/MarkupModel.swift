import SwiftUI
import Combine
import SceneKit

// Fiducial markups the user drops on the 2D slice panes and sees in the 3D view.
// Three kinds, by how many defining points they take:
//   point (1) -> a sphere      line (2) -> a segment      plane (3) -> a triangle
// Points are stored in VOXEL coordinates (the single source of truth); the 3D view
// converts them to millimetres (voxel × spacing, same convention as the mesh) and
// the slice panes project them back to pixels. Placement is explicit: it only
// happens while `placing` is on, one point per click, and a markup finalizes once
// it has all the points its kind needs.
@MainActor
final class MarkupModel: ObservableObject {
    enum Kind: String, CaseIterable, Identifiable {
        case point, line, plane
        var id: String { rawValue }
        var pointsNeeded: Int {
            switch self {
            case .point: return 1
            case .line: return 2
            case .plane: return 3
            }
        }
        var title: String {
            switch self {
            case .point: return "Point"
            case .line: return "Line"
            case .plane: return "Plane"
            }
        }
        var icon: String {
            switch self {
            case .point: return "mappin"
            case .line: return "line.diagonal"
            case .plane: return "triangle"
            }
        }
    }

    struct Markup: Identifiable, Equatable {
        let id = UUID()
        var kind: Kind
        var voxels: [SIMD3<Int>] // defining points, voxel coordinates
        var colorIndex: Int
        var name: String
        var visible: Bool = true
    }

    private let volume: VolumeModel
    private var cancellables = Set<AnyCancellable>()

    @Published var placing = false
    @Published var kind: Kind = .point
    // The palette index the NEXT placed markup will use. Read-only from outside; the
    // UI changes it via pickNextColor(_:) so we can tell a deliberate pick apart from
    // the automatic advance. The placement dots show this colour live (pendingColor).
    @Published private(set) var nextColorIndex = 0
    @Published private(set) var markups: [Markup] = []
    @Published private(set) var pending: [SIMD3<Int>] = [] // in-progress points
    private var counter = 1
    // Cycle to a fresh colour after each placement (distinct colours by default). A
    // deliberate colour pick turns this off so the chosen colour STICKS for a run of
    // markups until the user picks again.
    private var autoAdvanceColor = true

    // Distinct, bright marker colours (independent of the segment palette).
    static let palette: [Color] = [
        .yellow, .cyan, .green, .orange, .pink, .purple, .mint, .red,
    ]

    // Palette is indexed with a EUCLIDEAN modulo so a stray negative index can never
    // trap on palette[-1] (Swift's % keeps the sign).
    static func paletteColor(_ index: Int) -> Color { palette[wrap(index)] }
    private static func wrap(_ i: Int) -> Int {
        let n = palette.count
        return ((i % n) + n) % n
    }

    init(volume: VolumeModel) {
        self.volume = volume
        // A fresh volume invalidates all markups (their voxel coords no longer map)
        // and restarts the colour cycle + numbering.
        volume.$hasVolume
            .sink { [weak self] _ in
                self?.markups = []
                self?.pending = []
                self?.placing = false
                self?.nextColorIndex = 0
                self?.counter = 1
                self?.autoAdvanceColor = true
            }
            .store(in: &cancellables)
    }

    // MARK: - Placement

    func place(_ voxel: SIMD3<Int>) {
        guard placing else { return }
        pending.append(voxel)
        if pending.count >= kind.pointsNeeded {
            markups.append(Markup(kind: kind, voxels: pending, colorIndex: nextColorIndex,
                                  name: "\(kind.title) \(counter)"))
            counter += 1
            if autoAdvanceColor {
                nextColorIndex = (nextColorIndex + 1) % Self.palette.count
            }
            pending = []
        }
    }

    // Deliberately choose the colour for upcoming markups; it sticks until re-picked.
    func pickNextColor(_ index: Int) {
        nextColorIndex = Self.wrap(index)
        autoAdvanceColor = false
    }

    // The colour the in-progress (pending) markup will take, so the placement dots
    // can be drawn filled in that colour instead of a neutral outline.
    var pendingColor: Color { Self.paletteColor(nextColorIndex) }

    // Discard a half-placed line/plane.
    func cancelPending() { pending = [] }

    // One step of undo for the Markups tab (Cmd-Z): drop the last in-progress point
    // if a multi-point markup is being placed, otherwise remove the last committed
    // markup. `canRemoveLast` gates the menu item.
    var canRemoveLast: Bool { !pending.isEmpty || !markups.isEmpty }
    func removeLast() {
        if !pending.isEmpty { pending.removeLast() }
        else if !markups.isEmpty { markups.removeLast() }
    }

    func remove(_ id: UUID) { markups.removeAll { $0.id == id } }

    func removeAll() { markups = []; pending = [] }

    func rename(_ id: UUID, to name: String) {
        guard let i = markups.firstIndex(where: { $0.id == id }) else { return }
        markups[i].name = name.isEmpty ? "\(markups[i].kind.title)" : name
    }

    // Recolour an existing markup (palette index), or toggle whether it is drawn.
    func setColorIndex(_ id: UUID, _ index: Int) {
        guard let i = markups.firstIndex(where: { $0.id == id }) else { return }
        markups[i].colorIndex = Self.wrap(index)
    }
    func setVisible(_ id: UUID, _ visible: Bool) {
        guard let i = markups.firstIndex(where: { $0.id == id }) else { return }
        markups[i].visible = visible
    }

    func color(_ m: Markup) -> Color { Self.paletteColor(m.colorIndex) }

    // MARK: - Geometry helpers

    // Voxel -> millimetre world position (matches marching-cubes vertices).
    func mm(_ v: SIMD3<Int>) -> SCNVector3 {
        let s = volume.spacing
        return SCNVector3(Float(v.x) * s.x, Float(v.y) * s.y, Float(v.z) * s.z)
    }

    // A marker radius proportional to the physical volume size, so spheres read at
    // any scan without being passed the camera.
    var markerRadius: Float {
        let s = volume.spacing
        let ext = max(Float(volume.width) * s.x,
                      max(Float(volume.height) * s.y, Float(volume.depth) * s.z))
        return max(ext * 0.012, 1)
    }

    // The renderable form the 3D view consumes: points already in mm + an NSColor.
    // Hidden markups are omitted so the visibility toggle applies in 3D too.
    func renders() -> [MarkupRender] {
        markups.filter { $0.visible }.map { m in
            MarkupRender(id: m.id, kind: m.kind, points: m.voxels.map { mm($0) },
                         color: NSColor(color(m)), colorIndex: m.colorIndex)
        }
    }

    func pendingMM() -> [SCNVector3] { pending.map { mm($0) } }

    // NSColor form of `pendingColor`, for the 3D view's in-progress spheres.
    func pendingColorNS() -> NSColor { NSColor(pendingColor) }

    // Whether a voxel lies on the currently displayed slice of `axis` (so the slice
    // overlay only dots points that are actually on screen).
    func onCurrentSlice(_ v: SIMD3<Int>, axis: Int) -> Bool {
        let idx = axis == 0 ? v.z : (axis == 1 ? v.y : v.x)
        return idx == volume.sliceIndex[axis]
    }
}

// A markup flattened for SceneKit: mm points + colour. Not Equatable on purpose:
// the 3D view diffs markups by a cheap signature, not by comparing SCNVector3s.
// `colorIndex` is the stable palette slot, used as the diff key (cheaper + more
// reliable than stringifying the NSColor).
struct MarkupRender {
    let id: UUID
    let kind: MarkupModel.Kind
    let points: [SCNVector3]
    let color: NSColor
    let colorIndex: Int
}
