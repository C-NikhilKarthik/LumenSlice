#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QStyleFactory>

#include "MainWindow.h"
#include "Theme.h"

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
    QApplication::setApplicationName("SurgNetra");
    QApplication::setOrganizationName("SurgNetra");
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    app.setStyleSheet(lumenwin::kAppStyle);
    configureDicomDictionary();

    // app.rc embeds the same icon into the .exe for Explorer/taskbar; this sets
    // it for the running window/Alt-Tab, mirroring the resources/dicom.dic
    // beside-the-exe convention above.
    const QString iconPath =
        QCoreApplication::applicationDirPath() + "/resources/app.ico";
    if (QFileInfo::exists(iconPath)) QApplication::setWindowIcon(QIcon(iconPath));

    lumenwin::MainWindow window;
    window.show();

    // Optional: `SurgNetra.exe <folder>` auto-loads a DICOM folder on launch.
    const QStringList args = QApplication::arguments();
    if (args.size() > 1 && QFileInfo(args.at(1)).isDir())
        window.loadFolder(args.at(1));

    return app.exec();
}
