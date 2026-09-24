import SwiftUI

// The Quantify tab's canvas: a table of per-segment measurements. Reads the
// StatisticsModel the Quantify controls populate; shows an empty state until the
// first measurement and a "stale" banner when the mask changed since. Geometry is
// converted from mm to the friendlier cm here (the CSV export keeps raw mm).
//
// The columns are laid out by weight across the full canvas width (rather than a
// content-sized Grid that bunches at the left) so the data spreads out and reads
// clearly on the wide 3D-less canvas.
struct StatsTable: View {
    @EnvironmentObject var model: VolumeModel
    @EnvironmentObject var stats: StatisticsModel

    // Column titles and their relative widths. Volume LM = voxel volume (reference,
    // matches Slicer's Volume LM); Volume CS = closed-surface volume (Slicer's CS).
    private let columns: [(title: String, weight: CGFloat)] = [
        ("Segment", 1.6), ("Voxels", 1.2), ("Volume LM (cm³)", 1.3),
        ("Volume CS (cm³)", 1.3), ("Surface (cm²)", 1.2), ("HU mean", 0.9),
        ("HU σ", 0.8), ("HU range", 1.2),
    ]

    var body: some View {
        ZStack {
            Color(nsColor: .textBackgroundColor).opacity(0.4)
            if !model.hasVolume {
                placeholder("Open a DICOM folder to measure segments.",
                            systemImage: "tablecells")
            } else if stats.rows.isEmpty {
                placeholder(stats.isComputing
                                ? "Measuring…"
                                : "Measure segments to see volume, surface area, "
                                    + "and HU statistics.",
                            systemImage: "function")
            } else {
                VStack(spacing: 0) {
                    if stats.isStale { staleBanner }
                    GeometryReader { geo in
                        let widths = columnWidths(for: geo.size.width - 32)
                        ScrollView(.vertical) {
                            VStack(alignment: .leading, spacing: 0) {
                                headerRow(widths)
                                    .padding(.bottom, 8)
                                Divider()
                                ForEach(stats.rows) { row in
                                    dataRow(row, widths)
                                        .padding(.vertical, 9)
                                    Divider().opacity(0.25)
                                }
                            }
                            .padding(.horizontal, 16)
                            .padding(.top, 16)
                        }
                    }
                }
            }
        }
    }

    // Spread the columns across the available width by weight.
    private func columnWidths(for total: CGFloat) -> [CGFloat] {
        let sum = columns.reduce(0) { $0 + $1.weight }
        let usable = max(total, 1)
        return columns.map { usable * $0.weight / sum }
    }

    private var staleBanner: some View {
        HStack(spacing: 6) {
            Image(systemName: "exclamationmark.triangle.fill")
            Text("The segmentation changed since these were measured. "
                 + "Measure again to refresh.")
                .font(.caption)
            Spacer()
        }
        .padding(.horizontal, 14)
        .padding(.vertical, 8)
        .background(Color.orange.opacity(0.18))
        .foregroundStyle(.orange)
    }

    private func headerRow(_ widths: [CGFloat]) -> some View {
        HStack(spacing: 0) {
            ForEach(Array(columns.enumerated()), id: \.offset) { i, col in
                Text(col.title)
                    .font(.caption.weight(.semibold))
                    .foregroundStyle(.secondary)
                    .frame(width: widths[i], alignment: .leading)
            }
        }
    }

    private func dataRow(_ row: SegmentStatsRow, _ widths: [CGFloat]) -> some View {
        HStack(spacing: 0) {
            HStack(spacing: 8) {
                Circle().fill(row.color).frame(width: 12, height: 12)
                Text(row.name).lineLimit(1)
            }
            .frame(width: widths[0], alignment: .leading)
            cell(row.voxelCount.formatted(), widths[1])
            cell(cubicCentimetres(row.volumeMM3), widths[2])
            cell(cubicCentimetres(row.meshVolumeMM3), widths[3])
            cell(squareCentimetres(row.surfaceAreaMM2), widths[4])
            cell(hu(row.huMean), widths[5])
            cell(hu(row.huStdDev), widths[6])
            cell("\(hu(row.huMin)) – \(hu(row.huMax))", widths[7])
        }
        .font(.system(.body, design: .rounded))
    }

    private func cell(_ text: String, _ width: CGFloat) -> some View {
        Text(text)
            .monospacedDigit()
            .lineLimit(1)
            .frame(width: width, alignment: .leading)
    }

    private func placeholder(_ text: String, systemImage: String) -> some View {
        VStack(spacing: 12) {
            Image(systemName: systemImage)
                .font(.system(size: 46, weight: .thin))
                .foregroundStyle(.secondary)
            Text(text)
                .font(.callout)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
        }
        .padding()
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    // mm^3 -> cm^3, mm^2 -> cm^2, HU rounded to whole units.
    private func cubicCentimetres(_ mm3: Double) -> String {
        String(format: "%.2f", mm3 / 1000.0)
    }
    private func squareCentimetres(_ mm2: Double) -> String {
        String(format: "%.2f", mm2 / 100.0)
    }
    private func hu(_ v: Double) -> String {
        String(format: "%.0f", v)
    }
}
