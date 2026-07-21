#include "MainWindow.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStringList>
#include <QTableWidget>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>
#include <algorithm>
#include <vector>

#include "MeshView.h"
#include "SliceView.h"

namespace lumenwin {

namespace {
constexpr int kPanelWidth = 340;

// Distinct default segment colours, cycled as segments are added.
const QColor kPalette[] = {
    QColor(255, 96, 96),  QColor(96, 200, 120), QColor(96, 160, 255),
    QColor(240, 200, 80), QColor(200, 120, 255), QColor(80, 220, 220),
    QColor(255, 150, 90), QColor(160, 220, 120),
};

QGroupBox* section(const QString& title) {
    auto* box = new QGroupBox(title);
    auto* v = new QVBoxLayout(box);
    v->setSpacing(6);
    return box;
}
QVBoxLayout* body(QGroupBox* box) {
    return qobject_cast<QVBoxLayout*>(box->layout());
}
}  // namespace

MainWindow::MainWindow() {
    setWindowTitle("LumenSlice");
    setAcceptDrops(true);
    resize(1360, 820);

    // Menu.
    auto* fileMenu = menuBar()->addMenu("&File");
    fileMenu->addAction("&Open DICOM Folder…", QKeySequence::Open, this,
                        &MainWindow::openFolder);
    fileMenu->addSeparator();
    fileMenu->addAction("E&xit", this, &QWidget::close);
    auto* editMenu = menuBar()->addMenu("&Edit");
    editMenu->addAction("&Undo", QKeySequence::Undo, this, &MainWindow::undo);
    editMenu->addAction("&Redo", QKeySequence::Redo, this, &MainWindow::redo);

    // Central layout: [icon rail][control panel][canvas].
    auto* rootLayout = new QHBoxLayout;
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);
    rootLayout->addWidget(buildTabRail());

    panels_ = new QStackedWidget;
    panels_->setFixedWidth(kPanelWidth);
    panels_->addWidget(buildVisualizePanel());
    panels_->addWidget(buildSegmentPanel());
    panels_->addWidget(buildThreeDPanel());
    panels_->addWidget(buildExportPanel());
    rootLayout->addWidget(panels_);

    central_ = new QStackedWidget;
    central_->addWidget(buildSliceBoard());
    meshView_ = new MeshView;
    central_->addWidget(meshView_);
    rootLayout->addWidget(central_, 1);

    auto* rootWidget = new QWidget;
    rootWidget->setLayout(rootLayout);
    setCentralWidget(rootWidget);

    connect(&meshWatcher_, &QFutureWatcher<int>::finished, this,
            &MainWindow::onMeshReady);

    selectTab(0);
    refreshAll();
    setStatus("Open a DICOM folder to begin.");
}

// ---------------------------------------------------------------------------
// Tab rail
// ---------------------------------------------------------------------------
QWidget* MainWindow::buildTabRail() {
    auto* rail = new QWidget;
    rail->setFixedWidth(64);
    rail->setStyleSheet("background:#1b1d23;");
    auto* v = new QVBoxLayout(rail);
    v->setContentsMargins(8, 12, 8, 12);
    v->setSpacing(8);

    const char* labels[] = {"View", "Seg", "3D", "Save"};
    auto* group = new QButtonGroup(this);
    for (int i = 0; i < 4; ++i) {
        auto* b = new QToolButton;
        b->setText(labels[i]);
        b->setCheckable(true);
        b->setFixedSize(48, 48);
        b->setStyleSheet(
            "QToolButton{color:#cfd2da;border:none;border-radius:8px;}"
            "QToolButton:checked{background:#3a6df0;color:white;}");
        group->addButton(b, i);
        v->addWidget(b);
    }
    v->addStretch();
    group->button(0)->setChecked(true);
    connect(group, &QButtonGroup::idClicked, this, &MainWindow::selectTab);
    return rail;
}

