#pragma once

#include "core/document/FloatingPreview.h"
#include "core/transform/Resample.h"

#include <QMatrix4x4>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>

#include <memory>
#include <QOpenGLVertexArrayObject>
#include <QSize>

namespace doodlebold {

// The fragment shader inverts the same bilinear map as the CPU resampler (core/transform/Resample.cpp,
// QuadMap::inverse) and samples with GL filtering; the quad's edge is anti-aliased over one screen pixel. The layer's
// blend mode is approximated as Normal. All functions need the canvas' GL context to be current.
class FloatingLayerRenderer : protected QOpenGLFunctions {
public:
    FloatingLayerRenderer() = default;
    ~FloatingLayerRenderer();

    bool initialize();
    bool isReady() const { return m_ready; }
    const QString& log() const { return m_log; }
    // Frees all GL objects (the context may be gone: pass false to only forget them).
    void release(bool glAlive = true);

    void load(const void* owner, const FloatingPreview& preview);
    void unload();
    bool isLoadedFor(const void* owner) const { return m_owner == owner && m_owner != nullptr; }

    GLuint belowTexture() const { return m_below; }
    GLuint aboveTexture() const { return m_above; }

    void drawFloating(const QMatrix4x4& docToClip, const transform::Quad& quad, double perspective, transform::Interpolation quality);

private:
    static GLuint upload(QOpenGLFunctions& gl, const QImage& image);

    bool m_ready = false;
    QString m_log;
    std::unique_ptr<QOpenGLShaderProgram> m_program;
    QOpenGLVertexArrayObject m_vao;
    const void* m_owner = nullptr;
    GLuint m_below = 0;
    GLuint m_above = 0;
    GLuint m_source = 0;
    QSize m_docSize;
    QRect m_box;
    double m_opacity = 1.0;
};

}
