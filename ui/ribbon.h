// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <QHash>
#include <QList>
#include <QWidget>

class QAction;
class QComboBox;
class QLabel;
class QStackedWidget;
class QTabBar;
class QToolBar;
class QResizeEvent;

// A compact Office-style ribbon built from existing QActions: no second command registry, so the
// menu, toolbars, shortcuts and the ribbon can never disagree about what a command does.
class Ribbon final : public QWidget {
    Q_OBJECT
  public:
    explicit Ribbon(QWidget* parent = nullptr);
    void addPage(const QString& key);
    // The first `primary` actions become large buttons, the rest are laid out as small entries,
    // one column per `columns` entries.
    void addPanel(const QString& page, const QString& title, const QList<QAction*>& actions,
                  int columns = 1, int primary = 1);
    void addPanelWidget(const QString& page, const QString& title, QWidget* widget);
    void setQuickAccess(const QList<QAction*>& actions);
    void setCurrentPage(int index);
    [[nodiscard]] int currentPage() const;
    [[nodiscard]] QTabBar* tabs() const { return tabs_; }
    [[nodiscard]] QToolBar* quickAccess() const { return quickAccess_; }
    [[nodiscard]] QComboBox* layerBox() const { return layer_; }
    [[nodiscard]] QComboBox* colorBox() const { return color_; }
    [[nodiscard]] QComboBox* weightBox() const { return weight_; }
    // Tab titles, like panel titles, keep the source string they were created from, so the three
    // copies of the same positional list cannot drift apart.
    void setPageTitles(const QStringList& sources);
    // Panel titles are translated from the source string each panel remembers, so adding a panel can
    // never shift the title of the panel next to it.
    void retranslate();
    // The combos only report a choice; MainWindow applies it to the current selection.
    void refreshLayers(const QStringList& names, const QString& current);
    // A double click on a tab minimizes the ribbon to its tabs, as AutoCAD does.
    void setCollapsed(bool collapsed);
    [[nodiscard]] bool collapsed() const { return collapsed_; }

  signals:
    void layerPicked(const QString& name);
    void colorPicked(const QVariant& value); // "ByLayer"/"ByBlock"/QColor
    void weightPicked(const QVariant& value); // "ByLayer"/"ByBlock"/double

  protected:
    void resizeEvent(QResizeEvent* event) override;

  private:
    QWidget* page(const QString& key);
    void updateStrip();
    void syncChevrons();
    void applyPageTitles();
    QTabBar* tabs_;
    QStackedWidget* pages_;
    QToolBar* quickAccess_;
    QComboBox *layer_, *color_, *weight_;
    QHash<QString, int> index_;
    QHash<QString, QWidget*> rows_; // the scrolling page content, without the viewport
    QList<QLabel*> panelLabels_;
    QStringList pageSources_;
    bool collapsed_ = false;
    bool writing_ = false;
};
