#include "MeshView.h"

#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLineF>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSurfaceFormat>
#include <QToolButton>
#include <QVector3D>
#include <QVector4D>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

#include "MarkupModel.h"

namespace lumenwin {

namespace {
const char* kVertexShader = R"(#version 330 core
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
uniform mat4 uMvp;
uniform mat4 uModelView;
out vec3 vNormalView;
out vec3 vPosView;
void main() {
    vNormalView = mat3(uModelView) * inNormal;
    vPosView = vec3(uModelView * vec4(inPos, 1.0));
    gl_Position = uMvp * vec4(inPos, 1.0);
}
)";

const char* kFragmentShader = R"(#version 330 core
in vec3 vNormalView;
in vec3 vPosView;
out vec4 fragColor;
uniform vec3 uColor;
void main() {
    vec3 n = normalize(vNormalView);
    if (!gl_FrontFacing) n = -n;
    vec3 l = normalize(-vPosView);           // head-light
    float diff = max(dot(n, l), 0.0);
    vec3 viewDir = normalize(-vPosView);
    vec3 h = normalize(l + viewDir);
    float spec = pow(max(dot(n, h), 0.0), 24.0) * 0.25;
    vec3 col = uColor * (0.22 + 0.78 * diff) + vec3(spec);
    fragColor = vec4(col, 1.0);
}
)";

const char* kVolumeVertexShader = R"(#version 330 core
layout(location = 0) in vec3 inPos;
uniform mat4 uMvp;
uniform vec3 uVolumeMin;
uniform vec3 uVolumeExtent;
out vec3 vTexCoord;
void main() {
    vTexCoord = inPos;
    gl_Position = uMvp * vec4(uVolumeMin + inPos * uVolumeExtent, 1.0);
}
)";

const char* kVolumeFragmentShader = R"(#version 330 core
in vec3 vTexCoord;
out vec4 fragColor;
uniform sampler3D uVolume;
uniform vec3 uCamera;
uniform float uStep;
uniform float uDensity;

void main() {
    vec3 ray = normalize(vTexCoord - uCamera);
    vec3 invRay = 1.0 / max(abs(ray), vec3(0.00001)) * sign(ray);
    vec3 t0 = (vec3(0.0) - vTexCoord) * invRay;
    vec3 t1 = (vec3(1.0) - vTexCoord) * invRay;
    float entry = max(max(min(t0.x, t1.x), min(t0.y, t1.y)), min(t0.z, t1.z));
    float exit = min(min(max(t0.x, t1.x), max(t0.y, t1.y)), max(t0.z, t1.z));
    if (exit <= max(entry, 0.0)) discard;

    vec3 p = vTexCoord + ray * max(entry, 0.0);
    float travelled = max(entry, 0.0);
    vec4 accum = vec4(0.0);
    for (int i = 0; i < 512 && travelled < exit && accum.a < 0.98; ++i) {
        float scalar = texture(uVolume, clamp(p, 0.0, 1.0)).r;
        float a = smoothstep(0.18, 0.72, scalar) * uDensity;
        vec3 colour = vec3(scalar * 0.75 + 0.25, scalar * 0.88 + 0.12, 1.0);
        a *= 1.0 - accum.a;
        accum.rgb += colour * a;
        accum.a += a;
        p += ray * uStep;
        travelled += uStep;
    }
    if (accum.a < 0.01) discard;
    fragColor = accum;
}
)";
}  // namespace

MeshView::MeshView(QWidget* parent) : QOpenGLWidget(parent) {
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    fmt.setSamples(4);
    setFormat(fmt);
    setMinimumSize(200, 200);
    buildToolbar();
    resetView();
}

MeshView::~MeshView() {
    if (glReady_) {
        makeCurrent();
        vbo_.destroy();
        ibo_.destroy();
        volumeVbo_.destroy();
        volumeIbo_.destroy();
        volumeTexture_.destroy();
        vao_.destroy();
        volumeVao_.destroy();
        doneCurrent();
    }
}

