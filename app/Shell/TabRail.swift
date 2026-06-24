import SwiftUI

// The far-left vertical icon rail. One button per WorkspaceTab; the active tab
// is highlighted. This is the "tabs on the left" surface — picking one swaps the
// control panel and canvas to its right (see AppShell).
struct TabRail: View {
    @Binding var selection: WorkspaceTab

    var body: some View {
        VStack(spacing: 6) {
            ForEach(WorkspaceTab.allCases) { tab in
                TabRailButton(tab: tab, isSelected: selection == tab) {
                    selection = tab
                }
            }
            Spacer()
        }
        .padding(.top, 56)        // clear the transparent titlebar
        .padding(.bottom, 14)
        .frame(width: 66)
        .frame(maxHeight: .infinity)
        .background(
            VisualEffectView(material: .underWindowBackground, blendingMode: .behindWindow)
                .ignoresSafeArea()
        )
        .overlay(alignment: .trailing) {
            Divider().opacity(0.4)
        }
    }
}

private struct TabRailButton: View {
    let tab: WorkspaceTab
    let isSelected: Bool
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            VStack(spacing: 3) {
                Image(systemName: tab.icon)
                    .font(.system(size: 18, weight: .medium))
                Text(tab.title)
                    .font(.system(size: 9, weight: .medium))
            }
            .frame(width: 54, height: 48)
            .foregroundStyle(isSelected ? AnyShapeStyle(.tint) : AnyShapeStyle(.secondary))
            .background(
                isSelected ? Color.accentColor.opacity(0.15) : Color.clear,
                in: RoundedRectangle(cornerRadius: 10)
            )
            .contentShape(RoundedRectangle(cornerRadius: 10))
        }
        .buttonStyle(.plain)
        .help(tab.title)
    }
}
