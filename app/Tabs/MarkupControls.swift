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

// One markup row: coloured kind icon, editable name, kind label, delete. Name edits
// commit on Return / focus loss (mirrors the segment list's behaviour).
private struct MarkupRow: View {
    let m: MarkupModel.Markup
    let markup: MarkupModel

    @State private var editingName = ""
    @FocusState private var focused: Bool

    var body: some View {
        HStack(spacing: 8) {
            Image(systemName: m.kind.icon)
                .foregroundStyle(markup.color(m))
                .frame(width: 16)
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
        .onAppear { editingName = m.name }
        .onChange(of: m.name) { n in if !focused { editingName = n } }
    }

    @MainActor private func commit() {
        if editingName != m.name { markup.rename(m.id, to: editingName) }
    }
}