// ---------------------------------------------------------------------------
// Visualize panel
// ---------------------------------------------------------------------------
QWidget* MainWindow::buildVisualizePanel() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->setSpacing(10);

    auto* openBtn = new QPushButton("Open DICOM Folder…");
    connect(openBtn, &QPushButton::clicked, this, &MainWindow::openFolder);
    v->addWidget(openBtn);

    auto* statusBox = section("Status");
    statusLabel_ = new QLabel("Open a DICOM folder to begin.");
    statusLabel_->setWordWrap(true);
    body(statusBox)->addWidget(statusLabel_);
    v->addWidget(statusBox);

    auto* volBox = section("Volume");
    dimsLabel_ = new QLabel("—");
    spacingLabel_ = new QLabel("—");
    huLabel_ = new QLabel("—");
    auto* volForm = new QFormLayout;
    volForm->addRow("Dimensions", dimsLabel_);
    volForm->addRow("Spacing", spacingLabel_);
    volForm->addRow("HU range", huLabel_);
    body(volBox)->addLayout(volForm);
    v->addWidget(volBox);

    auto* metaBox = section("Patient / Study");
    patientLabel_ = new QLabel("—");
    patientLabel_->setWordWrap(true);
    body(metaBox)->addWidget(patientLabel_);
    auto* inspectBtn = new QPushButton("Inspect all metadata…");
    connect(inspectBtn, &QPushButton::clicked, this,
            &MainWindow::showMetadataInspector);
    body(metaBox)->addWidget(inspectBtn);
    v->addWidget(metaBox);

    auto* wlBox = section("Window / Level (HU)");
    body(wlBox)->addWidget(
        new QLabel("Drag on a slice to adjust, or set exact values here."));
    levelSpin_ = new QDoubleSpinBox;
    levelSpin_->setRange(-4000, 4000);
    levelSpin_->setDecimals(0);
    windowSpin_ = new QDoubleSpinBox;
    windowSpin_->setRange(1, 8000);
    windowSpin_->setDecimals(0);
    levelSlider_ = new QSlider(Qt::Horizontal);
    levelSlider_->setRange(-4000, 4000);
    windowSlider_ = new QSlider(Qt::Horizontal);
    windowSlider_->setRange(1, 8000);

    auto* levelRow = new QHBoxLayout;
    levelRow->addWidget(new QLabel("Level"));
    levelRow->addWidget(levelSpin_);
    body(wlBox)->addLayout(levelRow);
    body(wlBox)->addWidget(levelSlider_);
    auto* windowRow = new QHBoxLayout;
    windowRow->addWidget(new QLabel("Window"));
    windowRow->addWidget(windowSpin_);
    body(wlBox)->addLayout(windowRow);
    body(wlBox)->addWidget(windowSlider_);

    auto setWL = [this](float lvl, float win) {
        st_.level = std::clamp(lvl, -4000.0f, 4000.0f);
        st_.window = std::clamp(win, 1.0f, 8000.0f);
        updateWlControls();
        refreshCanvas();
    };
    connect(levelSpin_, &QDoubleSpinBox::valueChanged, this,
            [this, setWL](double d) { setWL(float(d), st_.window); });
    connect(windowSpin_, &QDoubleSpinBox::valueChanged, this,
            [this, setWL](double d) { setWL(st_.level, float(d)); });
    connect(levelSlider_, &QSlider::valueChanged, this,
            [this, setWL](int i) { setWL(float(i), st_.window); });
    connect(windowSlider_, &QSlider::valueChanged, this,
            [this, setWL](int i) { setWL(st_.level, float(i)); });

    auto* presets = new QHBoxLayout;
    struct P { const char* name; float l, w; };
    for (P p : {P{"Bone", 400, 1500}, P{"Soft", 40, 400}, P{"Lung", -600, 1500}}) {
        auto* b = new QPushButton(p.name);
        connect(b, &QPushButton::clicked, this,
                [setWL, p] { setWL(p.l, p.w); });
        presets->addWidget(b);
    }
    body(wlBox)->addLayout(presets);
    v->addWidget(wlBox);

    auto* ovBox = section("Overlays");
    crosshairCheck_ = new QCheckBox("Crosshair lines");
    crosshairCheck_->setChecked(true);
    connect(crosshairCheck_, &QCheckBox::toggled, this, [this](bool on) {
        st_.showCrosshair = on;
        refreshCanvas();
    });
    body(ovBox)->addWidget(crosshairCheck_);
    v->addWidget(ovBox);

    v->addStretch();
    scroll->setWidget(page);
    return scroll;
}