// --- Toolbar ---------------------------------------------------------------
void MeshView::buildToolbar() {
    toolbar_ = new QWidget(this);
    toolbar_->setStyleSheet("background:rgba(20,22,28,190);border-radius:6px;");
    auto* h = new QHBoxLayout(toolbar_);
    h->setContentsMargins(6, 4, 6, 4);
    h->setSpacing(2);

    auto mk = [&](const QString& text, const QString& tip) {
        auto* b = new QToolButton(toolbar_);
        b->setText(text);
        b->setToolTip(tip);
        b->setStyleSheet(
            "QToolButton{color:#e6e8ee;padding:5px 10px;border:none;font-size:16px;}"
            "QToolButton:hover{background:rgba(255,255,255,32);border-radius:5px;}"
            "QToolButton::menu-indicator{image:none;}");
        h->addWidget(b);
        return b;
    };

    connect(mk("⟲", "Reset / reframe"), &QToolButton::clicked, this,
            &MeshView::resetView);
    connect(mk("+", "Zoom in"), &QToolButton::clicked, this,
            [this] { zoomBy(0.8f); });
    connect(mk("−", "Zoom out"), &QToolButton::clicked, this,
            [this] { zoomBy(1.25f); });

    auto* views = mk("Views ▾", "Standard anatomical views");
    views->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(views);
    struct V { const char* name; StdView v; };
    for (V item : {V{"Anterior", StdView::Anterior},
                   V{"Posterior", StdView::Posterior}, V{"Left", StdView::Left},
                   V{"Right", StdView::Right}, V{"Superior", StdView::Superior},
                   V{"Inferior", StdView::Inferior}}) {
        const StdView which = item.v;
        connect(menu->addAction(item.name), &QAction::triggered, this,
                [this, which] { setStandardView(which); });
    }
    views->setMenu(menu);

    connect(mk("⤓", "Save a PNG snapshot"), &QToolButton::clicked, this,
            &MeshView::saveSnapshot);

    // Generate the surface and toggle the scissor cut straight from the 3D view.
    connect(mk("⬡", "Generate / update the 3D surface"), &QToolButton::clicked,
            this, [this] { emit generateRequested(); });
    scissorBtn_ = mk("✂", "Scissor: draw a loop to cut the surface");
    scissorBtn_->setCheckable(true);
    scissorBtn_->setStyleSheet(
        scissorBtn_->styleSheet() +
        "QToolButton:checked{background:#4f7cf0;color:white;border-radius:5px;}");
    connect(scissorBtn_, &QToolButton::toggled, this, [this](bool on) {
        setScissorMode(on);
        emit scissorModeChanged(on);
    });

    connect(mk("⛶", "Maximize / restore this view"), &QToolButton::clicked, this,
            [this] { emit doubleClicked(); });

    toolbar_->adjustSize();
}

void MeshView::positionToolbar() {
    if (toolbar_) {
        toolbar_->adjustSize();
        toolbar_->move(12, 12);
        toolbar_->raise();
    }
}

// --- Camera ----------------------------------------------------------------
QMatrix4x4 MeshView::viewMatrix() const {
    QMatrix4x4 v;
    v.translate(0, 0, -dist_);
    v *= rot_;
    v.translate(-center_[0], -center_[1], -center_[2]);
    return v;
}

QMatrix4x4 MeshView::projMatrix() const {
    const float aspect = height() > 0 ? float(width()) / float(height()) : 1.0f;
    QMatrix4x4 p;
    p.perspective(40.0f, aspect, 0.05f * radius_, 100.0f * radius_);
    return p;
}

void MeshView::resetView() {
    dist_ = 3.0f * radius_;
    // A pleasant anterior-superior-right three-quarter default.
    const QVector3D c(center_[0], center_[1], center_[2]);
    const QVector3D dir = QVector3D(0.55f, 0.75f, 0.35f).normalized();
    QMatrix4x4 m;
    m.lookAt(c + dir * (3.0f * radius_), c, QVector3D(0, 0, 1));
    m.setColumn(3, QVector4D(0, 0, 0, 1));  // keep rotation only
    rot_ = m;
    update();
}

void MeshView::setStandardView(StdView view) {
    QVector3D dir(0, 1, 0), up(0, 0, 1);
    switch (view) {
        case StdView::Anterior:  dir = {0, 1, 0};  up = {0, 0, 1}; break;
        case StdView::Posterior: dir = {0, -1, 0}; up = {0, 0, 1}; break;
        case StdView::Right:     dir = {1, 0, 0};  up = {0, 0, 1}; break;
        case StdView::Left:      dir = {-1, 0, 0}; up = {0, 0, 1}; break;
        case StdView::Superior:  dir = {0, 0, 1};  up = {0, 1, 0}; break;
        case StdView::Inferior:  dir = {0, 0, -1}; up = {0, 1, 0}; break;
    }
    const QVector3D c(center_[0], center_[1], center_[2]);
    QMatrix4x4 m;
    m.lookAt(c + dir * (3.0f * radius_), c, up);
    m.setColumn(3, QVector4D(0, 0, 0, 1));
    rot_ = m;
    dist_ = 3.0f * radius_;
    update();
}

