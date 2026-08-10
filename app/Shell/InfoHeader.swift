import SwiftUI

// A Form section header: a title plus a small "ⓘ" button that reveals a longer
// explanation in a popover (with a native hover tooltip too). Keeps help text out of
// the section body so the panels read as controls, not congested paragraphs. Use it
// in place of the string `Section("Title")` initializer:
//
//   Section { …controls… } header: { InfoHeader("Title", help: "…explanation…") }
//
struct InfoHeader: View {
    let title: String
    let help: String
    @State private var showHelp = false

    init(_ title: String, help: String) {
        self.title = title
        self.help = help
    }

    var body: some View {
        HStack(spacing: 4) {
            Text(title)
            Button {
                showHelp.toggle()
            } label: {
                Image(systemName: "info.circle")
            }
            .buttonStyle(.borderless)
            .foregroundStyle(.secondary)
            .help(help)
            .accessibilityLabel("\(title) help")
            .popover(isPresented: $showHelp, arrowEdge: .bottom) {
                Text(help)
                    .font(.callout)
                    .frame(width: 260, alignment: .leading)
                    .fixedSize(horizontal: false, vertical: true)
                    .padding(12)
            }
            Spacer()
        }
    }
}
