import simd

// Pure geometry for the 3D camera, factored out of Scene3DController so it can be
// unit tested without a live SCNView (mirrors RangeSliderMath / SliceCoordinates).
enum Scene3DMath {
    // The eight corners of an axis-aligned box, so a rotated node's world bounds are
    // covered exactly (min/max alone under-cover once a transform rotates the box).
    static func corners(min lo: SIMD3<Float>, max hi: SIMD3<Float>) -> [SIMD3<Float>] {
        [SIMD3(lo.x, lo.y, lo.z), SIMD3(hi.x, lo.y, lo.z),
         SIMD3(lo.x, hi.y, lo.z), SIMD3(hi.x, hi.y, lo.z),
         SIMD3(lo.x, lo.y, hi.z), SIMD3(hi.x, lo.y, hi.z),
         SIMD3(lo.x, hi.y, hi.z), SIMD3(hi.x, hi.y, hi.z)]
    }

    // Enclose world-space points in a centre + half-extent "radius". nil if empty.
    static func enclose(_ points: [SIMD3<Float>]) -> (center: SIMD3<Float>, radius: Float)? {
        guard var lo = points.first else { return nil }
        var hi = lo
        for p in points.dropFirst() {
            lo = simd.min(lo, p)
            hi = simd.max(hi, p)
        }
        let center = (lo + hi) / 2
        let radius = Swift.max(hi.x - lo.x, Swift.max(hi.y - lo.y, hi.z - lo.z)) / 2
        return (center, radius)
    }
}
