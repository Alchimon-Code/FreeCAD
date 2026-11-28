// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include "overlay.hpp"
#include "scene_cache.hpp"

#include <QOpenGLFunctions_4_5_Core>
#include <QOpenGLShaderProgram>

namespace pd {
// Owned by exactly one Viewport context. Destructor, initialize and draw require that context current.
class SceneRenderer final : protected QOpenGLFunctions_4_5_Core {
  public:
    SceneRenderer();
    ~SceneRenderer();
    bool initialize(QString& shaderLog);
    void invalidate(const Document& doc, std::span<const Change> packet, const SelectionManager& selection) {
        cache_.invalidate(doc, packet, selection);
    }
    void invalidateSelection(std::span<const EntityId> ids) { cache_.invalidateSelection(ids); }
    void draw(const Document&, const Camera2D&, double dpr, const SelectionManager&, const OverlayState&,
              bool darkTheme, const edit::Plan* ghost = nullptr, bool showGrid = true);

    void drawPage(const Document&, const Layout&, const Camera2D&, double dpr);
    void invalidatePage() { pageKey_.clear(); }

  private:
    struct Buffer {
        GLuint vao = 0, vbo = 0;
        GLsizei lines = 0, total = 0, fills = 0;
        Vec2 origin;
    };
    void upload(Buffer&, std::span<const RenderVertex>, std::size_t lineCount);
    void destroy(Buffer&);
    void drawGrid(const Camera2D&, double dpr, bool darkTheme);
    SceneCache cache_;
    QOpenGLShaderProgram stroke_, grid_;
    std::map<EntityId, Buffer> chunks_;
    GLuint gridArray_ = 0;
    Buffer overlay_, paper_;
    std::string pageKey_;
    std::uint64_t paperRevision_ = 0;
    double paperDpr_ = 0, paperScale_ = 0;
    Vec2 paperEye_, paperSize_;
    std::vector<RenderVertex> overlayVertices_;
};
} // namespace pd
