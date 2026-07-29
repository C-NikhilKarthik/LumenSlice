#include "MainWindow.h"

#include <QApplication>
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
#include <QGridLayout>
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
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>
#include <algorithm>
#include <vector>

#include "MeshView.h"
#include "RangeSlider.h"
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

// Install `page` into a control-panel scroll area. Word-wraps every label and
// forbids the horizontal scrollbar so a long line can never widen the page and
// shift the content sideways under the icon rail.
void finishPanel(QScrollArea* scroll, QWidget* page) {
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    const QList<QLabel*> labels = page->findChildren<QLabel*>();
    for (QLabel* l : labels) l->setWordWrap(true);
    scroll->setWidget(page);
}
QVBoxLayout* body(QGroupBox* box) {
    return qobject_cast<QVBoxLayout*>(box->layout());
}

// Make a segment name safe to use as a filename stem.
QString sanitizeFilename(const QString& name) {
    QString out;
    for (const QChar c : name)
        out += (c.isLetterOrNumber() || c == '-' || c == '_') ? c : QChar('_');
    return out.isEmpty() ? QStringLiteral("segment") : out;
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
    st_.markups = &markups_;
    panels_->addWidget(buildVisualizePanel());
    panels_->addWidget(buildSegmentPanel());
    panels_->addWidget(buildThreeDPanel());
    panels_->addWidget(buildExportPanel());
    panels_->addWidget(buildMarkupPanel());
    rootLayout->addWidget(panels_);

    meshView_ = new MeshView;
    meshView_->setMarkupModel(&markups_);
    rootLayout->addWidget(buildQuad(), 1);

    auto* rootWidget = new QWidget;
    rootWidget->setLayout(rootLayout);
    setCentralWidget(rootWidget);

    connect(&meshWatcher_, &QFutureWatcher<int>::finished, this,
            &MainWindow::onMeshReady);
    connect(&loadWatcher_, &QFutureWatcher<LoadResult>::finished, this,
            &MainWindow::onLoadReady);
    connect(meshView_, &MeshView::scissorFinished, this,
            &MainWindow::onScissorFinished);

    selectTab(0);
    refreshAll();
    setStatus("Open a DICOM folder to begin.");
}

