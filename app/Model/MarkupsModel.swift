import SwiftUI
import Combine
import LumenCore

// A markup is a small annotation placed in the volume: a list of points in voxel
// space plus a type that decides how they connect (a line, an angle, a curve, ...).
// Markups are pure UI-side geometry - they never touch the mask - so this model
// lives entirely in Swift and only reads the volume for its voxel<->pixel mapping
// and physical spacing (for lengths and angles).

enum MarkupType: String, CaseIterable, Identifiable {
    case point        // "Point List": any number of independent points
    case line         // exactly 2 points; shows length
    case angle        // 3 points; shows the angle at the middle vertex
    case curve        // open polyline through N points
    case closedCurve  // closed polyline (last connects back to first)
    case plane        // 3 points defining a plane; drawn as their triangle

    var id: String { rawValue }

    var title: String {
        switch self {
        case .point: return "Point List"
        case .line: return "Line"
        case .angle: return "Angle"
        case .curve: return "Curve"
        case .closedCurve: return "Closed Curve"
        case .plane: return "Plane"
        }
    }

    var icon: String {
        switch self {
        case .point: return "smallcircle.filled.circle"
        case .line: return "line.diagonal"
        case .angle: return "angle"
        case .curve: return "scribble"
        case .closedCurve: return "circle.dashed"
        case .plane: return "square.on.square.dashed"
        }
    }

    // Fixed-size markups cap their point count; nil means unlimited.
    var maxPoints: Int? {
        switch self {
        case .line: return 2
        case .angle, .plane: return 3
        case .point, .curve, .closedCurve: return nil
        }
    }

    // Curves that draw a segment from the last point back to the first.
    var isClosed: Bool { self == .closedCurve }
    // Types drawn as a connected path (vs. loose dots).
    var isConnected: Bool { self != .point }
}

struct Markup: Identifiable, Equatable {
    let id: UUID
    var name: String
    var type: MarkupType
    var color: Color
    var visible: Bool
    var points: [SIMD3<Int>]

    var isFull: Bool {
        if let cap = type.maxPoints { return points.count >= cap }
        return false
    }
}

@MainActor
final class MarkupsModel: ObservableObject {
    private let volume: VolumeModel

    @Published private(set) var markups: [Markup] = []
    @Published var activeID: UUID?
    @Published var newType: MarkupType = .point

    // Set by the shell when the Markups tab is shown, so the panes only wire the
    // placement gesture + overlay while the user is actually in this tab.
    @Published var isActive = false

    private var nextNumber = 1
    private var nextColorIndex = 0
    private var cancellables = Set<AnyCancellable>()

    // Reuse the segmentation palette so markups and segments share a look.
    private static let palette = SegmentationModel.paletteColors

    init(volume: VolumeModel) {
        self.volume = volume

        // Markup points are voxel coordinates tied to the loaded volume, so a new
        // dataset must clear them - otherwise they render against unrelated anatomy
        // in the new volume's space. Mirrors SegmentationModel's reset-on-load.
        volume.$hasVolume
            .sink { [weak self] _ in
                guard let self else { return }
                self.markups = []
                self.activeID = nil
                self.nextNumber = 1
                self.nextColorIndex = 0
            }
            .store(in: &cancellables)
    }

    var active: Markup? { markups.first { $0.id == activeID } }

    // MARK: - List management

    func addMarkup() {
        let color = Self.palette[nextColorIndex % Self.palette.count]
        nextColorIndex += 1
        let markup = Markup(id: UUID(),
                            name: "\(newType.title) \(nextNumber)",
                            type: newType,
                            color: color,
                            visible: true,
                            points: [])
        nextNumber += 1
        markups.append(markup)
        activeID = markup.id
    }

    func removeMarkup(_ id: UUID) {
        markups.removeAll { $0.id == id }
        if activeID == id { activeID = markups.last?.id }
    }

    func removeAll() {
        markups.removeAll()
        activeID = nil
    }

    func setActive(_ id: UUID) { activeID = id }

    func setVisible(_ id: UUID, _ visible: Bool) {
        guard let i = index(of: id) else { return }
        markups[i].visible = visible
    }

    func setColor(_ id: UUID, _ color: Color) {
        guard let i = index(of: id) else { return }
        markups[i].color = color
    }

    func rename(_ id: UUID, to name: String) {
        guard let i = index(of: id) else { return }
        markups[i].name = name.isEmpty ? markups[i].type.title : name
    }

    // MARK: - Point placement

    // Add a voxel to the active markup (creating one of the current type if none is
    // active). Ignores the click once a fixed-size markup is full.
    func addPoint(_ voxel: SIMD3<Int>) {
        if activeID == nil { addMarkup() }
        guard let i = markups.firstIndex(where: { $0.id == activeID }) else { return }
        if markups[i].isFull { return }
        markups[i].points.append(voxel)
    }

    func removeLastPoint() {
        guard let i = index(of: activeID), !markups[i].points.isEmpty else { return }
        markups[i].points.removeLast()
    }

    func clearActivePoints() {
        guard let i = index(of: activeID) else { return }
        markups[i].points.removeAll()
    }

    // MARK: - Measurements (physical, using voxel spacing)

    // Total path length of a line/curve in millimetres, or nil if under 2 points.
    func length(of markup: Markup) -> Double? {
        guard markup.points.count >= 2 else { return nil }
        var total = 0.0
        for k in 1..<markup.points.count {
            total += distanceMM(markup.points[k - 1], markup.points[k])
        }
        if markup.type.isClosed {
            total += distanceMM(markup.points.last!, markup.points.first!)
        }
        return total
    }

    // Angle at the middle vertex of a 3-point angle markup, in degrees.
    func angleDegrees(of markup: Markup) -> Double? {
        guard markup.type == .angle, markup.points.count == 3 else { return nil }
        let a = physical(markup.points[0])
        let b = physical(markup.points[1])
        let c = physical(markup.points[2])
        let u = a - b, v = c - b
        let lu = (u * u).sum().squareRoot()
        let lv = (v * v).sum().squareRoot()
        guard lu > 0, lv > 0 else { return nil }
        let cosine = max(-1.0, min(1.0, Double((u * v).sum()) / Double(lu * lv)))
        return acos(cosine) * 180.0 / .pi
    }

    // MARK: - Helpers

    private func index(of id: UUID?) -> Int? {
        guard let id else { return nil }
        return markups.firstIndex { $0.id == id }
    }

    private func physical(_ voxel: SIMD3<Int>) -> SIMD3<Float> {
        SIMD3(Float(voxel.x) * volume.spacing.x,
              Float(voxel.y) * volume.spacing.y,
              Float(voxel.z) * volume.spacing.z)
    }

    private func distanceMM(_ a: SIMD3<Int>, _ b: SIMD3<Int>) -> Double {
        let d = physical(a) - physical(b)
        return Double((d * d).sum().squareRoot())
    }
}
