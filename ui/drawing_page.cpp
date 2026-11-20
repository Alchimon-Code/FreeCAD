// SPDX-License-Identifier: LGPL-2.1-or-later
#include "drawing_page.h"
#include "command_session.h"
#include "viewport.h"
#include <QPlainTextDocumentLayout>
#include <QStandardPaths>
#include <QTextCursor>
#include <QVBoxLayout>
DrawingPage::DrawingPage(CommandSession* initial, int serial, QWidget* parent)
    : QWidget(parent), session(initial ? initial : new CommandSession(this)),
      canvas(new Viewport(session->document(), this)), transcript(new QTextDocument(this)), number(serial) {
    auto l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    l->addWidget(canvas);
    transcript->setDocumentLayout(new QPlainTextDocumentLayout(transcript));
    transcript->setMaximumBlockCount(150);
    if (!initial)
        session->loadAliases(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) +
                             "/aliases.ini");
    canvas->setEditor(&session->editor());
    canvas->showSpace(session->activeLayout());
    session->setSelectionProvider(
        [v = QPointer<Viewport>(canvas)] { return v ? v->selectedIds() : std::vector<pd::EntityId>{}; });
    connect(session, &CommandSession::documentChanged, canvas, [this](const QList<pd::Change>& batch) {
        canvas->invalidate({batch.constData(), size_t(batch.size())});
    });
    connect(session, &CommandSession::spaceChanged, canvas,
            [this] { canvas->showSpace(session->activeLayout()); });
    connect(session, &CommandSession::consoleOutput, this,
            [this](bool err, const QString& s) { append(err ? "[stderr] " + s : s); });
    connect(canvas, &Viewport::rendererError, this, &DrawingPage::append);
    connect(canvas, &Viewport::interactionMessage, this, &DrawingPage::append);
}
DrawingPage::~DrawingPage() {
    session->setFileRouter({});
    session->setFilePicker({});
    session->setSelectionProvider({});
    // A viewport borrows the Document. Release its GL resources before Qt destroys an owned session.
    delete canvas;
}
void DrawingPage::append(const QString& s) {
    if (s.isEmpty())
        return;
    QTextCursor c(transcript);
    c.movePosition(QTextCursor::End);
    if (!transcript->isEmpty())
        c.insertBlock();
    c.insertText(s);
}
