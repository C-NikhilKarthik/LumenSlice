import SwiftUI

// One segment list row: visibility eye, colour well, editable name, voxel count,
// active selection, and delete. Tapping the row (outside the controls) makes it
// active. Name edits commit on Return / focus loss to avoid per-keystroke churn.
// Lives in its own file to keep SegmentControls under the file-length limit.
struct SegmentListRow: View {
    let row: SegmentRow
    let isActive: Bool
    let seg: SegmentationModel

    @State private var editingName: String = ""
    @State private var showColors = false
    @FocusState private var nameFocused: Bool

    var body: some View {
        HStack(spacing: 8) {
            Button {
                seg.setVisible(row.id, !row.visible)
            } label: {
                Image(systemName: row.visible ? "eye.fill" : "eye.slash")
                    .foregroundStyle(row.visible ? .primary : .secondary)
            }
            .buttonStyle(.borderless)

            // A plain swatch + popover palette, NOT SwiftUI's ColorPicker: the latter
            // drives the shared NSColorPanel, which lingers and re-opens on every
            // launch.
            Button {
                showColors.toggle()
            } label: {
                Circle()
                    .fill(row.color)
                    .frame(width: 15, height: 15)
                    .overlay(Circle().strokeBorder(.white.opacity(0.35), lineWidth: 1))
            }
            .buttonStyle(.borderless)
            .popover(isPresented: $showColors, arrowEdge: .bottom) {
                ColorPalettePopover(selected: row.color) { picked in
                    seg.setColor(row.id, picked)
                    showColors = false
                }
            }

            TextField("Name", text: $editingName)
                .textFieldStyle(.plain)
                .focused($nameFocused)
                .onSubmit { commitName() }
                .onChange(of: nameFocused) { focused in if !focused { commitName() } }

            Spacer(minLength: 4)

            Text(row.voxels.formatted())
                .font(.caption2.monospacedDigit())
                .foregroundStyle(.secondary)

            Button {
                seg.removeSegment(row.id)
            } label: {
                Image(systemName: "trash")
                    .foregroundStyle(.secondary)
            }
            .buttonStyle(.borderless)
        }
        .padding(.vertical, 2)
        .padding(.horizontal, 6)
        .background(isActive ? Color.accentColor.opacity(0.18) : Color.clear,
                    in: RoundedRectangle(cornerRadius: 6))
        .contentShape(Rectangle())
        .onTapGesture { seg.setActive(row.id) }
        .onAppear { editingName = row.name }
        .onChange(of: row.name) { newName in if !nameFocused { editingName = newName } }
    }

    // Explicitly main-actor: older Swift toolchains (the CI runner) don't infer
    // MainActor isolation for a View's non-body method, so the call to the
    // @MainActor `seg.rename` would otherwise fail to build there.
    @MainActor
    private func commitName() {
        if editingName != row.name { seg.rename(row.id, to: editingName) }
    }
}

// A small grid of preset colour swatches shown in a popover. Replaces SwiftUI's
// ColorPicker so we never touch the shared NSColorPanel (which otherwise lingers
// and re-opens on every launch). Shared by the segment list and (indirectly) any
// other palette-driven control.
struct ColorPalettePopover: View {
    let selected: Color
    let onPick: (Color) -> Void

    private let columns = Array(repeating: GridItem(.fixed(26), spacing: 8), count: 4)

    var body: some View {
        LazyVGrid(columns: columns, spacing: 8) {
            ForEach(Array(SegmentationModel.paletteColors.enumerated()), id: \.offset) {
                _, color in
                Button {
                    onPick(color)
                } label: {
                    Circle()
                        .fill(color)
                        .frame(width: 22, height: 22)
                        .overlay(
                            Circle().strokeBorder(.primary.opacity(0.6), lineWidth: 2)
                                .opacity(color == selected ? 1 : 0))
                }
                .buttonStyle(.borderless)
            }
        }
        .padding(12)
    }
}
