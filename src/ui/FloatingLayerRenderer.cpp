#include "ui/FloatingLayerRenderer.h"

#include <QMatrix3x3>
#include <QPolygonF>
#include <QTransform>
#include <QVector2D>
#include <QVector4D>

#include <algorithm>
#include <cmath>

namespace doodlebold {

namespace {

const char* kVertexShader = R"(#version 330 core
uniform mat4 docToClip;
uniform vec4 bounds;  // x0, y0, x1, y1 of the rectangle to cover, in document space
out vec2 docPos;
void main() {
    vec2 t = vec2(gl_VertexID & 1, gl_VertexID >> 1);
    docPos = mix(bounds.xy, bounds.zw, t);
    gl_Position = docToClip * vec4(docPos, 0.0, 1.0);
})";

// X(u, v) = a + u e + v f + u v g maps the unit square onto the quad. Inverted per fragment like QuadMap::inverse.
const char* kFragmentShader = R"(#version 330 core
in vec2 docPos;
uniform sampler2D source;
uniform vec2 a;
uniform vec2 e;
uniform vec2 f;
uniform vec2 g;
uniform mat3 projective;
uniform float perspective;
uniform vec2 docSize;
uniform float opacity;
out vec4 color;

float cross2(vec2 p, vec2 q) { return p.x * q.y - p.y * q.x; }

bool inverseMap(vec2 p, out vec2 uv) {
    vec2 h = p - a;
    float k2 = cross2(g, f);
    float k1 = cross2(e, f) + cross2(h, g);
    float k0 = cross2(h, e);
    float scale = max(1e-6, length(e) * length(f));
    float roots[2];
    int n = 0;
    if (abs(k2) < 1e-5 * scale) {  // a parallelogram: linear
        if (abs(k1) < 1e-8 * scale)
            return false;
        roots[0] = -k0 / k1;
        n = 1;
    } else {
        float disc = k1 * k1 - 4.0 * k0 * k2;
        if (disc < 0.0)
            return false;
        float q = -0.5 * (k1 + (k1 >= 0.0 ? 1.0 : -1.0) * sqrt(disc));  // no cancellation
        roots[0] = q / k2;
        roots[1] = abs(q) > 1e-20 ? k0 / q : roots[0];
        n = 2;
    }
    float best = 1e30;
    bool found = false;
    uv = vec2(0.5);
    for (int i = 0; i < n; ++i) {
        float v = roots[i];
        vec2 d = e + g * v;
        if (max(abs(d.x), abs(d.y)) < 1e-6)
            continue;
        float u = abs(d.x) >= abs(d.y) ? (h.x - f.x * v) / d.x : (h.y - f.y * v) / d.y;
        float outside = max(0.0, max(-u, u - 1.0)) + max(0.0, max(-v, v - 1.0));
        if (outside < best) {
            best = outside;
            uv = vec2(u, v);
            found = true;
        }
    }
    if (!found)
        return false;
    if (perspective > 0.0) {
        vec3 t = projective * vec3(p, 1.0);
        uv += (t.xy / t.z - uv) * perspective;
    }
    return true;
}

void main() {
    vec2 uv;
    float cov = 1.0;
    if (!inverseMap(docPos, uv)) {
        uv = vec2(0.5);
        cov = 0.0;
    }
    vec2 fw = max(fwidth(uv), vec2(1e-6));
    vec2 edge = clamp(min(uv, 1.0 - uv) / fw + 0.5, 0.0, 1.0);  // anti-aliased border of the quad
    cov *= edge.x * edge.y;
    if (docPos.x < 0.0 || docPos.y < 0.0 || docPos.x > docSize.x || docPos.y > docSize.y)
        cov = 0.0;  // results are clipped to the canvas
    vec4 c = textureGrad(source, clamp(uv, 0.0, 1.0), dFdx(uv), dFdy(uv));
    color = c * (cov * opacity);
})";

}

FloatingLayerRenderer::~FloatingLayerRenderer() = default;

bool FloatingLayerRenderer::initialize() {
    if (m_ready)
        return true;
    initializeOpenGLFunctions();
    m_program = std::make_unique<QOpenGLShaderProgram>();
    if (!m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader) ||
        !m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader) || !m_program->link()) {
        m_log = m_program->log();
        m_program.reset();
        return false;
    }
    m_vao.create();
    m_ready = true;
    return true;
}