// ---------------------------------------------------------------------------
// Segment panel
// ---------------------------------------------------------------------------
QWidget* MainWindow::buildSegmentPanel() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->setSpacing(10);

    // Segment list.
    auto* segBox = section("Segments");
    auto* addBtn = new QPushButton("+ Add segment");
    connect(addBtn, &QPushButton::clicked, this, &MainWindow::addSegment);
    body(segBox)->addWidget(addBtn);
    segListContainer_ = new QWidget;
    segListLayout_ = new QVBoxLayout(segListContainer_);
    segListLayout_->setContentsMargins(0, 0, 0, 0);
    segListLayout_->setSpacing(4);
    body(segBox)->addWidget(segListContainer_);
    v->addWidget(segBox);

    // Tool selector.
    auto* toolBox = section("Tool");
    toolCombo_ = new QComboBox;
    toolCombo_->addItem("Threshold", int(Tool::Threshold));
    toolCombo_->addItem("Fill (region grow)", int(Tool::RegionGrow));
    toolCombo_->addItem("Level trace", int(Tool::LevelTrace));
    toolCombo_->addItem("Paint", int(Tool::Paint));
    toolCombo_->addItem("Erase", int(Tool::Erase));
    body(toolBox)->addWidget(toolCombo_);
    v->addWidget(toolBox);

    // Tool detail (stacked).
    toolDetail_ = new QStackedWidget;
    // 0: threshold
    {
        auto* w = new QWidget;
        auto* f = new QVBoxLayout(w);
        f->addWidget(new QLabel("Label every voxel in this HU window into the "
                                "active segment."));
        threshLoSpin_ = new QDoubleSpinBox;
        threshLoSpin_->setRange(-4000, 4000);
        threshLoSpin_->setDecimals(0);
        threshLoSpin_->setValue(300);
        threshHiSpin_ = new QDoubleSpinBox;
        threshHiSpin_->setRange(-4000, 4000);
        threshHiSpin_->setDecimals(0);
        threshHiSpin_->setValue(3000);
        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel("Low"));
        row->addWidget(threshLoSpin_);
        row->addWidget(new QLabel("High"));
        row->addWidget(threshHiSpin_);
        f->addLayout(row);
        auto* presets = new QHBoxLayout;
        struct T { const char* n; float lo, hi; };
        for (T t : {T{"Bone", 300, 3000}, T{"Soft", 40, 80},
                    T{"Lung", -900, -400}}) {
            auto* b = new QPushButton(t.n);
            connect(b, &QPushButton::clicked, this, [this, t] {
                threshLoSpin_->setValue(t.lo);
                threshHiSpin_->setValue(t.hi);
            });
            presets->addWidget(b);
        }
        f->addLayout(presets);
        auto* applyBtn = new QPushButton("Apply threshold");
        connect(applyBtn, &QPushButton::clicked, this,
                &MainWindow::applyThreshold);
        f->addWidget(applyBtn);
        auto* otsuBtn = new QPushButton("Otsu auto-threshold");
        connect(otsuBtn, &QPushButton::clicked, this, &MainWindow::applyOtsu);
        f->addWidget(otsuBtn);
        toolDetail_->addWidget(w);
    }
    // 1: region grow
    {
        auto* w = new QWidget;
        auto* f = new QVBoxLayout(w);
        f->addWidget(new QLabel("Click a structure to flood-fill connected "
                                "voxels within tolerance."));
        toleranceLabel_ = new QLabel("Tolerance: ± 100 HU");
        toleranceSlider_ = new QSlider(Qt::Horizontal);
        toleranceSlider_->setRange(1, 1000);
        toleranceSlider_->setValue(100);
        connect(toleranceSlider_, &QSlider::valueChanged, this, [this](int val) {
            st_.tolerance = float(val);
            toleranceLabel_->setText(QString("Tolerance: ± %1 HU").arg(val));
        });
        f->addWidget(toleranceLabel_);
        f->addWidget(toleranceSlider_);
        toolDetail_->addWidget(w);
    }
    // 2: level trace
    {
        auto* w = new QWidget;
        auto* f = new QVBoxLayout(w);
        f->addWidget(new QLabel("Click a bright structure to add every connected "
                                "pixel at or above the clicked HU (this slice)."));
        toolDetail_->addWidget(w);
    }
    // 3: brush (paint / erase share it)
    {
        auto* w = new QWidget;
        auto* f = new QVBoxLayout(w);
        f->addWidget(new QLabel("Drag over the slice to paint/erase the active "
                                "segment."));
        brushLabel_ = new QLabel("Brush radius: 12 px");
        brushSlider_ = new QSlider(Qt::Horizontal);
        brushSlider_->setRange(1, 80);
        brushSlider_->setValue(12);
        connect(brushSlider_, &QSlider::valueChanged, this, [this](int val) {
            st_.brushRadius = val;
            brushLabel_->setText(QString("Brush radius: %1 px").arg(val));
        });
        f->addWidget(brushLabel_);
        f->addWidget(brushSlider_);
        toolDetail_->addWidget(w);
    }
    v->addWidget(toolDetail_);

    connect(toolCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        st_.tool = Tool(toolCombo_->currentData().toInt());
        switch (st_.tool) {
            case Tool::Threshold: toolDetail_->setCurrentIndex(0); break;
            case Tool::RegionGrow: toolDetail_->setCurrentIndex(1); break;
            case Tool::LevelTrace: toolDetail_->setCurrentIndex(2); break;
            default: toolDetail_->setCurrentIndex(3); break;  // paint/erase
        }
    });
    st_.tool = Tool::Threshold;

    // Refine.
    auto* refineBox = section("Refine");
    auto* refineRow = new QHBoxLayout;
    auto* shrinkBtn = new QPushButton("Shrink");
    auto* growBtn = new QPushButton("Grow");
    connect(shrinkBtn, &QPushButton::clicked, this, &MainWindow::refineShrink);
    connect(growBtn, &QPushButton::clicked, this, &MainWindow::refineGrow);
    refineRow->addWidget(shrinkBtn);
    refineRow->addWidget(growBtn);
    body(refineBox)->addLayout(refineRow);
    auto* smoothBtn = new QPushButton("Smooth edges");
    connect(smoothBtn, &QPushButton::clicked, this, &MainWindow::refineSmooth);
    body(refineBox)->addWidget(smoothBtn);
    v->addWidget(refineBox);

    // Grow from seeds.
    auto* seedsBox = section("Grow from seeds");
    body(seedsBox)->addWidget(new QLabel(
        "Paint a seed in two or more segments, then Grow."));
    seedItersLabel_ = new QLabel("Iterations: 25");
    seedItersSlider_ = new QSlider(Qt::Horizontal);
    seedItersSlider_->setRange(5, 100);
    seedItersSlider_->setValue(25);
    connect(seedItersSlider_, &QSlider::valueChanged, this, [this](int val) {
        seedItersLabel_->setText(QString("Iterations: %1").arg(val));
    });
    body(seedsBox)->addWidget(seedItersLabel_);
    body(seedsBox)->addWidget(seedItersSlider_);
    seedGateLabel_ = new QLabel("Seed at least two segments (0/2 seeded).");
    body(seedsBox)->addWidget(seedGateLabel_);
    growSeedsBtn_ = new QPushButton("Grow from seeds");
    connect(growSeedsBtn_, &QPushButton::clicked, this,
            &MainWindow::growFromSeeds);
    body(seedsBox)->addWidget(growSeedsBtn_);
    v->addWidget(seedsBox);

    // Edit.
    auto* editBox = section("Edit");
    auto* editRow = new QHBoxLayout;
    undoBtn_ = new QPushButton("Undo");
    redoBtn_ = new QPushButton("Redo");
    connect(undoBtn_, &QPushButton::clicked, this, &MainWindow::undo);
    connect(redoBtn_, &QPushButton::clicked, this, &MainWindow::redo);
    editRow->addWidget(undoBtn_);
    editRow->addWidget(redoBtn_);
    body(editBox)->addLayout(editRow);
    overlayCheck_ = new QCheckBox("Show overlay");
    overlayCheck_->setChecked(true);
    connect(overlayCheck_, &QCheckBox::toggled, this, [this](bool on) {
        st_.showOverlay = on;
        refreshCanvas();
    });
    body(editBox)->addWidget(overlayCheck_);
    totalVoxelsLabel_ = new QLabel("Total voxels: 0");
    body(editBox)->addWidget(totalVoxelsLabel_);
    auto* clearBtn = new QPushButton("Clear active segment");
    connect(clearBtn, &QPushButton::clicked, this, &MainWindow::clearActive);
    body(editBox)->addWidget(clearBtn);
    v->addWidget(editBox);

    v->addStretch();
    scroll->setWidget(page);
    return scroll;
}

