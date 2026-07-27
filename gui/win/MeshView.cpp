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
        vao_.destroy();
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
            "QToolButton{color:#e2e4ea;padding:3px 8px;border:none;font-size:13px;}"
            "QToolButton:hover{background:rgba(255,255,255,32);border-radius:4px;}"
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

    connect(mk("⤓ PNG", "Save a PNG snapshot"), &QToolButton::clicked, this,
            &MeshView::saveSnapshot);

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

    vao_.create();
    vbo_.create();
    ibo_.create();
    glReady_ = true;
    if (pendingUpload_) uploadPending();
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

void MeshView::paintGL() {
    if (pendingUpload_) uploadPending();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const QMatrix4x4 proj = projMatrix();
    const QMatrix4x4 view = viewMatrix();
    lastMvp_ = proj * view;

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
    if (lasso_.size() >= 2) {
        painter.setPen(QPen(QColor(255, 220, 60), 2, Qt::DashLine));
        painter.setBrush(QColor(255, 220, 60, 40));
        QPolygonF poly(lasso_);
        painter.drawPolygon(poly);
    }
    drawMarkups(painter);
    painter.end();
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

void MeshView::wheelEvent(QWheelEvent* e) {
    zoomBy(e->angleDelta().y() > 0 ? 0.9f : 1.1f);
}

}  // namespace lumenwin
