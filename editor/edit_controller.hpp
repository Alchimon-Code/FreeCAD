// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <QObject>
#include <optional>
#include <planedraft/edit.hpp>

// One interaction state machine for the console AND the viewport. No widget, GL, pixels,
// event loop, transient document edits, or borrowed model geometry in this class.
class EditController final : public QObject {
    Q_OBJECT
  public:
    enum class Tool {
        None,
        Line,
        Move,
        Copy,
        Rotate,
        Scale,
        Mirror,
        Trim,
        Extend,
        Offset,
        Fillet,
        Erase,
        Grip,
        DimLinear,
        DimAligned,
        DimRadius,
        DimDiameter,
        DimAngular,
        Hatch,
        Block,
        Insert,
        Explode,
        Layout,
        DimStyle,
        Circle,
        Arc,
        Polyline,
        Rectangle,
        Polygon,
        Chamfer,
        Array,
        MatchProp,
        Distance,
        Area
    };
    enum class Step {
        Idle,
        Select,
        Base,
        Destination,
        Angle,
        Factor,
        Reference,
        Axis,
        Target,
        Distance,
        Side,
        Radius,
        FirstLine,
        SecondLine,
        Hot,
        Definition,
        HatchOptions,
        BlockName,
        BlockBase,
        InsertSpec,
        Insertion,
        LayoutSpec,
        StyleSpec,
        Sides,
        ChamferDistance,
        ArraySpec
    };
    explicit EditController(pd::Document& d)
        : doc_(d), subscription_(d.observe([this](auto changes) {
              if (changes.front().kind == pd::Change::Kind::Reset || !doc_.layer(layer_))
                  layer_ = pd::DefaultLayer;
              if (active())
                  cancel();
          })) {}
    struct NamedTool {
        const char* global;
        Tool tool;
    };
    static std::span<const NamedTool> vocabulary();
    // The AutoCAD forms: "x,y" is absolute, "d<a" is polar from the origin, "@dx,dy" and "@d<a" are
    // offsets from the last point, which only the current gesture knows.
    static std::optional<pd::Vec2> coordinate(const QString& text,
                                              std::optional<pd::Vec2> base = std::nullopt);
    pd::edit::Tracked track(pd::Vec2 base, pd::Vec2 candidate) const;
    static std::optional<Tool> named(const QString& global);
    [[nodiscard]] Step step() const { return step_; }
    [[nodiscard]] bool active() const { return step_ != Step::Idle; }
    // Steps whose answer is free text: Space must stay a literal character there.
    [[nodiscard]] bool expectsFreeText() const {
        switch (step_) {
        case Step::BlockName:
        case Step::LayoutSpec:
        case Step::StyleSpec:
        case Step::InsertSpec:
        case Step::HatchOptions:
            return true;
        default:
            return false;
        }
    }
    [[nodiscard]] bool pickingEntity() const {
        return step_ == Step::Target || step_ == Step::FirstLine || step_ == Step::SecondLine;
    }
    [[nodiscard]] QString prompt() const;
    QString start(Tool, std::vector<pd::EntityId> preselected);
    QString setDimStyle(const QString&);
    QString beginGrip(pd::EntityId, std::size_t index);
    QString cancel();
    // Unrecognised input may instead be a new command. Valid phase keywords take precedence.
    std::optional<QString> tryAnswer(const QString&, std::vector<pd::EntityId> selection);
    QString enter(std::vector<pd::EntityId> selection);
    QString pick(pd::Vec2, std::optional<pd::EntityId> hit = {});
    void hover(std::optional<pd::Vec2> osnapOrCursor, std::optional<pd::EntityId> hit = {});
    [[nodiscard]] const pd::edit::Plan* ghost() const { return ghost_ ? &*ghost_ : nullptr; }
    [[nodiscard]] std::optional<pd::Vec2> cursorPoint() const { return cursor_; }
    [[nodiscard]] std::optional<pd::Vec2> guideBase() const { return guided_ ? base_ : std::nullopt; }
    [[nodiscard]] pd::EntityId hotId() const { return step_ == Step::Hot ? first_ : 0; }
    [[nodiscard]] std::optional<pd::Vec2> hotPoint() const {
        return step_ == Step::Hot ? (cursor_ ? cursor_ : base_) : std::nullopt;
    }
    pd::LayerId activeLayer() const { return layer_; }
    void setActiveLayer(pd::LayerId layer) {
        if (layer_ == layer)
            return;
        if (!doc_.layer(layer))
            throw std::invalid_argument("unknown layer");
        if (active())
            cancel();
        layer_ = layer;
        emit activeLayerChanged();
    }
    void setTracking(pd::edit::Tracking);
    void setPolarDegrees(int);
    [[nodiscard]] pd::edit::Tracking tracking() const { return mode_; }
    [[nodiscard]] int polarDegrees() const { return degrees_; }
    // The base point of the current gesture, for dynamic input and typed coordinates.
    [[nodiscard]] std::optional<pd::Vec2> basePoint() const { return base_; }
  signals:
    void activeLayerChanged();
    void stateChanged(); // phase/precision change, NOT every hover
  private:
    using Ids = std::vector<pd::EntityId>;
    std::optional<pd::DimKind> dimensionKind() const;
    bool finalDimPoint() const;
    QString draftingPrompt() const;
    std::optional<QString> draftingAnswer(const QString&);
    pd::edit::Plan draftPoint(pd::Vec2) const;
    std::vector<pd::Vec2> definitions_;
    pd::DimStyle style_;
    double axis_ = 0;
    QString blockName_;
    pd::BlockId blockId_ = 0;
    pd::Vec2 factors_{1, 1};
    double rotation_ = 0;
    int sides_ = 4;
    bool circumscribed_ = false;
    double chamfer1_ = 0, chamfer2_ = 0;
    std::vector<pd::Vec2> points_;
    // A LINE keeps drawing segments until Enter, so it remembers the corners and what it committed.
    std::vector<pd::Vec2> corners_;
    std::vector<pd::EntityId> segments_;
    bool writing_ = false; // the editor's own edits must not cancel the tool that makes them
    QString selectionDone(Ids);
    QString transition(Step);
    QString number(double);
    pd::edit::Plan proposal(pd::Vec2) const;
    QString publish(pd::edit::Plan);
    QString error(const std::exception&) const;
    void addSegment(pd::Vec2, pd::Vec2);
    void eraseSegment(pd::EntityId);
    pd::Document& doc_;
    pd::LayerId layer_ = pd::DefaultLayer;
    pd::Document::Subscription subscription_;
    Step step_ = Step::Idle;
    Tool tool_ = Tool::None;
    Ids ids_;
    std::optional<pd::Vec2> base_, cursor_;
    std::optional<pd::EntityId> hit_;
    std::optional<pd::edit::Plan> ghost_;
    pd::EntityId first_ = 0;
    pd::Vec2 firstPick_;
    size_t gripIndex_ = 0;
    double value_ = 0, reference_ = 1;
    bool guided_ = false;
    pd::edit::Tracking mode_ = pd::edit::Tracking::Free;
    int degrees_ = 30;
};
