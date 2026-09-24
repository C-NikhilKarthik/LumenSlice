import SwiftUI
import AppKit
import UniformTypeIdentifiers

// Quantify-tab controls: measure every non-empty segment (volume, surface area, HU
// statistics) and save the table as CSV. The measurement runs in StatisticsModel;
// the results render in the canvas (StatsTable). Mirrors the Export tab's shape.
struct QuantifyControls: View {
    @EnvironmentObject var model: VolumeModel
    @EnvironmentObject var seg: SegmentationModel
    @EnvironmentObject var stats: StatisticsModel
    @State private var message: String?

    // Segments with voxels are the ones that can be measured.
    private var measurableCount: Int {
        seg.segments.filter { $0.voxels > 0 }.count
    }

    var body: some View {
        Form {
            if !model.hasVolume {
                Section {
                    Text("Open a DICOM folder to measure segments.")
                        .font(.callout)
                        .foregroundStyle(.secondary)
                }
            } else {
                measureSection
                exportSection
                if let message {
                    Section {
                        Text(message)
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                }
            }
        }
        .formStyle(.grouped)
        .scrollContentBackground(.hidden)
    }

    private var measureSection: some View {
        Section {
            Button {
                stats.measure()
            } label: {
                Label(stats.isComputing ? "Measuring…" : "Measure segments",
                      systemImage: "function")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(.borderedProminent)
            .controlSize(.large)
            .disabled(measurableCount == 0 || stats.isComputing)

            if measurableCount == 0 {
                Text("Segment a structure first (Segment tab).")
                    .font(.caption2)
                    .foregroundStyle(.secondary)
            } else if stats.isStale {
                Label("Segmentation changed - measure again to refresh.",
                      systemImage: "exclamationmark.triangle")
                    .font(.caption2)
                    .foregroundStyle(.orange)
            } else if !stats.rows.isEmpty {
                Text("\(stats.rows.count) segment"
                     + (stats.rows.count == 1 ? "" : "s") + " measured.")
                    .font(.caption2)
                    .foregroundStyle(.secondary)
            }
        } header: {
            InfoHeader("Measure",
                       help: "Computes each non-empty segment's volume (from the "
                           + "voxels and from a closed surface), the surface area, "
                           + "and the HU distribution. Results show in the table to "
                           + "the right.")
        }
    }

    private var exportSection: some View {
        Section {
            Button {
                exportCSV()
            } label: {
                Label("Export CSV…", systemImage: "tablecells")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(.bordered)
            .disabled(stats.rows.isEmpty || stats.isComputing)
        } header: {
            InfoHeader("Export",
                       help: "Save the measured table as a CSV file (geometry in "
                           + "millimetres). Measure first if the button is disabled.")
        }
    }

    private func exportCSV() {
        let csv = stats.csv()
        guard !csv.isEmpty else { return }
        let panel = NSSavePanel()
        panel.nameFieldStringValue = "SurgNetra-statistics.csv"
        panel.allowedContentTypes = [.commaSeparatedText]
        guard panel.runModal() == .OK, let url = panel.url else { return }
        do {
            try csv.write(to: url, atomically: true, encoding: .utf8)
            message = "Saved \(url.lastPathComponent)."
        } catch {
            message = "CSV export failed: \(error.localizedDescription)"
        }
    }
}
