// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <QWidget>
class CommandSession;
class Viewport;
class QTextDocument;
// Each tab owns its camera, FBO cache, selection, command buffer and transcript. The initial
// session is supplied by main; all other sessions are owned by their pages.
class DrawingPage final : public QWidget {
  public:
    DrawingPage(CommandSession*, int number, QWidget* parent);
    ~DrawingPage() override;
    void append(const QString&);
    CommandSession* session;
    Viewport* canvas;
    QTextDocument* transcript;
    QString draft;
    int number;
};