// ---------------------------------------------------------------------------
// 3D panel
// ---------------------------------------------------------------------------
QWidget* MainWindow::buildThreeDPanel() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->setSpacing(10);

    v->addWidget(new QLabel("Build a 3D surface from the segmentation mask using "
                            "marching cubes."));

    auto* qualBox = section("Quality");
    auto* smoothRow = new QHBoxLayout;
    smoothRow->addWidget(new QLabel("Smoothing"));
    smoothingSpin_ = new QSpinBox;
    smoothingSpin_->setRange(0, 5);
    smoothingSpin_->setValue(1);
    smoothRow->addWidget(smoothingSpin_);
    body(qualBox)->addLayout(smoothRow);
    auto* resRow = new QHBoxLayout;
    resRow->addWidget(new QLabel("Resolution"));
    resolutionCombo_ = new QComboBox;
    resolutionCombo_->addItem("Full", 1);
    resolutionCombo_->addItem("Half", 2);
    resolutionCombo_->addItem("Third", 3);
    resRow->addWidget(resolutionCombo_);
    body(qualBox)->addLayout(resRow);
    v->addWidget(qualBox);

    generateBtn_ = new QPushButton("Generate / Update 3D");
    connect(generateBtn_, &QPushButton::clicked, this,
            &MainWindow::generateMesh);
    v->addWidget(generateBtn_);

    auto* meshBox = section("Mesh");
    meshInfoLabel_ = new QLabel("No surface yet.");
    meshInfoLabel_->setWordWrap(true);
    body(meshBox)->addWidget(meshInfoLabel_);
    body(meshBox)->addWidget(new QLabel("Drag to orbit, scroll to zoom."));
    v->addWidget(meshBox);

    v->addStretch();
    scroll->setWidget(page);
    return scroll;
}

// ---------------------------------------------------------------------------
// Export panel
// ---------------------------------------------------------------------------
QWidget* MainWindow::buildExportPanel() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->setSpacing(10);

    auto* meshBox = section("3D mesh");
    exportStlBtn_ = new QPushButton("Export STL…");
    connect(exportStlBtn_, &QPushButton::clicked, this, &MainWindow::exportStl);
    body(meshBox)->addWidget(exportStlBtn_);
    v->addWidget(meshBox);

    auto* sliceBox = section("Slice");
    exportPngBtn_ = new QPushButton("Export axial PNG…");
    connect(exportPngBtn_, &QPushButton::clicked, this, &MainWindow::exportPng);
    body(sliceBox)->addWidget(exportPngBtn_);
    v->addWidget(sliceBox);

    exportMsgLabel_ = new QLabel;
    exportMsgLabel_->setWordWrap(true);
    v->addWidget(exportMsgLabel_);

    v->addStretch();
    scroll->setWidget(page);
    return scroll;
}

// ---------------------------------------------------------------------------
// Slice board (3 panes + sliders)
// ---------------------------------------------------------------------------
QWidget* MainWindow::buildSliceBoard() {
    auto* board = new QWidget;
    auto* h = new QHBoxLayout(board);
    h->setContentsMargins(6, 6, 6, 6);
    h->setSpacing(6);
    const int axes[3] = {LUMEN_AXIS_AXIAL, LUMEN_AXIS_CORONAL,
                         LUMEN_AXIS_SAGITTAL};
    for (int i = 0; i < 3; ++i) {
        auto* col = new QVBoxLayout;
        panes_[i] = new SliceView(axes[i], &st_);
        connect(panes_[i], &SliceView::sliceScrolled, this,
                &MainWindow::onSliceScrolled);
        connect(panes_[i], &SliceView::windowLevelDragged, this,
                &MainWindow::onWindowLevelDragged);
        connect(panes_[i], &SliceView::focusPicked, this,
                &MainWindow::onFocusPicked);
        connect(panes_[i], &SliceView::paintStroke, this,
                &MainWindow::onPaintStroke);
        connect(panes_[i], &SliceView::floodClicked, this,
                &MainWindow::onFloodClicked);
        connect(panes_[i], &SliceView::levelTraceClicked, this,
                &MainWindow::onLevelTraceClicked);
        connect(panes_[i], &SliceView::strokeBegan, this,
                &MainWindow::onStrokeBegan);
        col->addWidget(panes_[i], 1);
        sliders_[i] = new QSlider(Qt::Horizontal);
        const int axis = axes[i];
        connect(sliders_[i], &QSlider::valueChanged, this,
                [this, axis](int val) { onSliceScrolled(axis, val); });
        col->addWidget(sliders_[i]);
        h->addLayout(col, 1);
    }
    return board;
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------
void MainWindow::openFolder() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, "Select a DICOM folder", QDir::homePath());
    if (!dir.isEmpty()) loadPath(dir);
}

