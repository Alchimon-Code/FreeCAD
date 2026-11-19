// SPDX-License-Identifier: LGPL-2.1-or-later
#include "scene_renderer.hpp"
#include "qt_text.hpp"
#include "tessellation.hpp"
#include <planedraft/drafting.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>

// Also pulls the resource object out of the static library.
static void initShaderResources() {
    [[maybe_unused]] static const bool registered = [] {
        Q_INIT_RESOURCE(pd_viewport_shaders);
        return true;
    }();
}

namespace pd {
SceneRenderer::SceneRenderer() : cache_(qtTextStrokes) {}
bool SceneRenderer::initialize(QString& shaderLog) {
    if (!initializeOpenGLFunctions()) {
        return false;
    }
    initShaderResources();
    auto link = [&](QOpenGLShaderProgram& program, const QString& name) {
        const auto base = ":/pd/shaders/" + name;
        const bool ok = program.addShaderFromSourceFile(QOpenGLShader::Vertex, base + ".vert") &&
                        program.addShaderFromSourceFile(QOpenGLShader::Fragment, base + ".frag") &&
                        program.link();
        if (!ok) {
            shaderLog = program.log();
        }
        return ok;
    };
    if (!link(stroke_, "stroke") || !link(grid_, "grid")) {
        return false;
    }
    glGenVertexArrays(1, &gridArray_); // core profile needs a VAO even for gl_VertexID
    return true;
}
SceneRenderer::~SceneRenderer() {
    if (!gridArray_) {
        return;
    }
    for (auto& [key, chunk] : chunks_) {
        destroy(chunk);
    }
    destroy(overlay_);
    destroy(paper_);
    glDeleteVertexArrays(1, &gridArray_);
}
void SceneRenderer::destroy(Buffer& b) {
    glDeleteVertexArrays(1, &b.vao);
    glDeleteBuffers(1, &b.vbo);
    b = {};
}
void SceneRenderer::upload(Buffer& b, std::span<const RenderVertex> data, std::size_t lines) {
    if (!b.vao) {
        glGenVertexArrays(1, &b.vao);
        glGenBuffers(1, &b.vbo);
        glBindVertexArray(b.vao);
        glBindBuffer(GL_ARRAY_BUFFER, b.vbo);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(RenderVertex), nullptr);
        glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(RenderVertex),
                              reinterpret_cast<const void*>(offsetof(RenderVertex, ink)));
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glBindVertexArray(0);
    }
    glBindBuffer(GL_ARRAY_BUFFER, b.vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(data.size_bytes()), data.data(), GL_DYNAMIC_DRAW);
    b.lines = static_cast<GLsizei>(lines);
    b.total = static_cast<GLsizei>(data.size());
}
void SceneRenderer::drawGrid(const Camera2D& camera, double dpr, bool darkTheme) {
    // 1/2/5 decades: minor lines stay readable, phase never carries a large absolute world coordinate.
    const double desired = 24.0 / camera.scale();
    const double decade = std::pow(10., std::floor(std::log10(desired)));
    const double step = desired <= decade       ? decade
                        : desired <= 2 * decade ? 2 * decade
                        : desired <= 5 * decade ? 5 * decade
                                                : 10 * decade;
    const double px = camera.scale() * dpr;
    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp); // actual widget FBO, including fractional DPI rounding
    grid_.bind();
    glUniform2f(0, vp[0] + vp[2] * 0.5f, vp[1] + vp[3] * 0.5f);
    glUniform2f(1, static_cast<float>(std::remainder(camera.eye().x, step * 5) * px),
                static_cast<float>(std::remainder(camera.eye().y, step * 5) * px));
    glUniform1f(2, static_cast<float>(step * px));
    const double limit = std::max(vp[2], vp[3]) + 4.0;
    glUniform2f(3, static_cast<float>(std::clamp(-camera.eye().x * px, -limit, limit)),
                static_cast<float>(std::clamp(-camera.eye().y * px, -limit, limit)));
    if (darkTheme) {
        glUniform3f(4, 0.071f, 0.086f, 0.102f);
    } else {
        glUniform3f(4, 0.125f, 0.153f, 0.184f);
    } // white ByLayer ink remains visible in either UI theme
    glBindVertexArray(gridArray_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}
