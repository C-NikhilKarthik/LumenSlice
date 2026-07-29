// Top-level Windows shell: a left icon rail selects one of four control panels
// (Visualize / Segment / 3D / Export); the central canvas shows the tri-axis slice
// board or the 3D mesh. All editing routes through the C bridge, and every mask
// mutation snapshots undo first. Mesh generation runs off the UI thread.
#pragma once

#include <QFutureWatcher>
#include <QHash>
#include <QElapsedTimer>
#include <QList>
#include <QMainWindow>
#include <QPointF>
#include <QString>
#include <vector>

#include "BridgeVolume.h"
#include "MarkupModel.h"
#include "MeshView.h"
#include "RangeSlider.h"
#include "ViewState.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGridLayout;
class QLabel;
class QListWidget;
class QPushButton;
class QSlider;
class QSpinBox;
class QStackedWidget;
class QTimer;
class QToolButton;
class QVBoxLayout;
class QWidget;

namespace lumenwin {

// Result of an off-thread DICOM folder load.
struct LoadResult {
    LumenVolume* volume = nullptr;
    QString message;
};

class SliceView;

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
    void onLoadReady();
    void selectTab(int tab);

    // Canvas intents.
    void onSliceScrolled(int axis, int index);
    void onWindowLevelDragged(float dLevel, float dWindow);
    void onFocusPicked(int x, int y, int z);
    void onPaintStroke(int axis, int index, int cx, int cy, int radius, bool add);
    void onFloodClicked(int x, int y, int z);
    void onLevelTraceClicked(int axis, int index, int cx, int cy);
    void onStrokeBegan();
    void onStrokeEnded();

    // Segment ops.
    void addSegment();
    void applyThreshold();
    void applyOtsu();
    void refineGrow();
    void refineShrink();
    void refineHollow();
    void refineSmooth();
    void keepLargest();
    void removeSmallIslands();
    void growFromSeeds();
    void undo();
    void redo();
    void clearActive();

    // 3D + export.
    void generateMesh();
    void onMeshReady();
    void onScissorFinished(const QList<QPointF>& poly);
    void exportStl();
    void exportPng();

    // Markups.
    void onMarkupPointPicked(int x, int y, int z);

private:
    // UI construction.
    QWidget* buildTabRail();
    QWidget* buildVisualizePanel();
    QWidget* buildSegmentPanel();
    QWidget* buildThreeDPanel();
    QWidget* buildExportPanel();
    QWidget* buildMarkupPanel();
    QWidget* buildQuad();
    void toggleMaximize(int cell);
    void rebuildMarkupList();
    void updateMarkupPaletteSelection();
    void updateMarkupPending();
    void refreshMarkups();

    // Sequential per-segment colored mesh generation (one snapshot_label ->
    // generate -> readback per visible non-empty segment, worker off the UI thread).
    void startNextMeshSegment();
    void finishMeshGeneration();

    // Refresh helpers.
    void loadPath(const QString& path);
    void refreshAll();
    void refreshCanvas();
    void refreshSliders();
    void refreshVolumeInfo();
    void rebuildSegmentList();
    void rebuildExportSegmentList();
    void updateSegmentCounts();
    void updateUndoRedo();
    void updateWlControls();
    void setStatus(const QString& text);
    void showMetadataInspector();
    QColor nextSegmentColor() const;

    BridgeVolume vol_;
    ViewState st_;
    QString metaJson_;

    // Canvas: a 2x2 quad (Axial / Coronal / Sagittal / 3D), each cell
    // double-click-maximizable.
    QGridLayout* quadLayout_ = nullptr;
    QWidget* quadCells_[4] = {nullptr, nullptr, nullptr, nullptr};
    int maximized_ = -1;
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
    RangeSlider* threshSlider_ = nullptr;
    QLabel* threshLabel_ = nullptr;
    QTimer* threshTimer_ = nullptr;
    QSlider* toleranceSlider_ = nullptr;
    QLabel* toleranceLabel_ = nullptr;
    QSlider* brushSlider_ = nullptr;
    QLabel* brushLabel_ = nullptr;
    QSlider* seedItersSlider_ = nullptr;
    QLabel* seedItersLabel_ = nullptr;
    QLabel* seedGateLabel_ = nullptr;
    QPushButton* growSeedsBtn_ = nullptr;
    QSpinBox* removeSmallSpin_ = nullptr;
    QPushButton* undoBtn_ = nullptr;
    QPushButton* redoBtn_ = nullptr;
    QCheckBox* overlayCheck_ = nullptr;
    QLabel* totalVoxelsLabel_ = nullptr;

    // 3D controls.
    QSpinBox* smoothingSpin_ = nullptr;
    QComboBox* resolutionCombo_ = nullptr;
    QPushButton* generateBtn_ = nullptr;
    QLabel* meshInfoLabel_ = nullptr;
    QCheckBox* scissorModeCheck_ = nullptr;
    QComboBox* scissorEraseCombo_ = nullptr;
    QCheckBox* scissorActiveOnlyCheck_ = nullptr;

    // Export controls.
    QWidget* exportSegContainer_ = nullptr;
    QVBoxLayout* exportSegLayout_ = nullptr;
    QHash<int, QCheckBox*> exportSegChecks_;
    QCheckBox* oneFilePerSegCheck_ = nullptr;
    QPushButton* exportStlBtn_ = nullptr;
    QPushButton* exportPngBtn_ = nullptr;
    QLabel* exportMsgLabel_ = nullptr;

    // Markups (client-side).
    MarkupModel markups_;
    QComboBox* markupKindCombo_ = nullptr;
    QList<QToolButton*> markupPaletteBtns_;
    QCheckBox* markupPlaceCheck_ = nullptr;
    QLabel* markupPendingLabel_ = nullptr;
    QPushButton* markupCancelBtn_ = nullptr;
    QWidget* markupListContainer_ = nullptr;
    QVBoxLayout* markupListLayout_ = nullptr;

    // Off-thread folder load.
    QFutureWatcher<LoadResult> loadWatcher_;
    bool loading_ = false;

    // Mesh generation (off-thread, per-segment colored surfaces).
    QFutureWatcher<int> meshWatcher_;
    bool generating_ = false;
    QElapsedTimer paintRefreshClock_;
    bool brushStrokeActive_ = false;
    std::vector<int> pendingSegIds_;
    int pendingSegIndex_ = 0;
    std::vector<MeshView::MeshPiece> meshPieces_;
};

}  // namespace lumenwin