// ---------------------------------------------------------------------------
// Tab rail
// ---------------------------------------------------------------------------
QWidget* MainWindow::buildTabRail() {
    auto* rail = new QWidget;
    rail->setFixedWidth(72);
    rail->setStyleSheet("background:#141519;");
    auto* v = new QVBoxLayout(rail);
    v->setContentsMargins(10, 14, 10, 14);
    v->setSpacing(8);

    struct R { const char* glyph; const char* label; };
    const R items[] = {{"◧", "View"}, {"✎", "Seg"}, {"◈", "3D"},
                       {"⤓", "Save"}, {"⌖", "Mark"}};
    auto* group = new QButtonGroup(this);
    for (int i = 0; i < 5; ++i) {
        auto* b = new QToolButton;
        b->setText(QString("%1\n%2").arg(items[i].glyph, items[i].label));
        b->setCheckable(true);
        b->setFixedSize(52, 52);
        b->setStyleSheet(
            "QToolButton{color:#a7adba;border:none;border-radius:12px;font-size:11px;}"
            "QToolButton:hover{background:#22252d;color:#e7e9ef;}"
            "QToolButton:checked{background:#4f7cf0;color:white;}");
        group->addButton(b, i);
        v->addWidget(b, 0, Qt::AlignHCenter);
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
    openBtn->setObjectName("accent");
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
    auto* orientCheck = new QCheckBox("Orientation labels (R/L/A/P/S/I)");
    orientCheck->setChecked(st_.showOrientationLabels);
    connect(orientCheck, &QCheckBox::toggled, this, [this](bool on) {
        st_.showOrientationLabels = on;
        refreshCanvas();
    });
    body(ovBox)->addWidget(orientCheck);
    v->addWidget(ovBox);

    v->addStretch();
    finishPanel(scroll, page);
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
    addBtn->setObjectName("accent");
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
    auto* toolRow = new QHBoxLayout;
    toolRow->setSpacing(0);
    auto* toolGroup = new QButtonGroup(this);
    const char* toolLabels[5] = {"Thresh", "Fill", "Trace", "Paint", "Erase"};
    for (int i = 0; i < 5; ++i) {
        auto* b = new QToolButton;
        b->setText(toolLabels[i]);
        b->setCheckable(true);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        const QString ends =
            i == 0 ? "border-top-left-radius:7px;border-bottom-left-radius:7px;"
            : i == 4 ? "border-top-right-radius:7px;border-bottom-right-radius:7px;"
                     : "";
        b->setStyleSheet(
            "QToolButton{background:#2a2d36;border:1px solid #3a3f4c;padding:6px 2px;"
            "color:#c3c8d2;font-size:12px;" + ends +
            "}QToolButton:checked{background:#4f7cf0;color:white;border-color:#4f7cf0;}");
        toolGroup->addButton(b, i);
        toolRow->addWidget(b, 1);
    }
    toolGroup->button(0)->setChecked(true);
    body(toolBox)->addLayout(toolRow);
    v->addWidget(toolBox);

    // Tool detail (stacked).
    toolDetail_ = new QStackedWidget;
    // 0: threshold
    {
        auto* w = new QWidget;
        auto* f = new QVBoxLayout(w);
        f->addWidget(new QLabel("Drag the handles to label every voxel in this HU "
                                "window into the active segment (applies live)."));
        threshLabel_ = new QLabel("Low 300  —  High 3000 HU");
        f->addWidget(threshLabel_);
        threshSlider_ = new RangeSlider;
        threshSlider_->setBounds(-1000, 3000);
        threshSlider_->setValues(300, 3000);
        f->addWidget(threshSlider_);
        // Live, debounced threshold; one undo snapshot per drag.
        threshTimer_ = new QTimer(this);
        threshTimer_->setSingleShot(true);
        threshTimer_->setInterval(120);
        connect(threshTimer_, &QTimer::timeout, this, &MainWindow::applyThreshold);
        connect(threshSlider_, &RangeSlider::editingStarted, this, [this] {
            LumenVolume* v = st_.volume;
            if (v && lumen_seg_active(v) != 0) {
                lumen_seg_push_undo(v);
                updateUndoRedo();
            }
        });
        connect(threshSlider_, &RangeSlider::rangeChanged, this,
                [this](double lo, double hi) {
                    threshLabel_->setText(QString("Low %1  —  High %2 HU")
                                              .arg(qRound(lo))
                                              .arg(qRound(hi)));
                    threshTimer_->start();  // debounce, then applyThreshold()
                });
        auto* presets = new QHBoxLayout;
        struct T { const char* n; double lo, hi; };
        for (T t : {T{"Bone", 300, 3000}, T{"Soft", 40, 80},
                    T{"Lung", -900, -400}}) {
            auto* b = new QPushButton(t.n);
            connect(b, &QPushButton::clicked, this, [this, t] {
                threshSlider_->setValues(t.lo, t.hi);
                threshLabel_->setText(QString("Low %1  —  High %2 HU")
                                          .arg(qRound(t.lo))
                                          .arg(qRound(t.hi)));
                LumenVolume* v = st_.volume;
                if (v && lumen_seg_active(v) != 0) {
                    lumen_seg_push_undo(v);
                    updateUndoRedo();
                }
                applyThreshold();
            });
            presets->addWidget(b);
        }
        f->addLayout(presets);
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

    connect(toolGroup, &QButtonGroup::idClicked, this, [this](int id) {
        static const struct { Tool tool; int page; } kMap[5] = {
            {Tool::Threshold, 0}, {Tool::RegionGrow, 1}, {Tool::LevelTrace, 2},
            {Tool::Paint, 3}, {Tool::Erase, 3}};
        st_.tool = kMap[id].tool;
        toolDetail_->setCurrentIndex(kMap[id].page);
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

    // Island cleanup. Keep the same operations and defaults as the macOS
    // segmentation model; the implementation remains in the shared core.
    auto* islandsBox = section("Islands");
    auto* keepBtn = new QPushButton("Keep largest island");
    connect(keepBtn, &QPushButton::clicked, this, &MainWindow::keepLargest);
    body(islandsBox)->addWidget(keepBtn);
    auto* removeRow = new QHBoxLayout;
    removeSmallSpin_ = new QSpinBox;
    removeSmallSpin_->setRange(1, 1000000000);
    removeSmallSpin_->setValue(50);
    removeRow->addWidget(new QLabel("Remove below voxels:"));
    removeRow->addWidget(removeSmallSpin_);
    auto* removeBtn = new QPushButton("Remove small islands");
    connect(removeBtn, &QPushButton::clicked, this,
            &MainWindow::removeSmallIslands);
    removeRow->addWidget(removeBtn);
    body(islandsBox)->addLayout(removeRow);
    v->addWidget(islandsBox);

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
    finishPanel(scroll, page);
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
    generateBtn_->setObjectName("accent");
    connect(generateBtn_, &QPushButton::clicked, this,
            &MainWindow::generateMesh);
    v->addWidget(generateBtn_);

    auto* meshBox = section("Mesh");
    meshInfoLabel_ = new QLabel("No surface yet.");
    meshInfoLabel_->setWordWrap(true);
    body(meshBox)->addWidget(meshInfoLabel_);
    body(meshBox)->addWidget(new QLabel("Drag to orbit, scroll to zoom."));
    v->addWidget(meshBox);

    auto* scissorBox = section("Scissor cut");
    body(scissorBox)->addWidget(new QLabel(
        "Draw a freehand loop over the surface to cut voxels through the depth, "
        "then the surface rebuilds."));
    scissorModeCheck_ = new QCheckBox("Scissor mode (draw to cut)");
    connect(scissorModeCheck_, &QCheckBox::toggled, this, [this](bool on) {
        if (meshView_) meshView_->setScissorMode(on);
    });
    body(scissorBox)->addWidget(scissorModeCheck_);
    scissorEraseCombo_ = new QComboBox;
    scissorEraseCombo_->addItem("Erase inside the loop", 1);
    scissorEraseCombo_->addItem("Keep inside (erase outside)", 0);
    body(scissorBox)->addWidget(scissorEraseCombo_);
    scissorActiveOnlyCheck_ = new QCheckBox("Active segment only");
    body(scissorBox)->addWidget(scissorActiveOnlyCheck_);
    v->addWidget(scissorBox);

    v->addStretch();
    finishPanel(scroll, page);
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

    auto* meshBox = section("3D mesh (STL)");
    body(meshBox)->addWidget(new QLabel("Choose which segments to export:"));

    exportSegContainer_ = new QWidget;
    exportSegLayout_ = new QVBoxLayout(exportSegContainer_);
    exportSegLayout_->setContentsMargins(0, 0, 0, 0);
    exportSegLayout_->setSpacing(2);
    body(meshBox)->addWidget(exportSegContainer_);

    auto* allNone = new QHBoxLayout;
    auto* allBtn = new QPushButton("All");
    auto* noneBtn = new QPushButton("None");
    connect(allBtn, &QPushButton::clicked, this, [this] {
        for (auto* c : exportSegChecks_) c->setChecked(true);
    });
    connect(noneBtn, &QPushButton::clicked, this, [this] {
        for (auto* c : exportSegChecks_) c->setChecked(false);
    });
    allNone->addWidget(allBtn);
    allNone->addWidget(noneBtn);
    body(meshBox)->addLayout(allNone);

    oneFilePerSegCheck_ = new QCheckBox("One file per segment");
    oneFilePerSegCheck_->setChecked(true);
    body(meshBox)->addWidget(oneFilePerSegCheck_);

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
    finishPanel(scroll, page);
    return scroll;
}

// ---------------------------------------------------------------------------
// Markups panel
// ---------------------------------------------------------------------------
QWidget* MainWindow::buildMarkupPanel() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->setSpacing(10);

    v->addWidget(new QLabel(
        "Drop points on any slice; they show in the 3D view. Point = 1 click, "
        "Line = 2, Plane = 3 (a triangle)."));

    auto* typeBox = section("Type");
    markupKindCombo_ = new QComboBox;
    markupKindCombo_->addItem("Point", int(MarkupModel::Kind::Point));
    markupKindCombo_->addItem("Line", int(MarkupModel::Kind::Line));
    markupKindCombo_->addItem("Plane", int(MarkupModel::Kind::Plane));
    connect(markupKindCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        markups_.setKind(MarkupModel::Kind(markupKindCombo_->currentData().toInt()));
        markups_.cancelPending();
        updateMarkupPending();
        refreshMarkups();
    });
    body(typeBox)->addWidget(markupKindCombo_);
    v->addWidget(typeBox);

    auto* colorBox = section("Colour");
    auto* palRow = new QHBoxLayout;
    palRow->setSpacing(4);
    for (int i = 0; i < int(MarkupModel::palette().size()); ++i) {
        auto* b = new QToolButton;
        b->setFixedSize(20, 20);
        b->setToolTip(QString("Colour %1").arg(i + 1));
        connect(b, &QToolButton::clicked, this, [this, i] {
            markups_.pickNextColor(i);
            updateMarkupPaletteSelection();
            refreshMarkups();
        });
        markupPaletteBtns_.append(b);
        palRow->addWidget(b);
    }
    palRow->addStretch();
    body(colorBox)->addLayout(palRow);
    body(colorBox)->addWidget(
        new QLabel("New markups use this colour until you pick another."));
    v->addWidget(colorBox);

    auto* placeBox = section("Place");
    markupPlaceCheck_ = new QCheckBox("Place markups (click slices)");
    connect(markupPlaceCheck_, &QCheckBox::toggled, this, [this](bool on) {
        markups_.setPlacing(on);
        st_.markupPlacing = on && currentTab_ == 4;
        if (!on) markups_.cancelPending();
        updateMarkupPending();
        refreshMarkups();
    });
    body(placeBox)->addWidget(markupPlaceCheck_);
    auto* pendRow = new QHBoxLayout;
    markupPendingLabel_ = new QLabel;
    markupCancelBtn_ = new QPushButton("Cancel point");
    connect(markupCancelBtn_, &QPushButton::clicked, this, [this] {
        markups_.cancelPending();
        updateMarkupPending();
        refreshMarkups();
    });
    pendRow->addWidget(markupPendingLabel_, 1);
    pendRow->addWidget(markupCancelBtn_);
    body(placeBox)->addLayout(pendRow);
    v->addWidget(placeBox);

    auto* listBox = section("Markups");
    markupListContainer_ = new QWidget;
    markupListLayout_ = new QVBoxLayout(markupListContainer_);
    markupListLayout_->setContentsMargins(0, 0, 0, 0);
    markupListLayout_->setSpacing(3);
    body(listBox)->addWidget(markupListContainer_);
    auto* clearBtn = new QPushButton("Clear all");
    connect(clearBtn, &QPushButton::clicked, this, [this] {
        markups_.removeAll();
        rebuildMarkupList();
        updateMarkupPending();
        refreshMarkups();
    });
    body(listBox)->addWidget(clearBtn);
    v->addWidget(listBox);

    v->addStretch();
    finishPanel(scroll, page);
    updateMarkupPaletteSelection();
    updateMarkupPending();
    return scroll;
}

void MainWindow::updateMarkupPaletteSelection() {
    for (int i = 0; i < markupPaletteBtns_.size(); ++i) {
        const QColor c = MarkupModel::paletteColor(i);
        const bool sel = (i == markups_.nextColorIndex());
        markupPaletteBtns_[i]->setStyleSheet(
            QString("QToolButton{background:rgb(%1,%2,%3);border:%4;border-radius:4px;}")
                .arg(c.red()).arg(c.green()).arg(c.blue())
                .arg(sel ? "2px solid white" : "1px solid #555"));
    }
}

void MainWindow::updateMarkupPending() {
    if (!markupPendingLabel_) return;
    const int have = int(markups_.pending().size());
    const int need = MarkupModel::pointsNeeded(markups_.kind());
    markupPendingLabel_->setText(
        have > 0 ? QString("%1/%2 points placed…").arg(have).arg(need)
                 : (markups_.placing() ? "Click a slice to drop a point."
                                       : "Turn on, then click the slices."));
    markupCancelBtn_->setVisible(have > 0);
}

void MainWindow::refreshMarkups() {
    refreshCanvas();
    if (meshView_) meshView_->update();
}

void MainWindow::rebuildMarkupList() {
    if (!markupListLayout_) return;
    QLayoutItem* item = nullptr;
    while ((item = markupListLayout_->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    for (const auto& m : markups_.markups()) {
        const int id = m.id;
        auto* row = new QWidget;
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(2, 1, 2, 1);
        h->setSpacing(4);

        auto* vis = new QCheckBox;
        vis->setChecked(m.visible);
        connect(vis, &QCheckBox::toggled, this, [this, id](bool on) {
            markups_.setVisible(id, on);
            refreshMarkups();
        });
        h->addWidget(vis);

        const QColor col = markups_.color(m);
        auto* swatch = new QToolButton;
        swatch->setFixedSize(16, 16);
        swatch->setToolTip("Click to recolour");
        swatch->setStyleSheet(
            QString("background:rgb(%1,%2,%3);border:1px solid #555;")
                .arg(col.red()).arg(col.green()).arg(col.blue()));
        connect(swatch, &QToolButton::clicked, this, [this, id] {
            // Cycle to the next palette colour.
            const MarkupModel::Markup* found = nullptr;
            for (const auto& mm : markups_.markups())
                if (mm.id == id) found = &mm;
            if (found)
                markups_.setColorIndex(id, found->colorIndex + 1);
            rebuildMarkupList();
            refreshMarkups();
        });
        h->addWidget(swatch);

        auto* name = new QLineEdit(m.name);
        connect(name, &QLineEdit::editingFinished, this,
                [this, id, name] { markups_.rename(id, name->text()); });
        h->addWidget(name, 1);

        auto* kind = new QLabel(MarkupModel::title(m.kind));
        kind->setStyleSheet("color:#8a8f9a;");
        h->addWidget(kind);

        auto* del = new QToolButton;
        del->setText("✕");
        connect(del, &QToolButton::clicked, this, [this, id] {
            markups_.remove(id);
            rebuildMarkupList();
            refreshMarkups();
        });
        h->addWidget(del);

        markupListLayout_->addWidget(row);
    }
    if (markups_.markups().empty()) {
        auto* empty = new QLabel("No markups yet.");
        empty->setStyleSheet("color:#8a8f9a;");
        markupListLayout_->addWidget(empty);
    }
}

void MainWindow::onMarkupPointPicked(int x, int y, int z) {
    const bool committed = markups_.place(x, y, z);
    if (committed) rebuildMarkupList();
    updateMarkupPaletteSelection();
    updateMarkupPending();
    refreshMarkups();
}

// ---------------------------------------------------------------------------
// Canvas quad: Axial / Coronal / Sagittal / 3D, each double-click-maximizable
// ---------------------------------------------------------------------------
QWidget* MainWindow::buildQuad() {
    auto* board = new QWidget;
    quadLayout_ = new QGridLayout(board);
    quadLayout_->setContentsMargins(8, 8, 8, 8);
    quadLayout_->setSpacing(8);
    const int axes[3] = {LUMEN_AXIS_AXIAL, LUMEN_AXIS_CORONAL,
                         LUMEN_AXIS_SAGITTAL};
    const int cellPos[4][2] = {{0, 0}, {0, 1}, {1, 0}, {1, 1}};

    for (int i = 0; i < 3; ++i) {
        auto* cell = new QWidget;
        auto* col = new QVBoxLayout(cell);
        col->setContentsMargins(0, 0, 0, 0);
        col->setSpacing(2);
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
        connect(panes_[i], &SliceView::markupPointPicked, this,
                &MainWindow::onMarkupPointPicked);
        connect(panes_[i], &SliceView::strokeBegan, this,
                &MainWindow::onStrokeBegan);
        connect(panes_[i], &SliceView::doubleClicked, this,
                [this, i] { toggleMaximize(i); });
        col->addWidget(panes_[i], 1);
        sliders_[i] = new QSlider(Qt::Horizontal);
        const int axis = axes[i];
        connect(sliders_[i], &QSlider::valueChanged, this,
                [this, axis](int val) { onSliceScrolled(axis, val); });
        col->addWidget(sliders_[i]);
        quadCells_[i] = cell;
        quadLayout_->addWidget(cell, cellPos[i][0], cellPos[i][1]);
    }
    // Fourth cell: the 3D view, wrapped in a rounded card frame to match the
    // slice panes.
    auto* meshFrame = new QWidget;
    meshFrame->setObjectName("meshCard");
    meshFrame->setStyleSheet(
        "QWidget#meshCard{background:#121418;border:1px solid #2f3440;"
        "border-radius:12px;}");
    auto* mf = new QVBoxLayout(meshFrame);
    mf->setContentsMargins(2, 2, 2, 2);
    mf->addWidget(meshView_);
    connect(meshView_, &MeshView::doubleClicked, this,
            [this] { toggleMaximize(3); });
    quadCells_[3] = meshFrame;
    quadLayout_->addWidget(meshFrame, cellPos[3][0], cellPos[3][1]);

    quadLayout_->setRowStretch(0, 1);
    quadLayout_->setRowStretch(1, 1);
    quadLayout_->setColumnStretch(0, 1);
    quadLayout_->setColumnStretch(1, 1);
    return board;
}

// Double-click maximizes a cell (hides the other three); double-click again
// restores the 2x2 grid.
void MainWindow::toggleMaximize(int cell) {
    maximized_ = (maximized_ == cell) ? -1 : cell;
    for (int i = 0; i < 4; ++i)
        if (quadCells_[i])
            quadCells_[i]->setVisible(maximized_ == -1 || maximized_ == i);

    // Collapse the empty row/column so the maximized cell fills the canvas.
    const int pos[4][2] = {{0, 0}, {0, 1}, {1, 0}, {1, 1}};
    if (maximized_ == -1) {
        quadLayout_->setRowStretch(0, 1);
        quadLayout_->setRowStretch(1, 1);
        quadLayout_->setColumnStretch(0, 1);
        quadLayout_->setColumnStretch(1, 1);
    } else {
        const int r = pos[maximized_][0], c = pos[maximized_][1];
        quadLayout_->setRowStretch(0, r == 0 ? 1 : 0);
        quadLayout_->setRowStretch(1, r == 1 ? 1 : 0);
        quadLayout_->setColumnStretch(0, c == 0 ? 1 : 0);
        quadLayout_->setColumnStretch(1, c == 1 ? 1 : 0);
    }
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
    if (loading_) return;
    loading_ = true;
    setStatus(QString("Loading %1 …").arg(path));
    // Ingestion (recursive crawl + decode) runs off the UI thread so a large
    // series never freezes the window; the volume is adopted back on the UI thread.
    loadWatcher_.setFuture(QtConcurrent::run([path]() -> LoadResult {
        char msg[512] = {0};
        LumenVolume* v =
            lumen_load_folder(path.toUtf8().constData(), msg, sizeof(msg));
        return LoadResult{v, QString::fromUtf8(msg)};
    }));
}

void MainWindow::onLoadReady() {
    loading_ = false;
    const LoadResult r = loadWatcher_.result();
    if (!r.volume) {
        setStatus(QString("Could not load: %1").arg(r.message));
        QMessageBox::warning(
            this, "LumenSlice",
            r.message.isEmpty() ? "Could not load the DICOM folder." : r.message);
        return;
    }
    vol_.adopt(r.volume);
    const std::string status = r.message.toStdString();
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

    // A fresh volume invalidates all markups (their voxel coords no longer map).
    float sx = 1, sy = 1, sz = 1;
    lumen_spacing(v, &sx, &sy, &sz);
    markups_.resetForVolume(sx, sy, sz);
    if (markupPlaceCheck_) markupPlaceCheck_->setChecked(false);

    setStatus(QString::fromStdString(status));
    refreshAll();
    meshView_->clearMeshes();
    meshInfoLabel_->setText("No surface yet.");
}

void MainWindow::selectTab(int tab) {
    currentTab_ = tab;
    panels_->setCurrentIndex(tab);
    // Segment tab enables canvas tool interactions; others keep left-drag = W/L.
    st_.segmentInteractive = (tab == 1);
    // Markup placement is only active on the Markups tab with the toggle on.
    st_.markupPlacing = (tab == 4) && markups_.placing();
    if (tab == 3) rebuildExportSegmentList();  // reflect current segments/names
    if (tab == 4) rebuildMarkupList();
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
    // Fills the active segment from the slider's HU window. No undo snapshot here:
    // the live drag snapshots once on editingStarted, and presets/Otsu snapshot
    // before calling this.
    LumenVolume* v = st_.volume;
    if (!v || lumen_seg_active(v) == 0 || !threshSlider_) return;
    lumen_seg_threshold(v, float(threshSlider_->lowValue()),
                        float(threshSlider_->highValue()));
    refreshCanvas();
    updateSegmentCounts();
}

void MainWindow::applyOtsu() {
    LumenVolume* v = st_.volume;
    if (!v || lumen_seg_active(v) == 0) return;
    const float t = lumen_seg_otsu(v);
    float lo = 0, hi = 0;
    lumen_hu_range(v, &lo, &hi);
    threshSlider_->setValues(t, hi);
    threshLabel_->setText(
        QString("Low %1  —  High %2 HU").arg(qRound(t)).arg(qRound(hi)));
    lumen_seg_push_undo(v);
    updateUndoRedo();
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

void MainWindow::keepLargest() {
    LumenVolume* v = st_.volume;
    if (!v || lumen_seg_active(v) == 0) return;
    lumen_seg_push_undo(v);
    lumen_seg_keep_largest(v);
    updateUndoRedo();
    refreshCanvas();
    updateSegmentCounts();
}

void MainWindow::removeSmallIslands() {
    LumenVolume* v = st_.volume;
    if (!v || lumen_seg_active(v) == 0 || !removeSmallSpin_) return;
    lumen_seg_push_undo(v);
    lumen_seg_remove_small(v, removeSmallSpin_->value());
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

    // Collect the visible, non-empty segments — one colored surface each.
    std::vector<long> hist(256, 0);
    lumen_seg_label_histogram(v, hist.data());
    pendingSegIds_.clear();
    const int count = lumen_seg_segment_count(v);
    for (int i = 0; i < count; ++i) {
        const int id = lumen_seg_segment_id_at(v, i);
        if (lumen_seg_get_visible(v, id) && hist[id] > 0) pendingSegIds_.push_back(id);
    }
    if (pendingSegIds_.empty()) {
        meshView_->clearMeshes();
        meshInfoLabel_->setText("No visible, non-empty segments to surface.");
        return;
    }

    generating_ = true;
    generateBtn_->setEnabled(false);
    generateBtn_->setText("Generating…");
    exportStlBtn_->setEnabled(false);
    meshPieces_.clear();
    pendingSegIndex_ = 0;
    startNextMeshSegment();
}

void MainWindow::startNextMeshSegment() {
    LumenVolume* v = st_.volume;
    if (!v || pendingSegIndex_ >= int(pendingSegIds_.size())) {
        finishMeshGeneration();
        return;
    }
    const int id = pendingSegIds_[pendingSegIndex_];
    lumen_mesh_snapshot_label(v, id);  // UI thread: copy just this segment's mask
    const int smooth = smoothingSpin_->value();
    const int down = resolutionCombo_->currentData().toInt();
    // Marching cubes on a worker thread (touches only the snapshot + mesh).
    meshWatcher_.setFuture(QtConcurrent::run(
        [v, smooth, down] { return lumen_mesh_generate(v, smooth, down); }));
}

void MainWindow::onMeshReady() {
    // Back on the UI thread: read this segment's mesh, tint it, keep going.
    LumenVolume* v = st_.volume;
    if (v && pendingSegIndex_ < int(pendingSegIds_.size())) {
        const int id = pendingSegIds_[pendingSegIndex_];
        const int vcount = lumen_mesh_vertex_count(v);
        const int icount = lumen_mesh_index_count(v);
        const float* verts = lumen_mesh_vertices(v);
        const float* norms = lumen_mesh_normals(v);
        const unsigned int* idx = lumen_mesh_indices(v);
        if (vcount > 0 && icount > 0 && verts && idx) {
            MeshView::MeshPiece piece;
            piece.interleaved.resize(size_t(vcount) * 6);
            for (int i = 0; i < vcount; ++i) {
                float* dst = &piece.interleaved[size_t(i) * 6];
                for (int k = 0; k < 3; ++k) dst[k] = verts[size_t(i) * 3 + k];
                for (int k = 0; k < 3; ++k)
                    dst[3 + k] = norms ? norms[size_t(i) * 3 + k] : 0.0f;
            }
            piece.indices.assign(idx, idx + icount);
            int r = 200, g = 200, b = 200;
            lumen_seg_get_color(v, id, &r, &g, &b);
            piece.color[0] = r / 255.0f;
            piece.color[1] = g / 255.0f;
            piece.color[2] = b / 255.0f;
            meshPieces_.push_back(std::move(piece));
        }
    }
    ++pendingSegIndex_;
    startNextMeshSegment();
}

void MainWindow::finishMeshGeneration() {
    generating_ = false;
    generateBtn_->setText("Generate / Update 3D");
    long tris = 0, verts = 0;
    for (const auto& p : meshPieces_) {
        tris += long(p.indices.size() / 3);
        verts += long(p.interleaved.size() / 6);
    }
    meshView_->setMeshes(meshPieces_);
    meshInfoLabel_->setText(QString("Surfaces: %1\nTriangles: %2\nVertices: %3")
                                .arg(meshPieces_.size())
                                .arg(tris)
                                .arg(verts));
    LumenVolume* v = st_.volume;
    generateBtn_->setEnabled(v && lumen_seg_count(v) > 0);
    exportStlBtn_->setEnabled(v && lumen_seg_count(v) > 0);
}

void MainWindow::onScissorFinished(const QList<QPointF>& poly) {
    LumenVolume* v = st_.volume;
    if (!v || poly.size() < 3 || generating_) {
        if (meshView_) meshView_->clearLasso();
        return;
    }
    std::vector<float> xy;
    xy.reserve(size_t(poly.size()) * 2);
    for (const QPointF& p : poly) {
        xy.push_back(float(p.x()));
        xy.push_back(float(p.y()));
    }
    // The core projects with proj*view; QMatrix4x4::constData() is exactly the
    // column-major buffer its scissor_cut expects (see scissor.hpp).
    const QMatrix4x4 mvp = meshView_->lastMvp();
    const int eraseInside = scissorEraseCombo_->currentData().toInt();
    const int onlyLabel =
        scissorActiveOnlyCheck_->isChecked() ? lumen_seg_active(v) : 0;

    lumen_seg_push_undo(v);
    const long cleared = lumen_seg_scissor_cut(
        v, mvp.constData(), meshView_->width(), meshView_->height(), xy.data(),
        int(poly.size()), eraseInside, onlyLabel);
    updateUndoRedo();
    refreshCanvas();
    updateSegmentCounts();
    meshView_->clearLasso();
    meshInfoLabel_->setText(QString("Scissor cut cleared %1 voxels. Rebuilding…")
                                .arg(cleared));
    generateMesh();  // rebuild the surface to reflect the cut
}

void MainWindow::exportStl() {
    LumenVolume* v = st_.volume;
    if (!v) return;

    // The checked segments that actually carry voxels.
    std::vector<long> hist(256, 0);
    lumen_seg_label_histogram(v, hist.data());
    std::vector<int> ids;
    for (auto it = exportSegChecks_.constBegin(); it != exportSegChecks_.constEnd();
         ++it) {
        if (it.value()->isChecked() && hist[it.key()] > 0) ids.push_back(it.key());
    }
    if (ids.empty()) {
        exportMsgLabel_->setText("Select at least one non-empty segment to export.");
        return;
    }

    const int smooth = smoothingSpin_->value();
    const int down = resolutionCombo_->currentData().toInt();

    if (oneFilePerSegCheck_->isChecked()) {
        // One STL per segment into a chosen directory.
        const QString dir = QFileDialog::getExistingDirectory(
            this, "Export one STL per segment to folder", QDir::homePath());
        if (dir.isEmpty()) return;
        QApplication::setOverrideCursor(Qt::WaitCursor);
        int written = 0;
        for (int id : ids) {
            lumen_mesh_snapshot_label(v, id);   // UI thread (export is synchronous)
            if (lumen_mesh_generate(v, smooth, down) <= 0) continue;
            const QString name = sanitizeFilename(
                segNames_.value(id, QString("Segment %1").arg(id)));
            const QString path = QString("%1/%2.stl").arg(dir, name);
            if (lumen_mesh_write_stl(v, path.toUtf8().constData()) == 0) ++written;
        }
        QApplication::restoreOverrideCursor();
        exportMsgLabel_->setText(
            QString("Wrote %1 STL file(s) to %2").arg(written).arg(dir));
    } else {
        // Fuse the chosen segments into a single STL (union via the bridge).
        const QString path = QFileDialog::getSaveFileName(
            this, "Export fused STL", QDir::homePath() + "/LumenSlice.stl",
            "STL mesh (*.stl)");
        if (path.isEmpty()) return;
        QApplication::setOverrideCursor(Qt::WaitCursor);
        lumen_mesh_snapshot_labels(v, ids.data(), int(ids.size()));
        const int tris = lumen_mesh_generate(v, smooth, down);
        const int rc =
            tris > 0 ? lumen_mesh_write_stl(v, path.toUtf8().constData()) : -1;
        QApplication::restoreOverrideCursor();
        exportMsgLabel_->setText(
            rc == 0 ? QString("Saved %1 (%2 segments fused)")
                          .arg(QFileInfo(path).fileName())
                          .arg(ids.size())
                    : "STL export failed.");
    }
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
    if (threshSlider_) threshSlider_->setBounds(lo, hi);

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
        exportStlBtn_->setEnabled(total > 0);  // export generates its own meshes
}

void MainWindow::rebuildExportSegmentList() {
    if (!exportSegLayout_) return;
    QLayoutItem* item = nullptr;
    while ((item = exportSegLayout_->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    exportSegChecks_.clear();

    LumenVolume* v = st_.volume;
    if (!v) return;
    std::vector<long> hist(256, 0);
    lumen_seg_label_histogram(v, hist.data());
    const int count = lumen_seg_segment_count(v);
    for (int i = 0; i < count; ++i) {
        const int id = lumen_seg_segment_id_at(v, i);
        auto* c = new QCheckBox(QString("%1  (%2 voxels)")
                                    .arg(segNames_.value(
                                        id, QString("Segment %1").arg(id)))
                                    .arg(hist[id]));
        c->setChecked(hist[id] > 0);
        c->setEnabled(hist[id] > 0);
        exportSegChecks_[id] = c;
        exportSegLayout_->addWidget(c);
    }
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
