import SwiftUI

// Full DICOM metadata view, presented as a sheet. Left: curated, human-readable
// panels (Patient / Study / Series / Equipment). Right: a searchable table of
// every top-level tag (group/element, VR, name, value). Mirrors the Slicer /
// OsiriX "show all metadata" experience.
struct MetadataInspector: View {
    let metadata: DicomMetadata

    @Environment(\.dismiss) private var dismiss
    @State private var search = ""

    private var indexedTags: [IndexedTag] {
        metadata.tags.enumerated().map { IndexedTag(id: $0.offset, tag: $0.element) }
    }

    private var filteredTags: [IndexedTag] {
        guard !search.isEmpty else { return indexedTags }
        let query = search.lowercased()
        return indexedTags.filter {
            $0.tag.name.lowercased().contains(query) ||
            $0.tag.ge.lowercased().contains(query) ||
            $0.tag.vr.lowercased().contains(query) ||
            $0.tag.value.lowercased().contains(query)
        }
    }

    var body: some View {
        VStack(spacing: 0) {
            header
            Divider()
            HSplitView {
                ScrollView { curatedPanels.padding(18) }
                    .frame(minWidth: 300)
                tagTable
                    .frame(minWidth: 440)
            }
        }
        .frame(minWidth: 860, minHeight: 540)
    }

    private var header: some View {
        HStack {
            Image(systemName: "list.bullet.rectangle.portrait")
                .foregroundStyle(.tint)
            Text("DICOM Metadata").font(.title2.weight(.semibold))
            Spacer()
            Button("Done") { dismiss() }
                .keyboardShortcut(.defaultAction)
        }
        .padding()
    }

    // MARK: Curated panels

    private var curatedPanels: some View {
        let m = metadata.meta
        return VStack(alignment: .leading, spacing: 18) {
            panel("Patient", [
                ("Name", m.patientName), ("ID", m.patientId),
                ("Birth date", m.patientBirthDate), ("Sex", m.patientSex),
                ("Age", m.patientAge),
            ])
            panel("Study", [
                ("Date", m.studyDate), ("Time", m.studyTime),
                ("Description", m.studyDescription),
                ("Accession", m.accessionNumber),
                ("Referring", m.referringPhysician),
                ("UID", m.studyInstanceUid),
            ])
            panel("Series", [
                ("Modality", m.modality), ("Description", m.seriesDescription),
                ("Number", m.seriesNumber), ("Body part", m.bodyPart),
                ("UID", m.seriesInstanceUid),
            ])
            panel("Equipment", [
                ("Manufacturer", m.manufacturer), ("Model", m.modelName),
                ("Station", m.stationName), ("Institution", m.institutionName),
                ("Software", m.softwareVersions),
            ])
        }
    }

    private func panel(_ title: String, _ rows: [(String, String)]) -> some View {
        let nonEmpty = rows.filter { !$0.1.isEmpty }
        return VStack(alignment: .leading, spacing: 6) {
            Text(title).font(.headline)
            if nonEmpty.isEmpty {
                Text("No data").font(.caption).foregroundStyle(.secondary)
            } else {
                ForEach(nonEmpty, id: \.0) { row in
                    HStack(alignment: .top, spacing: 8) {
                        Text(row.0)
                            .foregroundStyle(.secondary)
                            .frame(width: 96, alignment: .leading)
                        Text(row.1)
                            .textSelection(.enabled)
                        Spacer(minLength: 0)
                    }
                    .font(.callout)
                }
            }
        }
    }

    // MARK: Searchable tag table

    private var tagTable: some View {
        VStack(spacing: 0) {
            HStack(spacing: 6) {
                Image(systemName: "magnifyingglass").foregroundStyle(.secondary)
                TextField(
                    "Filter \(metadata.tags.count) tags by name, group, VR, or value",
                    text: $search)
                    .textFieldStyle(.plain)
                if !search.isEmpty {
                    Button { search = "" } label: { Image(systemName: "xmark.circle.fill") }
                        .buttonStyle(.plain)
                        .foregroundStyle(.secondary)
                }
            }
            .padding(8)
            Divider()
            Table(filteredTags) {
                TableColumn("Tag") { Text($0.tag.ge).monospaced() }.width(92)
                TableColumn("VR") { Text($0.tag.vr).monospaced() }.width(38)
                TableColumn("Name") { Text($0.tag.name) }.width(min: 140, ideal: 190)
                TableColumn("Value") { Text($0.tag.value).textSelection(.enabled) }
            }
        }
    }
}
