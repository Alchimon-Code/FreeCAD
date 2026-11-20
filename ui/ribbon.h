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

// A compact Office-style ribbon built from existing QActions: no second command registry, so the
// menu, toolbars, shortcuts and the ribbon can never disagree about what a command does.
class Ribbon final : public QWidget {
    Q_OBJECT
  public:
    explicit Ribbon(QWidget* parent = nullptr);
    void addPage(const QString& key);
    // Actions are laid out top to bottom, one column per `columns` entries.
    void addPanel(const QString& page, const QString& title, const QList<QAction*>& actions,
                  int columns = 1);
    void addPanelWidget(const QString& page, const QString& title, QWidget* widget);
    void setQuickAccess(const QList<QAction*>& actions);
    void setCurrentPage(int index);
    [[nodiscard]] int currentPage() const;
    [[nodiscard]] QTabBar* tabs() const { return tabs_; }
    [[nodiscard]] QToolBar* quickAccess() const { return quickAccess_; }
    [[nodiscard]] QComboBox* layerBox() const { return layer_; }
    [[nodiscard]] QComboBox* colorBox() const { return color_; }
    [[nodiscard]] QComboBox* weightBox() const { return weight_; }
    void setPageTitles(const QStringList& titles);
    void retranslate(const QStringList& panelTitles);
    // The combos only report a choice; MainWindow applies it to the current selection.
    void refreshLayers(const QStringList& names, const QString& current);

  signals:
    void layerPicked(const QString& name);
    void colorPicked(const QVariant& value); // "ByLayer"/"ByBlock"/QColor
    void weightPicked(const QVariant& value); // "ByLayer"/"ByBlock"/double

  private:
    QWidget* page(const QString& key);
    QTabBar* tabs_;
    QStackedWidget* pages_;
    QToolBar* quickAccess_;
    QComboBox *layer_, *color_, *weight_;
    QHash<QString, int> index_;
    QList<QLabel*> panelLabels_;
    bool writing_ = false;
};