void MeshView::zoomBy(float factor) {
    dist_ = std::clamp(dist_ * factor, 0.4f * radius_, 60.0f * radius_);
    update();
}

void MeshView::setVolumeTexture(std::vector<unsigned char> voxels, int width,
                                int height, int depth, float sx, float sy,
                                float sz) {
    pendingVolume_ = std::move(voxels);
    volumeWidth_ = width;
    volumeHeight_ = height;
    volumeDepth_ = depth;
    volumeSpacing_[0] = sx > 0 ? sx : 1.0f;
    volumeSpacing_[1] = sy > 0 ? sy : 1.0f;
    volumeSpacing_[2] = sz > 0 ? sz : 1.0f;
    volumeTexturePending_ = true;
    if (glReady_) {
        makeCurrent();
        uploadVolumeTexture();
        doneCurrent();
    }
    update();
}

void MeshView::clearVolumeTexture() {
    pendingVolume_.clear();
    volumeWidth_ = volumeHeight_ = volumeDepth_ = 0;
    volumeTexturePending_ = false;
    if (glReady_) {
        makeCurrent();
        volumeTexture_.destroy();
        doneCurrent();
    }
    update();
}

void MeshView::setVolumeRendering(bool on) {
    volumeRendering_ = on;
    update();
}

void MeshView::saveSnapshot() {
    const QImage img = grabFramebuffer();
    const QString path = QFileDialog::getSaveFileName(
        this, "Save 3D snapshot", QDir::homePath() + "/lumen3d.png",
        "PNG image (*.png)");
    if (!path.isEmpty()) img.save(path, "PNG");
}

// --- GL --------------------------------------------------------------------
void MeshView::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(0.06f, 0.07f, 0.09f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);

    program_.addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader);
    program_.addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader);
    program_.link();
    volumeProgram_.addShaderFromSourceCode(QOpenGLShader::Vertex,
                                           kVolumeVertexShader);
    volumeProgram_.addShaderFromSourceCode(QOpenGLShader::Fragment,
                                           kVolumeFragmentShader);
    volumeProgram_.link();

    vao_.create();
    vbo_.create();
    ibo_.create();
    volumeVao_.create();
    volumeVbo_.create();
    volumeIbo_.create();
    glReady_ = true;
    if (pendingUpload_) uploadPending();
    if (volumeTexturePending_) uploadVolumeTexture();
}

