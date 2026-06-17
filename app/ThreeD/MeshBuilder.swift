import SceneKit
import AppKit
import LumenCore

// Builds an SCNGeometry from the C++ mesh buffers the bridge exposes. Copies the
// vertex/normal/index data once into SCNGeometrySources/Element (SceneKit needs
// to own it); the bridge buffers stay valid until the next generate.
enum MeshBuilder {
    static func geometry(from handle: OpaquePointer) -> SCNGeometry? {
        let vcount = Int(lumen_mesh_vertex_count(handle))
        let icount = Int(lumen_mesh_index_count(handle))
        guard vcount > 0, icount > 0,
              let vptr = lumen_mesh_vertices(handle),
              let nptr = lumen_mesh_normals(handle),
              let iptr = lumen_mesh_indices(handle) else { return nil }

        let stride = 3 * MemoryLayout<Float>.size
        let vData = Data(bytes: vptr, count: vcount * stride)
        let nData = Data(bytes: nptr, count: vcount * stride)
        let iData = Data(bytes: iptr, count: icount * MemoryLayout<UInt32>.size)

        let vSource = SCNGeometrySource(
            data: vData, semantic: .vertex, vectorCount: vcount,
            usesFloatComponents: true, componentsPerVector: 3,
            bytesPerComponent: MemoryLayout<Float>.size, dataOffset: 0,
            dataStride: stride)
        let nSource = SCNGeometrySource(
            data: nData, semantic: .normal, vectorCount: vcount,
            usesFloatComponents: true, componentsPerVector: 3,
            bytesPerComponent: MemoryLayout<Float>.size, dataOffset: 0,
            dataStride: stride)
        let element = SCNGeometryElement(
            data: iData, primitiveType: .triangles, primitiveCount: icount / 3,
            bytesPerIndex: MemoryLayout<UInt32>.size)

        let geo = SCNGeometry(sources: [vSource, nSource], elements: [element])
        let material = SCNMaterial()
        material.diffuse.contents = NSColor(calibratedRed: 0.87, green: 0.85,
                                            blue: 0.78, alpha: 1) // bone-ish
        material.lightingModel = .physicallyBased
        material.roughness.contents = 0.55
        material.isDoubleSided = true
        geo.materials = [material]
        return geo
    }
}
