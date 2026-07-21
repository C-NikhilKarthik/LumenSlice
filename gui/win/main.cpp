#include <QApplication>
#include <QDir>
#include <QFileInfo>

#include "MainWindow.h"

// Point DCMTK at the bundled data dictionary (resources/dicom.dic beside the exe)
// before any DICOM parsing — mirrors the setup the original Win32 shell did.
static void configureDicomDictionary() {
    const QString dic =
        QCoreApplication::applicationDirPath() + "/resources/dicom.dic";
    if (QFileInfo::exists(dic))
        qputenv("DCMDICTPATH", QDir::toNativeSeparators(dic).toUtf8());
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("LumenSlice");
    QApplication::setOrganizationName("LumenSlice");
    configureDicomDictionary();

    lumenwin::MainWindow window;
    window.show();

    // Optional: `LumenSlice.exe <folder>` auto-loads a DICOM folder on launch.
    const QStringList args = QApplication::arguments();
    if (args.size() > 1 && QFileInfo(args.at(1)).isDir())
        window.loadFolder(args.at(1));

    return app.exec();
}