void MeshView::uploadVolumeTexture() {
    if (!volumeTexturePending_ || pendingVolume_.empty() || volumeWidth_ <= 0 ||
        volumeHeight_ <= 0 || volumeDepth_ <= 0) return;
    volumeTexturePending_ = false;
    volumeTexture_.destroy();
    volumeTexture_.setFormat(QOpenGLTexture::R8_UNorm);
    volumeTexture_.setSize(volumeWidth_, volumeHeight_, volumeDepth_);
    volumeTexture_.setMinMagFilters(QOpenGLTexture::Linear,
                                    QOpenGLTexture::Linear);
    volumeTexture_.setWrapMode(QOpenGLTexture::ClampToEdge);
    volumeTexture_.allocateStorage(QOpenGLTexture::Red, QOpenGLTexture::UInt8);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    volumeTexture_.setData(QOpenGLTexture::Red, QOpenGLTexture::UInt8,
                           pendingVolume_.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    const float vertices[] = {
        0,0,0, 1,0,0, 1,1,0, 0,1,0, 0,0,1, 1,0,1, 1,1,1, 0,1,1,
    };
    const unsigned int indices[] = {
        0,1,2, 2,3,0, 4,6,5, 6,4,7, 0,4,5, 5,1,0,
        3,2,6, 6,7,3, 0,3,7, 7,4,0, 1,5,6, 6,2,1,
    };
    volumeVao_.bind();
    volumeVbo_.bind();
    volumeVbo_.allocate(vertices, sizeof(vertices));
    volumeIbo_.bind();
    volumeIbo_.allocate(indices, sizeof(indices));
    volumeProgram_.bind();
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    volumeProgram_.release();
    volumeVao_.release();
    pendingVolume_.shrink_to_fit();
    if (totalIndices_ == 0) {
        center_[0] = 0.5f * volumeWidth_ * volumeSpacing_[0];
        center_[1] = 0.5f * volumeHeight_ * volumeSpacing_[1];
        center_[2] = 0.5f * volumeDepth_ * volumeSpacing_[2];
        radius_ = 0.5f * std::max({volumeWidth_ * volumeSpacing_[0],
                                   volumeHeight_ * volumeSpacing_[1],
                                   volumeDepth_ * volumeSpacing_[2]});
        resetView();
    }
}

void MeshView::setMeshes(std::vector<MeshPiece> pieces) {
    pendingInterleaved_.clear();
    pendingIndices_.clear();
    pendingRanges_.clear();

    float lo[3] = {1e30f, 1e30f, 1e30f};
    float hi[3] = {-1e30f, -1e30f, -1e30f};
    bool any = false;
    unsigned int vertBase = 0;

    for (const MeshPiece& piece : pieces) {
        const int vcount = int(piece.interleaved.size() / 6);
        if (vcount == 0 || piece.indices.empty()) continue;
        any = true;

        const size_t idxStart = pendingIndices_.size();
        pendingInterleaved_.insert(pendingInterleaved_.end(),
                                   piece.interleaved.begin(),
                                   piece.interleaved.end());
        for (int i = 0; i < vcount; ++i)
            for (int k = 0; k < 3; ++k) {
                const float c = piece.interleaved[size_t(i) * 6 + k];
                lo[k] = std::min(lo[k], c);
                hi[k] = std::max(hi[k], c);
            }
        for (unsigned int idx : piece.indices)
            pendingIndices_.push_back(idx + vertBase);

        DrawRange r;
        r.count = int(piece.indices.size());
        r.byteOffset = idxStart * sizeof(unsigned int);
        r.color[0] = piece.color[0];
        r.color[1] = piece.color[1];
        r.color[2] = piece.color[2];
        pendingRanges_.push_back(r);
        vertBase += unsigned(vcount);
    }

    if (any) {
        for (int k = 0; k < 3; ++k) center_[k] = 0.5f * (lo[k] + hi[k]);
        radius_ = 0.0f;
        for (int k = 0; k < 3; ++k)
            radius_ = std::max(radius_, 0.5f * (hi[k] - lo[k]));
        if (radius_ <= 0.0f) radius_ = 1.0f;
    }

    pendingUpload_ = true;
    if (glReady_) {
        makeCurrent();
        uploadPending();
        doneCurrent();
    }
    if (any) resetView();
    update();
}

void MeshView::uploadPending() {
    pendingUpload_ = false;
    ranges_ = pendingRanges_;
    totalIndices_ = int(pendingIndices_.size());

    vao_.bind();
    vbo_.bind();
    vbo_.allocate(pendingInterleaved_.data(),
                  int(pendingInterleaved_.size() * sizeof(float)));
    ibo_.bind();
    ibo_.allocate(pendingIndices_.data(),
                  int(pendingIndices_.size() * sizeof(unsigned int)));

    program_.bind();
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          reinterpret_cast<void*>(3 * sizeof(float)));
    program_.release();
    vao_.release();
}

void MeshView::resizeGL(int w, int h) { glViewport(0, 0, w, h); }

void MeshView::resizeEvent(QResizeEvent* e) {
    QOpenGLWidget::resizeEvent(e);
    positionToolbar();
}

void MeshView::setFocusVoxel(int x, int y, int z, int width, int height, int depth,
                             float sx, float sy, float sz) {
    focusVoxel_[0] = x;
    focusVoxel_[1] = y;
    focusVoxel_[2] = z;
    focusDimensions_[0] = width;
    focusDimensions_[1] = height;
    focusDimensions_[2] = depth;
    focusSpacing_[0] = sx;
    focusSpacing_[1] = sy;
    focusSpacing_[2] = sz;
    update();
}

