import SwiftUI

// Markups-tab controls: pick a markup type (point / line / plane), toggle placement
// on, then click the slice panes to drop the defining points. The canvas is the
// same 2×2 quad as Visualize, so points land on the slices and appear live in the
// 3D pane. A list below lets you rename or delete them.
struct MarkupControls: View {
    @EnvironmentObject var model: VolumeModel
    @EnvironmentObject var markup: MarkupModel

    var body: some View {
        Form {
            if !model.hasVolume {
                Section {
                    Text("Open a DICOM folder to place markups.")
                        .font(.callout)
                        .foregroundStyle(.secondary)
                }
            } else {
                infoSection
                typeSection
                colorSection
                placeSection
                listSection
            }
        }
        .formStyle(.grouped)
        .scrollContentBackground(.hidden)
    }

    private var infoSection: some View {
        Section {
            Text("Drop points on any slice pane; they show in the 3D pane. A Point "
                 + "is one click, a Line is two, a Plane is three (a triangle).")
                .font(.caption)
                .foregroundStyle(.secondary)
        }
    }

    private var typeSection: some View {
        Section("Type") {
            Picker("Type", selection: $markup.kind) {
                ForEach(MarkupModel.Kind.allCases) { k in
                    Label(k.title, systemImage: k.icon).tag(k)
                }
            }
            .pickerStyle(.segmented)
            .labelsHidden()
        }
    }

    private var colorSection: some View {
        Section("Colour") {
            MarkupPalette(selected: markup.nextColorIndex) { markup.pickNextColor($0) }
            Text("New markups use this colour (the points you drop show it live) and "
                 + "keep using it until you pick another. Recolour an existing markup "
                 + "from the list below.")
                .font(.caption2)
                .foregroundStyle(.secondary)
        }
    }

    private var placeSection: some View {
        Section {
            Toggle(isOn: $markup.placing) {
                Label("Place markups", systemImage: "hand.tap")
            }
            if !markup.pending.isEmpty {
                HStack {
                    Text("\(markup.pending.count)/\(markup.kind.pointsNeeded) points placed…")
                        .font(.caption2)
                        .foregroundStyle(.secondary)
                    Spacer()
                    Button("Cancel point") { markup.cancelPending() }
                        .buttonStyle(.borderless)
                        .controlSize(.small)
                }
            }
            Text(markup.placing
                 ? "Click a slice to drop a point."
                 : "Turn on, then click the slices.")
                .font(.caption2)
                .foregroundStyle(.secondary)
        }
    }

    private var listSection: some View {
        Section("Markups") {
            if markup.markups.isEmpty {
                Text("No markups yet.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            ForEach(markup.markups) { m in
                MarkupRow(m: m, markup: markup)
            }
            if !markup.markups.isEmpty {
                Button(role: .destructive) {
                    markup.removeAll()
                } label: {
                    Label("Clear all", systemImage: "trash").frame(maxWidth: .infinity)
                }
                .buttonStyle(.bordered)
                .controlSize(.small)
            }
        }
    }
}

// One markup row: a visibility toggle, a colour swatch (tap to recolour), an
// editable name, the kind, and delete. Name edits commit on Return / focus loss
// (mirrors the segment list's behaviour).
private struct MarkupRow: View {
    let m: MarkupModel.Markup
    let markup: MarkupModel

    @State private var editingName = ""
    @State private var pickingColor = false
    @FocusState private var focused: Bool

    var body: some View {
        HStack(spacing: 8) {
            Button {
                markup.setVisible(m.id, !m.visible)
            } label: {
                Image(systemName: m.visible ? "eye" : "eye.slash")
                    .foregroundStyle(.secondary)
            }
            .buttonStyle(.borderless)
            .help(m.visible ? "Hide" : "Show")

            Button {
                pickingColor = true
            } label: {
                Image(systemName: m.kind.icon)
                    .foregroundStyle(markup.color(m))
                    .frame(width: 16)
            }
            .buttonStyle(.borderless)
            .popover(isPresented: $pickingColor, arrowEdge: .bottom) {
                MarkupPalette(selected: m.colorIndex) {
                    markup.setColorIndex(m.id, $0)
                    pickingColor = false
                }
                .padding(12)
            }

            TextField("Name", text: $editingName)
                .textFieldStyle(.plain)
                .focused($focused)
                .onSubmit { commit() }
                .onChange(of: focused) { f in if !f { commit() } }
            Spacer(minLength: 4)
            Text(m.kind.title)
                .font(.caption2)
                .foregroundStyle(.secondary)
            Button {
                markup.remove(m.id)
            } label: {
                Image(systemName: "trash").foregroundStyle(.secondary)
            }
            .buttonStyle(.borderless)
        }
        .padding(.vertical, 2)
        .opacity(m.visible ? 1 : 0.5)
        .onAppear { editingName = m.name }
        .onChange(of: m.name) { n in if !focused { editingName = n } }
    }

    @MainActor private func commit() {
        if editingName != m.name { markup.rename(m.id, to: editingName) }
    }
}

// A row of the eight markup palette swatches; taps report the chosen index. Used
// both for the next-markup colour and to recolour an existing markup in a popover.
private struct MarkupPalette: View {
    let selected: Int
    let onPick: (Int) -> Void

    var body: some View {
        HStack(spacing: 6) {
            ForEach(Array(MarkupModel.palette.enumerated()), id: \.offset) { i, color in
                Circle()
                    .fill(color)
                    .frame(width: 18, height: 18)
                    .overlay(Circle().strokeBorder(.primary.opacity(0.15), lineWidth: 1))
                    .overlay(Circle().strokeBorder(.white, lineWidth: selected == i ? 2 : 0))
                    .onTapGesture { onPick(i) }
            }
        }
    }
}
