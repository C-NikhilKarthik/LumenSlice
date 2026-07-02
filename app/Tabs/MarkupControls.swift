import SwiftUI

// Markups-tab controls: pick a type and add a markup, then click in any slice to
// drop its points. The list below is the Name / Type / Colour / Toggle table with
// per-markup visibility, colour, rename, point count, and delete.
struct MarkupControls: View {
    @EnvironmentObject var model: VolumeModel
    @EnvironmentObject var markups: MarkupsModel

    var body: some View {
        Form {
            if !model.hasVolume {
                Section {
                    Text("Open a DICOM folder to place markups.")
                        .font(.callout)
                        .foregroundStyle(.secondary)
                }
            } else {
                newSection
                activeSection
                listSection
            }
        }
        .formStyle(.grouped)
        .scrollContentBackground(.hidden)
    }

    private var newSection: some View {
        Section("New markup") {
            Text("Pick a type, Add it, then click in any slice to drop points. "
                 + "Line, angle, and plane cap their points; point lists and curves "
                 + "are open-ended.")
                .font(.caption2)
                .foregroundStyle(.secondary)
            Picker("Type", selection: $markups.newType) {
                ForEach(MarkupType.allCases) { type in
                    Label(type.title, systemImage: type.icon).tag(type)
                }
            }
            .pickerStyle(.menu)
            Button { markups.addMarkup() } label: {
                Label("Add \(markups.newType.title)", systemImage: "plus")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(.borderedProminent)
            .controlSize(.small)
        }
    }

    @ViewBuilder private var activeSection: some View {
        if let active = markups.active {
            Section("Active: \(active.name)") {
                LabeledContent("Points", value: "\(active.points.count)")
                if let len = markups.length(of: active) {
                    LabeledContent("Length", value: String(format: "%.1f mm", len))
                }
                if let ang = markups.angleDegrees(of: active) {
                    LabeledContent("Angle", value: String(format: "%.1f\u{00B0}", ang))
                }
                HStack(spacing: 8) {
                    Button { markups.removeLastPoint() } label: {
                        Label("Undo point", systemImage: "arrow.uturn.backward")
                            .frame(maxWidth: .infinity)
                    }
                    Button { markups.clearActivePoints() } label: {
                        Label("Clear points", systemImage: "xmark")
                            .frame(maxWidth: .infinity)
                    }
                }
                .buttonStyle(.bordered)
                .controlSize(.small)
            }
        }
    }

    private var listSection: some View {
        Section {
            ForEach(markups.markups) { markup in
                MarkupRow(markup: markup,
                          isActive: markup.id == markups.activeID,
                          markups: markups)
            }
            if markups.markups.isEmpty {
                Text("No markups yet. Add one above.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
        } header: {
            HStack {
                Text("Markups")
                Spacer()
                if !markups.markups.isEmpty {
                    Button(role: .destructive) { markups.removeAll() } label: {
                        Label("Remove all", systemImage: "trash")
                            .labelStyle(.iconOnly)
                    }
                    .buttonStyle(.borderless)
                }
            }
        }
    }
}

// One markup list row: visibility eye, colour well, editable name + type, point
// count, delete. Tapping the row activates it (new points target the active markup).
private struct MarkupRow: View {
    let markup: Markup
    let isActive: Bool
    let markups: MarkupsModel

    @State private var editingName = ""
    @State private var showColors = false
    @FocusState private var nameFocused: Bool

    var body: some View {
        HStack(spacing: 8) {
            Button {
                markups.setVisible(markup.id, !markup.visible)
            } label: {
                Image(systemName: markup.visible ? "eye.fill" : "eye.slash")
                    .foregroundStyle(markup.visible ? .primary : .secondary)
            }
            .buttonStyle(.borderless)

            Button {
                showColors.toggle()
            } label: {
                Circle()
                    .fill(markup.color)
                    .frame(width: 15, height: 15)
                    .overlay(Circle().strokeBorder(.white.opacity(0.35), lineWidth: 1))
            }
            .buttonStyle(.borderless)
            .popover(isPresented: $showColors, arrowEdge: .bottom) {
                ColorPalettePopover(selected: markup.color) { picked in
                    markups.setColor(markup.id, picked)
                    showColors = false
                }
            }

            VStack(alignment: .leading, spacing: 1) {
                TextField("Name", text: $editingName)
                    .textFieldStyle(.plain)
                    .focused($nameFocused)
                    .onSubmit { commitName() }
                    .onChange(of: nameFocused) { focused in if !focused { commitName() } }
                Text(markup.type.title)
                    .font(.caption2)
                    .foregroundStyle(.secondary)
            }

            Spacer(minLength: 4)

            Text("\(markup.points.count)")
                .font(.caption2.monospacedDigit())
                .foregroundStyle(.secondary)

            Button {
                markups.removeMarkup(markup.id)
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
        .onTapGesture { markups.setActive(markup.id) }
        .onAppear { editingName = markup.name }
        .onChange(of: markup.name) { newName in if !nameFocused { editingName = newName } }
    }

    @MainActor
    private func commitName() {
        if editingName != markup.name { markups.rename(markup.id, to: editingName) }
    }
}