void MainWindow::loadPath(const QString& path) {
    std::string status;
    if (!vol_.load(path.toStdString(), status)) {
        setStatus(QString("Could not load: %1")
                      .arg(QString::fromStdString(status)));
        QMessageBox::warning(this, "LumenSlice",
                             QString::fromStdString(status).isEmpty()
                                 ? "Could not load the DICOM folder."
                                 : QString::fromStdString(status));
        return;
    }
    st_.volume = vol_.get();
    LumenVolume* v = st_.volume;

    int w = 0, h = 0, d = 0;
    lumen_dims(v, &w, &h, &d);
    st_.focus[0] = w / 2;
    st_.focus[1] = h / 2;
    st_.focus[2] = d / 2;
    for (int axis = 0; axis < 3; ++axis)
        st_.sliceIndex[axis] = lumen_slice_count(v, axis) / 2;

    // Metadata JSON (two-call: size, then fill).
    metaJson_.clear();
    const int need = lumen_meta_json(v, nullptr, 0);
    if (need > 0) {
        std::vector<char> buf(size_t(need) + 1, 0);
        lumen_meta_json(v, buf.data(), int(buf.size()));
        metaJson_ = QString::fromUtf8(buf.data());
    }

    segNames_.clear();
    const int segCount = lumen_seg_segment_count(v);
    for (int i = 0; i < segCount; ++i) {
        const int id = lumen_seg_segment_id_at(v, i);
        segNames_[id] = QString("Segment %1").arg(id);
    }

    setStatus(QString::fromStdString(status));
    refreshAll();
    meshView_->setMesh(nullptr);
    meshInfoLabel_->setText("No surface yet.");
}

void MainWindow::selectTab(int tab) {
    currentTab_ = tab;
    panels_->setCurrentIndex(tab);
    // Segment tab enables canvas tool interactions; others keep left-drag = W/L.
    st_.segmentInteractive = (tab == 1);
    central_->setCurrentIndex(tab == 2 ? 1 : 0);
    refreshCanvas();
}

void MainWindow::onSliceScrolled(int axis, int index) {
    st_.sliceIndex[axis] = index;
    if (sliders_[0]) {
        const int col = axis;  // axes are stored 0..2 in order
        QSignalBlocker b(sliders_[col]);
        sliders_[col]->setValue(index);
    }
    refreshCanvas();
}

void MainWindow::onWindowLevelDragged(float dLevel, float dWindow) {
    st_.level = std::clamp(st_.level + dLevel, -4000.0f, 4000.0f);
    st_.window = std::clamp(st_.window + dWindow, 1.0f, 8000.0f);
    updateWlControls();
    refreshCanvas();
}

void MainWindow::onFocusPicked(int x, int y, int z) {
    st_.focus[0] = x;
    st_.focus[1] = y;
    st_.focus[2] = z;
    // Link the panes: scroll each axis to the slice through the focus voxel.
    LumenVolume* v = st_.volume;
    if (v) {
        const int idx[3] = {z, y, x};  // axial=z, coronal=y, sagittal=x
        for (int axis = 0; axis < 3; ++axis) {
            const int count = lumen_slice_count(v, axis);
            st_.sliceIndex[axis] = std::clamp(idx[axis], 0, count - 1);
        }
        refreshSliders();
    }
    refreshCanvas();
}

void MainWindow::onStrokeBegan() {
    if (!st_.volume) return;
    lumen_seg_push_undo(st_.volume);
    updateUndoRedo();
}

void MainWindow::onPaintStroke(int axis, int index, int cx, int cy, int radius,
                               bool add) {
    if (!st_.volume) return;
    lumen_seg_paint(st_.volume, axis, index, cx, cy, radius, add ? 1 : 0);
    refreshCanvas();
    updateSegmentCounts();
}

void MainWindow::onFloodClicked(int x, int y, int z) {
    if (!st_.volume) return;
    lumen_seg_region_grow(st_.volume, x, y, z, st_.tolerance);
    refreshCanvas();
    updateSegmentCounts();
}

void MainWindow::onLevelTraceClicked(int axis, int index, int cx, int cy) {
    if (!st_.volume) return;
    lumen_seg_level_trace(st_.volume, axis, index, cx, cy);
    refreshCanvas();
    updateSegmentCounts();
}

QColor MainWindow::nextSegmentColor() const {
    const int n = st_.volume ? lumen_seg_segment_count(st_.volume) : 0;
    return kPalette[n % int(sizeof(kPalette) / sizeof(kPalette[0]))];
}