void SceneRenderer::draw(const Document& doc, const Camera2D& camera, double dpr,
                         const SelectionManager& selection, const OverlayState& overlay, bool darkTheme,
                         const edit::Plan* ghost, bool showGrid) {
    cache_.sync(
        doc, camera, dpr, selection,
        [&](auto key, Vec2 origin, auto data, auto lines, auto fills) {
            auto& chunk = chunks_[key];
            chunk.origin = origin;
            upload(chunk, data, lines);
            chunk.fills = static_cast<GLsizei>(fills);
        },
        [&](auto key) {
            if (auto node = chunks_.extract(key); !node.empty()) {
                destroy(node.mapped());
            }
        });
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    if (showGrid)
        drawGrid(camera, dpr, darkTheme);
    else {
        if (darkTheme)
            glClearColor(.071f, .086f, .102f, 1);
        else
            glClearColor(.125f, .153f, .184f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    stroke_.bind();
    glUniform1f(3, static_cast<float>(7 * dpr));
    auto bindChunk = [&](const Buffer& chunk) {
        glUniformMatrix3fv(0, 1, GL_FALSE, camera.clipMatrix(chunk.origin).data());
        glBindVertexArray(chunk.vao);
    };
    for (auto& [_, chunk] : chunks_)
        if (chunk.fills) {
            bindChunk(chunk);
            glDrawArrays(GL_TRIANGLES, chunk.lines, chunk.fills);
        }
    for (const auto& [key, chunk] : chunks_) {
        bindChunk(chunk);
        glDrawArrays(GL_LINES, 0, chunk.lines);
    }
    // Overlay: depth stays off. Selected grips use the same RTE origin, but fixed pixel size.
    glEnable(GL_PROGRAM_POINT_SIZE);
    for (const auto& [key, chunk] : chunks_) {
        if (chunk.total > chunk.lines + chunk.fills) {
            bindChunk(chunk);
            glDrawArrays(GL_POINTS, chunk.lines + chunk.fills, chunk.total - chunk.lines - chunk.fills);
        }
    }
    glDisable(GL_PROGRAM_POINT_SIZE);
    const auto triangles = buildOverlay(overlayVertices_, camera, overlay, ghost, qtTextStrokes, dpr);
    if (!overlayVertices_.empty()) {
        upload(overlay_, overlayVertices_, 0);
        const float pixels[]{static_cast<float>(2 / camera.size().x),
                             0,
                             0,
                             0,
                             static_cast<float>(-2 / camera.size().y),
                             0,
                             -1,
                             1,
                             1};
        glUniformMatrix3fv(0, 1, GL_FALSE, pixels);
        glBindVertexArray(overlay_.vao);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(triangles));
        glDrawArrays(GL_LINES, static_cast<GLint>(triangles),
                     overlay_.total - static_cast<GLsizei>(triangles));
        glDisable(GL_BLEND);
    }
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    stroke_.release();
}
void SceneRenderer::drawPage(const Document& doc, const Layout& layout, const Camera2D& camera, double dpr) {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glClearColor(.24f, .26f, .28f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    if (pageKey_ != layout.name || paperRevision_ != doc.revision() || paperEye_ != camera.eye() ||
        paperSize_ != camera.size() || paperScale_ != camera.scale() || paperDpr_ != dpr) {
        std::vector<RenderVertex> fill, lines;
        auto view = camera.viewBounds(1);
        auto vertex = [&](auto& list, Vec2 p, Ink ink) {
            auto v = Camera2D::relativeToEye(p, camera.eye());
            list.push_back({v[0], v[1], ink});
        };
        auto rect = [&](Vec2 a, Vec2 b, Ink ink) {
            for (auto p : {a, Vec2{b.x, a.y}, b, a, b, Vec2{a.x, b.y}})
                vertex(fill, p, ink);
        };
        auto size = layout.size();
        Vec2 shadow{4 / camera.scale(), -4 / camera.scale()};
        rect(shadow, size + shadow, {44, 46, 48});
        rect({}, size, {255, 255, 255});
        visitPage(doc, layout,
                  [&](const Entity& e, const StrokeStyle& s, LayerId, const BoundingBox2D& clip) {
                      if (!clip.intersects(view) || !clip.intersects(e.getBounds()))
                          return;
                      BoundingBox2D box(
                          {std::max(clip.min().x, view.min().x), std::max(clip.min().y, view.min().y)},
                          {std::min(clip.max().x, view.max().x), std::min(clip.max().y, view.max().y)});
                      auto c = s.color == Color{} ? Color{0, 0, 0} : s.color;
                      Ink ink{c.r, c.g, c.b};
                      if (auto face = dynamic_cast<const SolidFace*>(&e))
                          clipFace(*face, box, [&](Vec2 p) { vertex(fill, p, ink); });
                      else
                          visitStrokes(
                              e,
                              [&](const Stroke& st) {
                                  tessellate(st, camera.scale() * dpr, box, [&](Segment line) {
                                      vertex(lines, line.a, ink);
                                      vertex(lines, line.b, ink);
                                  });
                              },
                              qtTextStrokes);
                  });
        auto triangles = fill.size();
        fill.insert(fill.end(), lines.begin(), lines.end());
        upload(paper_, fill, lines.size());
        paper_.fills = static_cast<GLsizei>(triangles);
        pageKey_ = layout.name;
        paperRevision_ = doc.revision();
        paperEye_ = camera.eye();
        paperSize_ = camera.size();
        paperScale_ = camera.scale();
        paperDpr_ = dpr;
    }
    stroke_.bind();
    glUniformMatrix3fv(0, 1, GL_FALSE, camera.clipMatrix(camera.eye()).data());
    glBindVertexArray(paper_.vao);
    glDrawArrays(GL_TRIANGLES, 0, paper_.fills);
    glDrawArrays(GL_LINES, paper_.fills, paper_.lines);
    glBindVertexArray(0);
    stroke_.release();
}
} // namespace pd
