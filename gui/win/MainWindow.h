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
#include <QTimer>
#include <QString>
#include <functional>
#include <vector>

#include "BridgeVolume.h"
#include "MarkupModel.h"
#include "MeshView.h"
#include "RangeSlider.h"
#include "ViewState.h"

class QButtonGroup;
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
    void resizeEvent(QResizeEvent*) override;

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
    void commitThreshold();
    void useThresholdForMasking();
    void deactivateMask();
    void adjustMask();
    void applyOtsu();
    void growFromSeeds();
    void applyGrowPreview();
    void cancelGrowPreview();
    void undo();
    void redo();
    void clearActive();

    // 3D + export.
    void generateMesh();
    void onMeshReady();
    void onAutoMeshRefresh();
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
    void setThreeDTabLayout(bool dedicated);
    void rebuildMarkupList();
    void updateMarkupPaletteSelection();
    void updateMarkupPending();
    void refreshMarkups();

    // Sequential per-segment colored mesh generation (one snapshot_label ->
    // generate -> readback per visible non-empty segment, worker off the UI thread).
    void startNextMeshSegment();
    void finishMeshGeneration();
    int effectiveDownsample() const;  // coarsen mesh on very large volumes
    // Run a mask-mutating op on a worker thread with a busy overlay (snapshots undo
    // first). Keeps heavy ops (grow-from-seeds) from freezing the UI on
    // large volumes.
    void runMaskOp(const QString& busyText, std::function<void()> op,
                   bool refreshMesh = true, bool captureUndo = true);
    void scheduleMeshRefresh();

    // Refresh helpers.
    void loadPath(const QString& path);
    // Modal series picker for a multi-series folder; returns the chosen index or -1.
    int pickSeries(LumenSeriesScan* scan);
    void adoptLoadedVolume(const LoadResult& result);
    void refreshAll();
    void refreshCanvas();
    void refreshSliders();
    void updateSliceButtons();  // enable/disable the per-pane prev/next buttons
    void refreshVolumeInfo();
    void rebuildSegmentList();
    void rebuildExportSegmentList();
    void updateSegmentCounts();      // debounced: schedules recompute
    void recomputeSegmentCounts();   // the actual full-volume histogram scan
    void updateMaskIndicator();      // reflect the intensity-mask active state
    void updateUndoRedo();
    void updateWlControls();
    void setStatus(const QString& text);
    void showBusy(const QString& text);  // empty text hides the busy overlay
    void positionBusy();
    void positionMaskBadge();  // keep the canvas mask badge centered at the top
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
    // Per-pane step buttons: previous (up) / next (down) slice, beside the slider.
    QToolButton* slicePrevBtns_[3] = {nullptr, nullptr, nullptr};
    QToolButton* sliceNextBtns_[3] = {nullptr, nullptr, nullptr};
    MeshView* meshView_ = nullptr;
    QWidget* quadBoard_ = nullptr;
    QLabel* loadingOverlay_ = nullptr;

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
    RangeSlider* wlRange_ = nullptr;
    QCheckBox* crosshairCheck_ = nullptr;
    // Clinically useful HU band for the Level/Window spin boxes: a fixed range
    // that covers every preset, clamped to the loaded volume's HU span and
    // widened to include the current window. Mirrors macOS VisualizeControls'
    // wlBounds. Refined per volume in refreshVolumeInfo().
    float huBoundLo_ = -1400.0f;
    float huBoundHi_ = 1600.0f;

    // Segment controls.
    QVBoxLayout* segListLayout_ = nullptr;
    QWidget* segListContainer_ = nullptr;
    QHash<int, QLabel*> segCountLabels_;
    QHash<int, QString> segNames_;
    QComboBox* toolCombo_ = nullptr;
    QButtonGroup* toolGroup_ = nullptr;  // 5 tool buttons; index 0 = Threshold
    QStackedWidget* toolDetail_ = nullptr;
    RangeSlider* threshSlider_ = nullptr;
    QLabel* threshLabel_ = nullptr;
    QTimer* threshTimer_ = nullptr;
    // Intensity-mask controls: the "Use as paint mask" button is swapped for an
    // active indicator + Adjust/Turn-off buttons while a mask is in effect, and a
    // canvas badge (maskBadge_) mirrors the state over the slice panes.
    QPushButton* maskBtn_ = nullptr;
    QLabel* maskIndicator_ = nullptr;
    QPushButton* maskAdjustBtn_ = nullptr;
    QPushButton* maskDeactivateBtn_ = nullptr;
    QLabel* maskBadge_ = nullptr;
    QSlider* toleranceSlider_ = nullptr;
    QLabel* toleranceLabel_ = nullptr;
    QSlider* brushSlider_ = nullptr;
    QLabel* brushLabel_ = nullptr;
    QSlider* seedLocalitySlider_ = nullptr;
    QLabel* seedLocalityLabel_ = nullptr;
    QLabel* seedGateLabel_ = nullptr;
    QPushButton* growSeedsBtn_ = nullptr;
    QPushButton* growApplyBtn_ = nullptr;
    QPushButton* growCancelBtn_ = nullptr;
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
    LoadResult pendingLoad_;
    bool hasPendingLoad_ = false;

    // Mesh generation (off-thread, per-segment colored surfaces).
    QFutureWatcher<int> meshWatcher_;
    bool generating_ = false;
    bool autoMesh3D_ = false;  // opt-in: rebuild surface after each edit
    QTimer meshRefreshTimer_;
    bool meshRefreshPending_ = false;
    QTimer countsTimer_;  // debounces the full-volume segment histogram
    QFutureWatcher<void> heavyWatcher_;  // async grow-from-seeds mask operation
    bool heavyRefreshMesh_ = true;
    bool growPreviewPending_ = false;
    bool growPreviewActive_ = false;
    QTimer scrollThrottle_;  // caps slice re-extraction rate during wheel-scroll
    int lastScrollAxis_ = -1;
    bool scrollDirty_ = false;
    QElapsedTimer paintRefreshClock_;
    bool brushStrokeActive_ = false;
    std::vector<int> pendingSegIds_;
    int pendingSegIndex_ = 0;
    std::vector<MeshView::MeshPiece> meshPieces_;
};

}  // namespace lumenwin
