// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <QDockWidget>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QString>
#include <planedraft/ids.hpp>
#include <vector>
class CommandSession;
class Viewport;
class QTableWidget;
class QTableWidgetItem;
class QLabel;
class QSpinBox;
class PropertyInspector final : public QDockWidget {
    Q_OBJECT
  public:
    explicit PropertyInspector(QWidget* parent = nullptr);
    void bind(CommandSession*, Viewport*);
    void refresh();
    void retranslate();
  signals:
    void message(const QString&);

  private:
    void edited(QTableWidgetItem*);
    void toggled(int row, int column);
    QPointer<CommandSession> session_;
    QPointer<Viewport> view_;
    QTableWidget* table_;
    QLabel* summary_;
    QLabel* vertexLabel_;
    QSpinBox* vertex_;
    std::vector<pd::EntityId> ids_;
    QHash<QString, bool> collapsed_;        // a group of properties the user folded away
    QHash<int, QString> headerRow_;         // table row -> the group it heads
    QHash<QString, QList<int>> sectionRows_; // group -> the rows it owns
    std::uint64_t revision_ = 0;
    bool writing_ = false;
};