void MeshView::paintGL() {
    if (pendingUpload_) uploadPending();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const QMatrix4x4 proj = projMatrix();
    const QMatrix4x4 view = viewMatrix();
    lastMvp_ = proj * view;

    if (volumeRendering_ && volumeTexture_.isCreated()) {
        const QVector3D extent(float(volumeWidth_) * volumeSpacing_[0],
                               float(volumeHeight_) * volumeSpacing_[1],
                               float(volumeDepth_) * volumeSpacing_[2]);
        const QVector3D camera = view.inverted().map(QVector3D(0, 0, 0));
        const QVector3D cameraTex(camera.x() / std::max(extent.x(), 0.001f),
                                   camera.y() / std::max(extent.y(), 0.001f),
                                   camera.z() / std::max(extent.z(), 0.001f));
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_FRONT);
        volumeProgram_.bind();
        volumeProgram_.setUniformValue("uMvp", lastMvp_);
        volumeProgram_.setUniformValue("uVolumeMin", QVector3D(0, 0, 0));
        volumeProgram_.setUniformValue("uVolumeExtent", extent);
        volumeProgram_.setUniformValue("uCamera", cameraTex);
        volumeProgram_.setUniformValue("uStep", 1.0f / 256.0f);
        volumeProgram_.setUniformValue("uDensity", 0.08f);
        volumeTexture_.bind(0);
        volumeProgram_.setUniformValue("uVolume", 0);
        volumeVao_.bind();
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
        volumeVao_.release();
        volumeTexture_.release();
        volumeProgram_.release();
        glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
    }

    if (totalIndices_ > 0) {
        glEnable(GL_DEPTH_TEST);
        program_.bind();
        program_.setUniformValue("uMvp", lastMvp_);
        program_.setUniformValue("uModelView", view);
        vao_.bind();
        for (const DrawRange& r : ranges_) {
            program_.setUniformValue(
                "uColor", QVector3D(r.color[0], r.color[1], r.color[2]));
            glDrawElements(GL_TRIANGLES, r.count, GL_UNSIGNED_INT,
                           reinterpret_cast<void*>(r.byteOffset));
        }
        vao_.release();
        program_.release();
    }

    // 2D overlay: anatomical gnomon + in-progress scissor lasso.
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    drawGnomon(painter);
    drawFocus(painter);
    if (lasso_.size() >= 2) {
        painter.setPen(QPen(QColor(255, 220, 60), 2, Qt::DashLine));
        painter.setBrush(QColor(255, 220, 60, 40));
        QPolygonF poly(lasso_);
        painter.drawPolygon(poly);
    }
    drawMarkups(painter);
    painter.end();
}

void MeshView::drawFocus(QPainter& p) {
    if (focusDimensions_[0] <= 0 || focusDimensions_[1] <= 0 || focusDimensions_[2] <= 0)
        return;
    const float x = focusVoxel_[0] * focusSpacing_[0];
    const float y = focusVoxel_[1] * focusSpacing_[1];
    const float z = focusVoxel_[2] * focusSpacing_[2];
    const float maxX = std::max(0, focusDimensions_[0] - 1) * focusSpacing_[0];
    const float maxY = std::max(0, focusDimensions_[1] - 1) * focusSpacing_[1];
    const float maxZ = std::max(0, focusDimensions_[2] - 1) * focusSpacing_[2];
    const QVector3D center(x, y, z);
    const QVector3D ends[3][2] = {
        {QVector3D(0, y, z), QVector3D(maxX, y, z)},
        {QVector3D(x, 0, z), QVector3D(x, maxY, z)},
        {QVector3D(x, y, 0), QVector3D(x, y, maxZ)}
    };
    const QColor colors[3] = {Qt::red, Qt::green, Qt::blue};
    for (int axis = 0; axis < 3; ++axis) {
        QPointF a, b;
        if (!project(ends[axis][0], &a) || !project(ends[axis][1], &b)) continue;
        p.setPen(QPen(colors[axis], 1.5, Qt::SolidLine));
        p.drawLine(a, b);
    }
    QPointF c;
    if (project(center, &c)) {
        p.setPen(QPen(Qt::yellow, 1.5));
        p.setBrush(Qt::yellow);
        p.drawEllipse(c, 4.0, 4.0);
    }
}

// Project an mm point through the last frame's proj*view to widget pixels.
bool MeshView::project(const QVector3D& mm, QPointF* out) const {
    const QVector4D clip = lastMvp_ * QVector4D(mm, 1.0f);
    if (clip.w() <= 0.0001f) return false;  // behind the camera
    const float ndcX = clip.x() / clip.w();
    const float ndcY = clip.y() / clip.w();
    out->setX((ndcX * 0.5f + 0.5f) * width());
    out->setY((1.0f - (ndcY * 0.5f + 0.5f)) * height());
    return true;
}

