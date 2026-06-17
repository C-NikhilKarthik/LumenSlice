import Foundation

// The four workflow stages on the left rail. Selecting a tab swaps the left
// control panel and the central canvas; only the active tab's controls show.
//
//   Visualize -> W/L + slice navigation        | 3 slice panes
//   Segment   -> threshold / grow / paint       | 3 panes + mask overlay
//   3D        -> marching cubes + mesh controls  | rotatable SceneKit mesh
//   Export    -> PNG / STL / report             | preview
enum WorkspaceTab: String, CaseIterable, Identifiable {
    case visualize
    case segment
    case threeD
    case export

    var id: String { rawValue }

    var title: String {
        switch self {
        case .visualize: return "Visualize"
        case .segment: return "Segment"
        case .threeD: return "3D"
        case .export: return "Export"
        }
    }

    // SF Symbol for the rail icon.
    var icon: String {
        switch self {
        case .visualize: return "square.stack.3d.up"
        case .segment: return "scribble.variable"
        case .threeD: return "cube"
        case .export: return "square.and.arrow.up"
        }
    }
}
