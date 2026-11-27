// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <QDockWidget>
#include <QPointer>
class CommandSession;
class QTableWidget;
class QTableWidgetItem;
class QPushButton;
class LayerManager final : public QDockWidget {
    Q_OBJECT
  public:
    explicit LayerManager(QWidget* parent = nullptr);
    void bind(CommandSession*);
    void refresh();
    void retranslate();
  signals:
    void message(const QString&);

  private:
    void edited(QTableWidgetItem*);
    void createLayer();
    void removeLayer();
    void pickColor(int row);
    void fitColumns();
    QPointer<CommandSession> session_;
    QTableWidget* table_;
    QPushButton *add_, *remove_;
    bool writing_ = false;
};
