// Interactive 3D surface viewer for the marching-cubes mesh.
//
// Chosen as a QOpenGLWidget (core-profile 3.3) rather than Qt Quick 3D: it
// consumes the bridge's raw interleaved vertex/normal float buffers directly with
// no QML/Quick3D dependency or scene-graph marshaling, and matches the immediate,
// data-oriented style of the rest of the app. Left-drag orbits, wheel zooms.
//
// The surface is a set of colored sub-meshes — one per visible segment — packed
// into a single vertex/index buffer and drawn as ranges, each with its own colour
// uniform. A floating toolbar offers reset/zoom/standard-views/snapshot, and a
// small R/A/S gnomon shows the anatomical orientation (X=Right, Y=Anterior,
// Z=Superior).
#pragma once

#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QPoint>
#include <QOpenGLWidget>
#include <vector>

class QWidget;

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

    // Anatomical standard views. Axes: X=Right, Y=Anterior, Z=Superior.
    enum class StdView { Anterior, Posterior, Left, Right, Superior, Inferior };

    explicit MeshView(QWidget* parent = nullptr);
    ~MeshView() override;

    // Replace the whole surface with these colored pieces (recentres/rescales to
    // fit). Passing an empty list clears the view.
    void setMeshes(std::vector<MeshPiece> pieces);
    void clearMeshes() { setMeshes({}); }
    bool hasMesh() const { return totalIndices_ > 0; }

    // The model-view-projection matrix used for the last frame, and the viewport
    // size — exposed for screen-space operations (e.g. the scissor lasso).
    QMatrix4x4 lastMvp() const { return lastMvp_; }

public slots:
    void resetView();                    // reframe to bounds + default orientation
    void zoomBy(float factor);           // <1 zooms in, >1 out
    void setStandardView(StdView view);  // camera on an anatomical axis
    void saveSnapshot();                 // file dialog + grabFramebuffer

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void resizeEvent(QResizeEvent*) override;
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
    void buildToolbar();
    void positionToolbar();
    void drawGnomon(class QPainter& p);
    QMatrix4x4 viewMatrix() const;
    QMatrix4x4 projMatrix() const;

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

    // Camera: orbit rotation (world -> view) + distance, orbiting the mesh centre.
    float center_[3] = {0, 0, 0};
    float radius_ = 1.0f;
    QMatrix4x4 rot_;
    float dist_ = 3.0f;  // world units
    QPoint lastMouse_;

    QMatrix4x4 lastMvp_;
    QWidget* toolbar_ = nullptr;
};

}  // namespace lumenwin