void MainWindow::addSegment() {
    LumenVolume* v = st_.volume;
    if (!v) return;
    const QColor c = nextSegmentColor();
    const int id = lumen_seg_add(v, c.red(), c.green(), c.blue());
    if (id == 0) return;
    segNames_[id] = QString("Segment %1").arg(id);
    rebuildSegmentList();
    updateSegmentCounts();
    refreshCanvas();
}

void MainWindow::applyThreshold() {
    LumenVolume* v = st_.volume;
    if (!v || lumen_seg_active(v) == 0) return;
    lumen_seg_push_undo(v);
    lumen_seg_threshold(v, float(threshLoSpin_->value()),
                        float(threshHiSpin_->value()));
    updateUndoRedo();
    refreshCanvas();
    updateSegmentCounts();
}

void MainWindow::applyOtsu() {
    LumenVolume* v = st_.volume;
    if (!v) return;
    const float t = lumen_seg_otsu(v);
    float lo = 0, hi = 0;
    lumen_hu_range(v, &lo, &hi);
    threshLoSpin_->setValue(t);
    threshHiSpin_->setValue(hi);
    applyThreshold();
}

void MainWindow::refineGrow() {
    LumenVolume* v = st_.volume;
    if (!v || lumen_seg_active(v) == 0) return;
    lumen_seg_push_undo(v);
    lumen_seg_grow(v, 1);
    updateUndoRedo();
    refreshCanvas();
    updateSegmentCounts();
}

void MainWindow::refineShrink() {
    LumenVolume* v = st_.volume;
    if (!v || lumen_seg_active(v) == 0) return;
    lumen_seg_push_undo(v);
    lumen_seg_shrink(v, 1);
    updateUndoRedo();
    refreshCanvas();
    updateSegmentCounts();
}

void MainWindow::refineSmooth() {
    LumenVolume* v = st_.volume;
    if (!v || lumen_seg_active(v) == 0) return;
    lumen_seg_push_undo(v);
    lumen_seg_smooth(v, 1);
    updateUndoRedo();
    refreshCanvas();
    updateSegmentCounts();
}

void MainWindow::growFromSeeds() {
    LumenVolume* v = st_.volume;
    if (!v) return;
    lumen_seg_push_undo(v);
    lumen_seg_grow_from_seeds(v, seedItersSlider_->value());
    updateUndoRedo();
    refreshCanvas();
    updateSegmentCounts();
}

void MainWindow::clearActive() {
    LumenVolume* v = st_.volume;
    if (!v || lumen_seg_active(v) == 0) return;
    lumen_seg_push_undo(v);
    lumen_seg_clear(v);
    updateUndoRedo();
    refreshCanvas();
    updateSegmentCounts();
}

void MainWindow::undo() {
    if (st_.volume && lumen_seg_undo(st_.volume)) {
        refreshCanvas();
        updateSegmentCounts();
        updateUndoRedo();
    }
}

void MainWindow::redo() {
    if (st_.volume && lumen_seg_redo(st_.volume)) {
        refreshCanvas();
        updateSegmentCounts();
        updateUndoRedo();
    }
}

// ---------------------------------------------------------------------------
// 3D + export
// ---------------------------------------------------------------------------
void MainWindow::generateMesh() {
    LumenVolume* v = st_.volume;
    if (!v || generating_) return;
    if (lumen_seg_count(v) == 0) return;
    lumen_mesh_snapshot(v);  // main thread: copy the live mask
    generating_ = true;
    generateBtn_->setEnabled(false);
    generateBtn_->setText("Generating…");
    exportStlBtn_->setEnabled(false);
    const int smooth = smoothingSpin_->value();
    const int down = resolutionCombo_->currentData().toInt();
    // Marching cubes on a background thread (touches only the snapshot + mesh).
    meshWatcher_.setFuture(QtConcurrent::run(
        [v, smooth, down] { return lumen_mesh_generate(v, smooth, down); }));
}

void MainWindow::onMeshReady() {
    generating_ = false;
    generateBtn_->setText("Generate / Update 3D");
    LumenVolume* v = st_.volume;
    const int tris = meshWatcher_.result();
    const int verts = v ? lumen_mesh_vertex_count(v) : 0;
    meshView_->setMesh(v);
    meshInfoLabel_->setText(
        QString("Triangles: %1\nVertices: %2").arg(tris).arg(verts));
    generateBtn_->setEnabled(v && lumen_seg_count(v) > 0);
    exportStlBtn_->setEnabled(tris > 0);
}

void MainWindow::exportStl() {
    LumenVolume* v = st_.volume;
    if (!v || lumen_mesh_index_count(v) == 0) return;
    const QString path = QFileDialog::getSaveFileName(
        this, "Export STL", QDir::homePath() + "/LumenSlice.stl",
        "STL mesh (*.stl)");
    if (path.isEmpty()) return;
    const int rc = lumen_mesh_write_stl(v, path.toUtf8().constData());
    exportMsgLabel_->setText(rc == 0
                                 ? QString("Saved %1").arg(QFileInfo(path).fileName())
                                 : "STL export failed.");
}

