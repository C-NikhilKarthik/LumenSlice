// Top-level Windows shell: a left icon rail selects one of four control panels
// (Visualize / Segment / 3D / Export); the central canvas shows the tri-axis slice
// board or the 3D mesh. All editing routes through the C bridge, and every mask
// mutation snapshots undo first. Mesh generation runs off the UI thread.
#pragma once

#include <QFutureWatcher>
#include <QHash>
#include <QMainWindow>
#include <QString>

#include "BridgeVolume.h"
#include "ViewState.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSlider;
class QSpinBox;
class QStackedWidget;
class QVBoxLayout;
class QWidget;

namespace lumenwin {

class SliceView;
class MeshView;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();

    // Load a DICOM folder programmatically (e.g. a command-line argument).
    void loadFolder(const QString& path) { loadPath(path); }

protected:
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;

private slots:
    void openFolder();
    void selectTab(int tab);

    // Canvas intents.
    void onSliceScrolled(int axis, int index);
    void onWindowLevelDragged(float dLevel, float dWindow);
    void onFocusPicked(int x, int y, int z);
    void onPaintStroke(int axis, int index, int cx, int cy, int radius, bool add);
    void onFloodClicked(int x, int y, int z);
    void onLevelTraceClicked(int axis, int index, int cx, int cy);
    void onStrokeBegan();

    // Segment ops.
    void addSegment();
    void applyThreshold();
    void applyOtsu();
    void refineGrow();
    void refineShrink();
    void refineSmooth();
    void growFromSeeds();
    void undo();
    void redo();
    void clearActive();

    // 3D + export.
    void generateMesh();
    void onMeshReady();
    void exportStl();
    void exportPng();

private:
    // UI construction.
    QWidget* buildTabRail();
    QWidget* buildVisualizePanel();
    QWidget* buildSegmentPanel();
    QWidget* buildThreeDPanel();
    QWidget* buildExportPanel();
    QWidget* buildSliceBoard();

    // Refresh helpers.
    void loadPath(const QString& path);
    void refreshAll();
    void refreshCanvas();
    void refreshSliders();
    void refreshVolumeInfo();
    void rebuildSegmentList();
    void updateSegmentCounts();
    void updateUndoRedo();
    void updateWlControls();
    void setStatus(const QString& text);
    void showMetadataInspector();
    QColor nextSegmentColor() const;

    BridgeVolume vol_;
    ViewState st_;
    QString metaJson_;

    // Canvas.
    QStackedWidget* central_ = nullptr;
    SliceView* panes_[3] = {nullptr, nullptr, nullptr};
    QSlider* sliders_[3] = {nullptr, nullptr, nullptr};
    MeshView* meshView_ = nullptr;

    // Panels.
    QStackedWidget* panels_ = nullptr;
    int currentTab_ = 0;

    // Visualize controls.
    QLabel* statusLabel_ = nullptr;
    QLabel* dimsLabel_ = nullptr;
    QLabel* spacingLabel_ = nullptr;
    QLabel* huLabel_ = nullptr;
    QLabel* patientLabel_ = nullptr;
    QDoubleSpinBox* levelSpin_ = nullptr;
    QDoubleSpinBox* windowSpin_ = nullptr;
    QSlider* levelSlider_ = nullptr;
    QSlider* windowSlider_ = nullptr;
    QCheckBox* crosshairCheck_ = nullptr;

    // Segment controls.
    QVBoxLayout* segListLayout_ = nullptr;
    QWidget* segListContainer_ = nullptr;
    QHash<int, QLabel*> segCountLabels_;
    QHash<int, QString> segNames_;
    QComboBox* toolCombo_ = nullptr;
    QStackedWidget* toolDetail_ = nullptr;
    QDoubleSpinBox* threshLoSpin_ = nullptr;
    QDoubleSpinBox* threshHiSpin_ = nullptr;
    QSlider* toleranceSlider_ = nullptr;
    QLabel* toleranceLabel_ = nullptr;
    QSlider* brushSlider_ = nullptr;
    QLabel* brushLabel_ = nullptr;
    QSlider* seedItersSlider_ = nullptr;
    QLabel* seedItersLabel_ = nullptr;
    QLabel* seedGateLabel_ = nullptr;
    QPushButton* growSeedsBtn_ = nullptr;
    QPushButton* undoBtn_ = nullptr;
    QPushButton* redoBtn_ = nullptr;
    QCheckBox* overlayCheck_ = nullptr;
    QLabel* totalVoxelsLabel_ = nullptr;

    // 3D controls.
    QSpinBox* smoothingSpin_ = nullptr;
    QComboBox* resolutionCombo_ = nullptr;
    QPushButton* generateBtn_ = nullptr;
    QLabel* meshInfoLabel_ = nullptr;

    // Export controls.
    QPushButton* exportStlBtn_ = nullptr;
    QPushButton* exportPngBtn_ = nullptr;
    QLabel* exportMsgLabel_ = nullptr;

    // Mesh generation (off-thread).
    QFutureWatcher<int> meshWatcher_;
    bool generating_ = false;
};

}  // namespace lumenwin
