#include "MeshView.h"

#include <QMatrix4x4>
#include <QMouseEvent>
#include <QSurfaceFormat>
#include <QVector3D>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

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
    // Head-light: light travels with the camera.
    vec3 l = normalize(-vPosView);
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

void MeshView::setMesh(LumenVolume* v) {
    pendingInterleaved_.clear();
    pendingIndices_.clear();

    if (v) {
        const int vcount = lumen_mesh_vertex_count(v);
        const int icount = lumen_mesh_index_count(v);
        const float* verts = lumen_mesh_vertices(v);
        const float* norms = lumen_mesh_normals(v);
        const unsigned int* idx = lumen_mesh_indices(v);
        if (vcount > 0 && icount > 0 && verts && idx) {
            pendingInterleaved_.resize(size_t(vcount) * 6);
            float lo[3] = {1e30f, 1e30f, 1e30f};
            float hi[3] = {-1e30f, -1e30f, -1e30f};
            for (int i = 0; i < vcount; ++i) {
                float* dst = &pendingInterleaved_[size_t(i) * 6];
                for (int k = 0; k < 3; ++k) {
                    const float c = verts[size_t(i) * 3 + k];
                    dst[k] = c;
                    lo[k] = std::min(lo[k], c);
                    hi[k] = std::max(hi[k], c);
                }
                for (int k = 0; k < 3; ++k)
                    dst[3 + k] = norms ? norms[size_t(i) * 3 + k] : 0.0f;
            }
            for (int k = 0; k < 3; ++k) center_[k] = 0.5f * (lo[k] + hi[k]);
            radius_ = 0.0f;
            for (int k = 0; k < 3; ++k)
                radius_ = std::max(radius_, 0.5f * (hi[k] - lo[k]));
            if (radius_ <= 0.0f) radius_ = 1.0f;

            pendingIndices_.assign(idx, idx + icount);
        }
    }

    pendingUpload_ = true;
    if (glReady_) {
        makeCurrent();
        uploadPending();
        doneCurrent();
    }
    update();
}

void MeshView::uploadPending() {
    pendingUpload_ = false;
    indexCount_ = int(pendingIndices_.size());

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

void MeshView::paintGL() {
    if (pendingUpload_) uploadPending();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (indexCount_ == 0) return;

    const float aspect = height() > 0 ? float(width()) / float(height()) : 1.0f;
    QMatrix4x4 proj;
    proj.perspective(40.0f, aspect, 0.05f * radius_, 100.0f * radius_);

    QMatrix4x4 view;
    view.translate(0, 0, -distance_ * radius_);
    view.rotate(pitch_ * 57.2958f, 1, 0, 0);
    view.rotate(yaw_ * 57.2958f, 0, 1, 0);

    QMatrix4x4 model;
    model.translate(-center_[0], -center_[1], -center_[2]);

    const QMatrix4x4 modelView = view * model;
    const QMatrix4x4 mvp = proj * modelView;

    program_.bind();
    program_.setUniformValue("uMvp", mvp);
    program_.setUniformValue("uModelView", modelView);
    program_.setUniformValue("uColor", QVector3D(0.85f, 0.83f, 0.80f));
    vao_.bind();
    glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);
    vao_.release();
    program_.release();
}

void MeshView::mousePressEvent(QMouseEvent* e) { lastMouse_ = e->pos(); }

void MeshView::mouseMoveEvent(QMouseEvent* e) {
    if (!(e->buttons() & Qt::LeftButton)) return;
    const QPoint d = e->pos() - lastMouse_;
    lastMouse_ = e->pos();
    yaw_ += d.x() * 0.01f;
    pitch_ += d.y() * 0.01f;
    pitch_ = std::clamp(pitch_, -1.55f, 1.55f);
    update();
}

void MeshView::wheelEvent(QWheelEvent* e) {
    const float factor = (e->angleDelta().y() > 0) ? 0.9f : 1.1f;
    distance_ = std::clamp(distance_ * factor, 1.2f, 40.0f);
    update();
}

}  // namespace lumenwin