void MainWindow::exportPng() {
    LumenVolume* v = st_.volume;
    if (!v) return;
    int w = 0, h = 0;
    const unsigned char* px = lumen_extract_slice(
        v, LUMEN_AXIS_AXIAL, st_.sliceIndex[0], st_.level, st_.window, &w, &h);
    if (!px || w <= 0 || h <= 0) return;
    QImage img(reinterpret_cast<const uchar*>(px), w, h, w * 4,
               QImage::Format_RGBA8888);
    const QString path = QFileDialog::getSaveFileName(
        this, "Export axial PNG", QDir::homePath() + "/axial.png",
        "PNG image (*.png)");
    if (path.isEmpty()) return;
    exportMsgLabel_->setText(img.copy().save(path, "PNG")
                                 ? QString("Saved %1").arg(QFileInfo(path).fileName())
                                 : "PNG export failed.");
}

// ---------------------------------------------------------------------------
// Refresh helpers
// ---------------------------------------------------------------------------
void MainWindow::refreshAll() {
    const bool has = st_.hasVolume();
    refreshSliders();
    refreshVolumeInfo();
    rebuildSegmentList();
    updateSegmentCounts();
    updateUndoRedo();
    updateWlControls();
    if (exportPngBtn_) exportPngBtn_->setEnabled(has);
    refreshCanvas();
}

void MainWindow::refreshCanvas() {
    for (auto* p : panes_)
        if (p) p->update();
}

void MainWindow::refreshSliders() {
    LumenVolume* v = st_.volume;
    for (int i = 0; i < 3; ++i) {
        if (!sliders_[i]) continue;
        QSignalBlocker b(sliders_[i]);
        const int count = v ? lumen_slice_count(v, i) : 1;
        sliders_[i]->setRange(0, std::max(0, count - 1));
        sliders_[i]->setValue(st_.sliceIndex[i]);
        sliders_[i]->setEnabled(v != nullptr);
    }
}

void MainWindow::refreshVolumeInfo() {
    LumenVolume* v = st_.volume;
    if (!v) {
        dimsLabel_->setText("—");
        spacingLabel_->setText("—");
        huLabel_->setText("—");
        patientLabel_->setText("—");
        return;
    }
    int w = 0, h = 0, d = 0;
    lumen_dims(v, &w, &h, &d);
    dimsLabel_->setText(QString("%1 × %2 × %3").arg(w).arg(h).arg(d));
    float sx = 0, sy = 0, sz = 0;
    lumen_spacing(v, &sx, &sy, &sz);
    spacingLabel_->setText(QString("%1 / %2 / %3 mm")
                               .arg(sx, 0, 'f', 2)
                               .arg(sy, 0, 'f', 2)
                               .arg(sz, 0, 'f', 2));
    float lo = 0, hi = 0;
    lumen_hu_range(v, &lo, &hi);
    huLabel_->setText(QString("%1 … %2").arg(lo, 0, 'f', 0).arg(hi, 0, 'f', 0));

    // Curated patient/study summary from the meta JSON.
    QStringList lines;
    const QJsonDocument doc = QJsonDocument::fromJson(metaJson_.toUtf8());
    if (doc.isObject()) {
        const QJsonObject meta = doc.object().value("meta").toObject();
        auto add = [&](const char* label, const char* key) {
            const QString val = meta.value(key).toString();
            if (!val.isEmpty()) lines << QString("%1: %2").arg(label, val);
        };
        add("Patient", "patient_name");
        add("ID", "patient_id");
        add("Modality", "modality");
        add("Study date", "study_date");
        add("Study", "study_description");
    }
    patientLabel_->setText(lines.isEmpty() ? "No metadata." : lines.join("\n"));
}