void MeshView::drawMarkups(QPainter& p) {
    if (!markups_) return;
    auto dot = [&](const QPointF& s, const QColor& c, double r) {
        p.setPen(QPen(c.darker(140), 1.5));
        p.setBrush(c);
        p.drawEllipse(s, r, r);
    };
    for (const auto& m : markups_->markups()) {
        if (!m.visible) continue;
        const QColor col = markups_->color(m);
        std::vector<QPointF> pts;
        bool ok = true;
        for (const auto& v : m.voxels) {
            QPointF s;
            if (!project(markups_->mm(v), &s)) { ok = false; break; }
            pts.push_back(s);
        }
        if (!ok) continue;
        if (pts.size() == 3) {  // plane -> filled triangle
            QPolygonF tri({pts[0], pts[1], pts[2]});
            p.setPen(QPen(col, 2));
            p.setBrush(QColor(col.red(), col.green(), col.blue(), 60));
            p.drawPolygon(tri);
        } else if (pts.size() == 2) {  // line -> segment
            p.setPen(QPen(col, 2));
            p.drawLine(pts[0], pts[1]);
        }
        for (const QPointF& s : pts) dot(s, col, 4.5);
    }
    // In-progress (pending) points.
    const QColor pc = markups_->pendingColor();
    for (const auto& v : markups_->pending()) {
        QPointF s;
        if (project(markups_->mm(v), &s)) dot(s, pc, 4.0);
    }
}

void MeshView::drawGnomon(QPainter& p) {
    const int len = 30;
    const QPointF o(len + 18, height() - len - 18);  // bottom-left
    struct Axis { QVector3D dir; QColor col; const char* label; };
    const Axis axes[3] = {
        {{1, 0, 0}, QColor(230, 80, 80), "R"},    // X = Right
        {{0, 1, 0}, QColor(90, 210, 110), "A"},   // Y = Anterior
        {{0, 0, 1}, QColor(90, 160, 240), "S"},   // Z = Superior
    };
    QFont f = p.font();
    f.setBold(true);
    p.setFont(f);
    for (const Axis& a : axes) {
        const QVector3D d = rot_.mapVector(a.dir);
        const QPointF end(o.x() + d.x() * len, o.y() - d.y() * len);
        p.setPen(QPen(a.col, 2));
        p.drawLine(o, end);
        p.drawText(QRectF(end.x() - 7, end.y() - 8, 14, 16), Qt::AlignCenter,
                   a.label);
    }
}

void MeshView::setScissorMode(bool on) {
    scissorMode_ = on;
    lassoActive_ = false;
    lasso_.clear();
    setCursor(on ? Qt::CrossCursor : Qt::ArrowCursor);
    if (scissorBtn_ && scissorBtn_->isChecked() != on) {
        QSignalBlocker b(scissorBtn_);
        scissorBtn_->setChecked(on);
    }
    update();
}

void MeshView::clearLasso() {
    lasso_.clear();
    lassoActive_ = false;
    update();
}

void MeshView::mousePressEvent(QMouseEvent* e) {
    lastMouse_ = e->pos();
    if (scissorMode_ && e->button() == Qt::LeftButton) {
        lasso_.clear();
        lasso_.append(e->position());
        lassoActive_ = true;
        update();
    }
}

void MeshView::mouseMoveEvent(QMouseEvent* e) {
    if (scissorMode_) {
        if (lassoActive_ && (e->buttons() & Qt::LeftButton)) {
            const QPointF p = e->position();
            if (lasso_.isEmpty() ||
                QLineF(lasso_.last(), p).length() > 2.0)
                lasso_.append(p);
            update();
        }
        return;
    }
    if (!(e->buttons() & Qt::LeftButton)) return;
    const QPoint d = e->pos() - lastMouse_;
    lastMouse_ = e->pos();
    QMatrix4x4 dq;
    dq.rotate(d.x() * 0.4f, 0, 1, 0);
    dq.rotate(d.y() * 0.4f, 1, 0, 0);
    rot_ = dq * rot_;  // orbit in view space
    update();
}

void MeshView::mouseReleaseEvent(QMouseEvent* e) {
    if (scissorMode_ && lassoActive_ && e->button() == Qt::LeftButton) {
        lassoActive_ = false;
        if (lasso_.size() >= 3) emit scissorFinished(lasso_);
        update();
    }
}

void MeshView::mouseDoubleClickEvent(QMouseEvent*) { emit doubleClicked(); }

void MeshView::wheelEvent(QWheelEvent* e) {
    zoomBy(e->angleDelta().y() > 0 ? 0.9f : 1.1f);
}

}  // namespace lumenwin