void FloatingLayerRenderer::release(bool glAlive) {
    if (glAlive)
        unload();
    m_owner = nullptr;
    m_below = m_above = m_source = 0;
    if (glAlive && m_ready)
        m_vao.destroy();
    m_program.reset();  // Qt's resource guard frees the program in its own context, or not at all once that is gone
    m_ready = false;
}

GLuint FloatingLayerRenderer::upload(QOpenGLFunctions& gl, const QImage& image) {
    if (image.isNull())
        return 0;
    GLuint tex = 0;
    gl.glGenTextures(1, &tex);
    gl.glBindTexture(GL_TEXTURE_2D, tex);
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl.glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    gl.glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(image.bytesPerLine() / 4));
    gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image.width(), image.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, image.constBits());
    gl.glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    gl.glGenerateMipmap(GL_TEXTURE_2D);  // zoomed-out views and shrinking transforms read the smaller levels
    return tex;
}

void FloatingLayerRenderer::load(const void* owner, const FloatingPreview& preview) {
    unload();
    m_owner = owner;
    m_docSize = preview.below.size();
    m_box = preview.box;
    m_opacity = preview.opacity;
    m_below = upload(*this, preview.below);
    m_above = upload(*this, preview.above);
    m_source = upload(*this, preview.source);
}

void FloatingLayerRenderer::unload() {
    for (GLuint* t : {&m_below, &m_above, &m_source}) {
        if (*t)
            glDeleteTextures(1, t);
        *t = 0;
    }
    m_owner = nullptr;
}

void FloatingLayerRenderer::drawFloating(const QMatrix4x4& docToClip, const transform::Quad& quad, double perspective,
                                         transform::Interpolation) {
    if (!m_ready || !m_source || m_opacity <= 0.0)
        return;
    using transform::Quad;
    const QPointF a = quad.corner[0], e = quad.corner[1] - quad.corner[0], f = quad.corner[3] - quad.corner[0];
    const QPointF g = quad.corner[0] - quad.corner[1] + quad.corner[2] - quad.corner[3];
    perspective = std::clamp(perspective, 0.0, 1.0);
    QTransform projective;
    if (perspective > 0.0 &&
        !QTransform::quadToSquare(QPolygonF({quad.corner[0], quad.corner[1], quad.corner[2], quad.corner[3]}), projective))
        perspective = 0.0;

    double x0 = a.x(), x1 = a.x(), y0 = a.y(), y1 = a.y();
    for (const QPointF& p : quad.corner) {
        x0 = std::min(x0, p.x()), x1 = std::max(x1, p.x());
        y0 = std::min(y0, p.y()), y1 = std::max(y1, p.y());
    }
    x0 = std::max(0.0, std::floor(x0) - 2), y0 = std::max(0.0, std::floor(y0) - 2);
    x1 = std::min<double>(m_docSize.width(), std::ceil(x1) + 2), y1 = std::min<double>(m_docSize.height(), std::ceil(y1) + 2);
    if (x1 <= x0 || y1 <= y0)
        return;

    const QMatrix3x3 proj = [&] {
        const float v[9] = {float(projective.m11()), float(projective.m21()), float(projective.m31()),
                            float(projective.m12()), float(projective.m22()), float(projective.m32()),
                            float(projective.m13()), float(projective.m23()), float(projective.m33())};
        return QMatrix3x3(v);
    }();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_source);
    // TODO(reimplement): the preview should follow the Transform tool's quality options. For now it is nearest neighbour, as the result is.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    m_program->bind();
    m_program->setUniformValue("docToClip", docToClip);
    m_program->setUniformValue("bounds", QVector4D(float(x0), float(y0), float(x1), float(y1)));
    m_program->setUniformValue("source", 0);
    m_program->setUniformValue("a", QVector2D(float(a.x()), float(a.y())));
    m_program->setUniformValue("e", QVector2D(float(e.x()), float(e.y())));
    m_program->setUniformValue("f", QVector2D(float(f.x()), float(f.y())));
    m_program->setUniformValue("g", QVector2D(float(g.x()), float(g.y())));
    m_program->setUniformValue("projective", proj);
    m_program->setUniformValue("perspective", float(perspective));
    m_program->setUniformValue("docSize", QVector2D(float(m_docSize.width()), float(m_docSize.height())));
    m_program->setUniformValue("opacity", float(m_opacity));
    m_vao.bind();
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    m_vao.release();
    m_program->release();
}

}
