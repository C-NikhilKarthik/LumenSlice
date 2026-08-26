import SwiftUI

// Sheet shown when an opened folder holds more than one DICOM series. Lists each
// series ("<description or 'Series N'> - <modality> - <n> slices") and loads the
// chosen one on Load, or leaves the current volume untouched on Cancel. The first
// row (the largest series, since the scan is largest-first) is preselected.
struct SeriesPicker: View {
    @EnvironmentObject var model: VolumeModel
    @State private var selection: Int?

    var body: some View {
        let options = model.pendingSeries ?? []
        VStack(alignment: .leading, spacing: 0) {
            VStack(alignment: .leading, spacing: 4) {
                Text("Choose a series")
                    .font(.headline)
                Text("This folder contains \(options.count) DICOM series. "
                     + "Pick the one to open.")
                    .font(.callout)
                    .foregroundStyle(.secondary)
            }
            .padding(20)

            Divider()

            List(options, selection: $selection) { option in
                VStack(alignment: .leading, spacing: 2) {
                    Text(option.title)
                        .font(.body.weight(.medium))
                        .lineLimit(1)
                    Text(subtitle(option))
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
                .padding(.vertical, 2)
            }
            .frame(minHeight: 220)

            Divider()

            HStack {
                Spacer()
                Button("Cancel") { model.cancelSeries() }
                    .keyboardShortcut(.cancelAction)
                Button("Load") {
                    if let sel = selection { model.chooseSeries(sel) }
                }
                .keyboardShortcut(.defaultAction)
                .disabled(selection == nil)
            }
            .padding(20)
        }
        .frame(width: 460)
        // Preselect the first (largest) series, matching lumen_load_folder's default.
        .onAppear { if selection == nil { selection = options.first?.id } }
    }

    // "<modality> · <W × H> · <n> slices · <date>" - the secondary line under the
    // series title. Fields the scan didn't report are simply omitted.
    private func subtitle(_ o: SeriesOption) -> String {
        var parts: [String] = []
        if !o.modality.isEmpty { parts.append(o.modality) }
        if !o.sizeText.isEmpty { parts.append(o.sizeText) }
        parts.append("\(o.sliceCount) slice\(o.sliceCount == 1 ? "" : "s")")
        if !o.created.isEmpty { parts.append(o.created) }
        return parts.joined(separator: " · ")
    }
}