void MainWindow::rebuildSegmentList() {
    // Tear down existing rows.
    QLayoutItem* item = nullptr;
    while ((item = segListLayout_->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    segCountLabels_.clear();

    LumenVolume* v = st_.volume;
    if (!v) return;
    const int count = lumen_seg_segment_count(v);
    const int active = lumen_seg_active(v);
    for (int i = 0; i < count; ++i) {
        const int id = lumen_seg_segment_id_at(v, i);
        auto* row = new QWidget;
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(2, 2, 2, 2);
        h->setSpacing(4);

        // Visibility.
        auto* vis = new QCheckBox;
        vis->setChecked(lumen_seg_get_visible(v, id) != 0);
        connect(vis, &QCheckBox::toggled, this, [this, id](bool on) {
            lumen_seg_set_visible(st_.volume, id, on ? 1 : 0);
            refreshCanvas();
        });
        h->addWidget(vis);

        // Colour swatch.
        int r = 0, g = 0, b = 0;
        lumen_seg_get_color(v, id, &r, &g, &b);
        auto* swatch = new QToolButton;
        swatch->setFixedSize(20, 20);
        swatch->setStyleSheet(
            QString("background:rgb(%1,%2,%3);border:1px solid #555;")
                .arg(r).arg(g).arg(b));
        connect(swatch, &QToolButton::clicked, this, [this, id, swatch] {
            int cr = 0, cg = 0, cb = 0;
            lumen_seg_get_color(st_.volume, id, &cr, &cg, &cb);
            const QColor picked = QColorDialog::getColor(
                QColor(cr, cg, cb), this, "Segment colour");
            if (picked.isValid()) {
                lumen_seg_set_color(st_.volume, id, picked.red(), picked.green(),
                                    picked.blue());
                swatch->setStyleSheet(
                    QString("background:rgb(%1,%2,%3);border:1px solid #555;")
                        .arg(picked.red()).arg(picked.green()).arg(picked.blue()));
                refreshCanvas();
            }
        });
        h->addWidget(swatch);

        // Name (UI-only; the core has no segment-name concept).
        auto* name = new QLineEdit(segNames_.value(id, QString("Segment %1").arg(id)));
        connect(name, &QLineEdit::editingFinished, this,
                [this, id, name] { segNames_[id] = name->text(); });
        h->addWidget(name, 1);

        // Live voxel count.
        auto* cnt = new QLabel("0");
        cnt->setMinimumWidth(48);
        cnt->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        segCountLabels_[id] = cnt;
        h->addWidget(cnt);

        // Active toggle.
        auto* activeBtn = new QToolButton;
        activeBtn->setText("●");
        activeBtn->setCheckable(true);
        activeBtn->setChecked(id == active);
        activeBtn->setToolTip("Set active");
        connect(activeBtn, &QToolButton::clicked, this, [this, id] {
            lumen_seg_set_active(st_.volume, id);
            rebuildSegmentList();
            updateSegmentCounts();
        });
        h->addWidget(activeBtn);

        // Delete.
        auto* del = new QToolButton;
        del->setText("✕");
        connect(del, &QToolButton::clicked, this, [this, id] {
            lumen_seg_remove(st_.volume, id);
            segNames_.remove(id);
            rebuildSegmentList();
            updateSegmentCounts();
            refreshCanvas();
        });
        h->addWidget(del);

        segListLayout_->addWidget(row);
    }
}

void MainWindow::updateSegmentCounts() {
    LumenVolume* v = st_.volume;
    if (!v) {
        if (totalVoxelsLabel_) totalVoxelsLabel_->setText("Total voxels: 0");
        return;
    }
    std::vector<long> hist(256, 0);
    lumen_seg_label_histogram(v, hist.data());
    long total = 0;
    int seeded = 0;
    const int count = lumen_seg_segment_count(v);
    for (int i = 0; i < count; ++i) {
        const int id = lumen_seg_segment_id_at(v, i);
        const long n = hist[id];
        total += n;
        if (n > 0) ++seeded;
        if (auto* lbl = segCountLabels_.value(id, nullptr))
            lbl->setText(QString::number(n));
    }
    totalVoxelsLabel_->setText(QString("Total voxels: %1").arg(total));

    if (seedGateLabel_)
        seedGateLabel_->setText(
            QString("Seed at least two segments (%1/2 seeded).").arg(seeded));
    if (growSeedsBtn_) growSeedsBtn_->setEnabled(seeded >= 2 && !generating_);
    if (generateBtn_)
        generateBtn_->setEnabled(total > 0 && !generating_);
    if (exportStlBtn_)
        exportStlBtn_->setEnabled(lumen_mesh_index_count(v) > 0);
}

void MainWindow::updateUndoRedo() {
    LumenVolume* v = st_.volume;
    const bool canU = v && lumen_seg_can_undo(v);
    const bool canR = v && lumen_seg_can_redo(v);
    if (undoBtn_) undoBtn_->setEnabled(canU);
    if (redoBtn_) redoBtn_->setEnabled(canR);
}

void MainWindow::updateWlControls() {
    const QSignalBlocker b1(levelSpin_), b2(windowSpin_), b3(levelSlider_),
        b4(windowSlider_);
    levelSpin_->setValue(st_.level);
    windowSpin_->setValue(st_.window);
    levelSlider_->setValue(int(st_.level));
    windowSlider_->setValue(int(st_.window));
}

void MainWindow::setStatus(const QString& text) {
    if (statusLabel_) statusLabel_->setText(text);
    statusBar()->showMessage(text, 5000);
}

void MainWindow::showMetadataInspector() {
    if (metaJson_.isEmpty()) {
        QMessageBox::information(this, "Metadata", "No metadata available.");
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(metaJson_.toUtf8());
    const QJsonArray tags = doc.object().value("tags").toArray();
    auto* dlg = new QDialog(this);
    dlg->setWindowTitle("DICOM metadata");
    dlg->resize(720, 520);
    auto* layout = new QVBoxLayout(dlg);
    auto* table = new QTableWidget(tags.size(), 4, dlg);
    table->setHorizontalHeaderLabels({"Tag", "VR", "Name", "Value"});
    table->horizontalHeader()->setStretchLastSection(true);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    for (int i = 0; i < tags.size(); ++i) {
        const QJsonObject t = tags[i].toObject();
        table->setItem(i, 0, new QTableWidgetItem(t.value("ge").toString()));
        table->setItem(i, 1, new QTableWidgetItem(t.value("vr").toString()));
        table->setItem(i, 2, new QTableWidgetItem(t.value("name").toString()));
        table->setItem(i, 3, new QTableWidgetItem(t.value("value").toString()));
    }
    table->resizeColumnsToContents();
    layout->addWidget(table);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

// ---------------------------------------------------------------------------
// Drag & drop
// ---------------------------------------------------------------------------
void MainWindow::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls()) {
        for (const QUrl& url : e->mimeData()->urls()) {
            if (QFileInfo(url.toLocalFile()).isDir()) {
                e->acceptProposedAction();
                return;
            }
        }
    }
}

void MainWindow::dropEvent(QDropEvent* e) {
    for (const QUrl& url : e->mimeData()->urls()) {
        const QString local = url.toLocalFile();
        if (QFileInfo(local).isDir()) {
            loadPath(local);
            return;
        }
    }
}

}  // namespace lumenwin
