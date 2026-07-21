// Interactive 3D surface viewer for the marching-cubes mesh.
//
// Chosen as a QOpenGLWidget (core-profile 3.3) rather than Qt Quick 3D: it
// consumes the bridge's raw interleaved vertex/normal float buffers directly with
// no QML/Quick3D dependency or scene-graph marshaling, and matches the immediate,
// data-oriented style of the rest of the app. Left-drag orbits, wheel zooms.
#pragma once

#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPoint>
#include <vector>

#include "lumen_bridge.h"

namespace lumenwin {

class MeshView : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT
public:
    explicit MeshView(QWidget* parent = nullptr);
    ~MeshView() override;

    // Upload the mesh currently held by the volume (vertices/normals/indices).
    // Pass nullptr or an empty mesh to clear. Recentres/rescales to fit the view.
    void setMesh(LumenVolume* v);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;

private:
    QOpenGLShaderProgram program_;
    QOpenGLVertexArrayObject vao_;
    QOpenGLBuffer vbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer ibo_{QOpenGLBuffer::IndexBuffer};
    int indexCount_ = 0;
    bool glReady_ = false;

    // Pending CPU-side buffers uploaded on the next paint (setMesh may be called
    // before the context exists).
    std::vector<float> pendingInterleaved_;  // x,y,z,nx,ny,nz per vertex
    std::vector<unsigned int> pendingIndices_;
    bool pendingUpload_ = false;

    // Camera / model framing.
    float center_[3] = {0, 0, 0};
    float radius_ = 1.0f;
    float yaw_ = 0.6f;
    float pitch_ = 0.4f;
    float distance_ = 3.0f;  // in units of radius_
    QPoint lastMouse_;

    void uploadPending();
};

}  // namespace lumenwin
