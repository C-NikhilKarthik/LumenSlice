// Interactive 3D surface viewer for the marching-cubes mesh.
//
// Chosen as a QOpenGLWidget (core-profile 3.3) rather than Qt Quick 3D: it
// consumes the bridge's raw interleaved vertex/normal float buffers directly with
// no QML/Quick3D dependency or scene-graph marshaling, and matches the immediate,
// data-oriented style of the rest of the app. Left-drag orbits, wheel zooms.
//
// The surface is a set of colored sub-meshes — one per visible segment — packed
// into a single vertex/index buffer and drawn as ranges, each with its own colour
// uniform. MainWindow builds the pieces (one snapshot_label -> generate -> readback
// per segment, off the UI thread) and hands them over via setMeshes().
#pragma once

#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QPoint>
#include <QOpenGLWidget>
#include <vector>

namespace lumenwin {

class MeshView : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT
public:
    // One colored surface: interleaved x,y,z,nx,ny,nz vertices + triangle indices.
    struct MeshPiece {
        std::vector<float> interleaved;
        std::vector<unsigned int> indices;
        float color[3] = {0.82f, 0.80f, 0.78f};
    };

    explicit MeshView(QWidget* parent = nullptr);
    ~MeshView() override;

    // Replace the whole surface with these colored pieces (recentres/rescales to
    // fit). Passing an empty list clears the view.
    void setMeshes(std::vector<MeshPiece> pieces);
    void clearMeshes() { setMeshes({}); }
    bool hasMesh() const { return totalIndices_ > 0; }

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;

private:
    struct DrawRange {
        int count = 0;
        size_t byteOffset = 0;
        float color[3] = {0.8f, 0.8f, 0.8f};
    };

    void uploadPending();

    QOpenGLShaderProgram program_;
    QOpenGLVertexArrayObject vao_;
    QOpenGLBuffer vbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer ibo_{QOpenGLBuffer::IndexBuffer};
    bool glReady_ = false;

    // Concatenated CPU-side buffers (setMeshes may run before the context exists).
    std::vector<float> pendingInterleaved_;
    std::vector<unsigned int> pendingIndices_;
    std::vector<DrawRange> pendingRanges_;
    bool pendingUpload_ = false;

    std::vector<DrawRange> ranges_;
    int totalIndices_ = 0;

    // Camera / model framing.
    float center_[3] = {0, 0, 0};
    float radius_ = 1.0f;
    float yaw_ = 0.6f;
    float pitch_ = 0.4f;
    float distance_ = 3.0f;  // in units of radius_
    QPoint lastMouse_;
};

}  // namespace lumenwin
