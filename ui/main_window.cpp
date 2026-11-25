// SPDX-License-Identifier: LGPL-2.1-or-later
#include "main_window.h"

#include "command_session.h"
#include "drawing_page.h"
#include "language.h"
#include "layer_manager.h"
#include "property_inspector.h"
#include "ribbon.h"
#include "viewport.h"
#include <QScrollBar>
#include <QTabWidget>
#include <QTextDocument>

#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QMenu>
#include <QCloseEvent>
#include <QAbstractItemView>
#include <QCompleter>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHash>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProxyStyle>
#include <QPointer>
#include <QSettings>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QStringListModel>
#include <QTabBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
// The style draws a box around every file tab. AutoCAD's file tabs are flat: the current one is a
// shade lighter than the row it sits on and nothing else is drawn, so only the shape is replaced and
// the label, the cross and the scroll behaviour stay with the style.
class FlatTabStyle : public QProxyStyle {
  public:
    void drawControl(ControlElement element, const QStyleOption* option, QPainter* painter,
                     const QWidget* widget) const override {
        if (element == CE_TabBarTabShape) {
            const auto* tab = qstyleoption_cast<const QStyleOptionTab*>(option);
            if (tab) {
                if (tab->state & QStyle::State_Selected) {
                    const QColor window = tab->palette.color(QPalette::Window);
                    const QColor surface =
                        window.lightness() < 128 ? window.lighter(132) : QColor(Qt::white);
                    painter->fillRect(tab->rect, surface);
                }
                return;
            }
        }
        QProxyStyle::drawControl(element, option, painter, widget);
    }
};

namespace {
// The ribbon and the menus name a command the way AutoCAD does, not the way it is typed: PLINE reads
// Polyline, RECTANG reads Rectangle, DIMLINEAR reads Linear. The canonical name is what the command
// line accepts, so only the title changes.
//
// Russian command names run their words together — the line wants one token, a caption wants words —
// so the clipboard group, whose names are the longest, is spelled out here. Every other Russian title
// is a single word and keeps coming from the CommandNames context.
QString commandTitle(const QByteArray& cmd, bool russian) {
    if (!russian) {
        static const QHash<QByteArray, const char*> english{
            {"PLINE", "Polyline"},             {"RECTANG", "Rectangle"},
            {"MATCHPROP", "Match properties"}, {"DIMLINEAR", "Linear"},
            {"DIMALIGNED", "Aligned"},         {"DIMRADIUS", "Radius"},
            {"DIMDIAMETER", "Diameter"},       {"DIMANGULAR", "Angular"},
            {"DIMSTYLE", "Dimension style"}};
        if (const auto found = english.constFind(cmd); found != english.constEnd())
            return QString::fromLatin1(*found);
    } else {
        static const QHash<QByteArray, const char*> russianNames{
            {"MATCHPROP", "Копировать свойства"},
            {"COPYCLIP", "Копировать"},
            {"CUTCLIP", "Вырезать"},
            {"PASTECLIP", "Вставить"},
            {"COPYBASE", "Копировать с базовой точкой"}};
        if (const auto found = russianNames.constFind(cmd); found != russianNames.constEnd())
            return QString::fromUtf8(*found);
    }
    QString label = QCoreApplication::translate("CommandNames", cmd.constData()).toLower();
    if (label.isEmpty())
        return QString::fromLatin1(cmd);
    label[0] = label[0].toUpper();
    return label;
}
} // namespace

MainWindow::MainWindow(CommandSession& session, Language& language, bool loadTrustedPlugins)
    : language_(language), loadTrustedPlugins_(loadTrustedPlugins) {
    auto pane = new QWidget(this);
    auto vbox = new QVBoxLayout(pane);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(0);
    documents_ = new QTabWidget(pane);
    documents_->setObjectName("documentTabs");
    documents_->setTabsClosable(true);
    documents_->setMovable(true);
    documents_->setDocumentMode(true);
    // The tab bar is given the flat style as its own; a proxy keeps the application style as its base
    // and owns nothing of it, and the widget owns the proxy.
    auto* tabStyle = new FlatTabStyle;
    tabStyle->setParent(documents_->tabBar());
    documents_->tabBar()->setStyle(tabStyle);
    vbox->addWidget(documents_, 1);
    tabs_ = new QTabBar(pane);
    tabs_->setObjectName("spaceTabs");
    tabs_->setExpanding(false);
    tabs_->setShape(QTabBar::RoundedSouth);
    vbox->addWidget(tabs_);
    ribbon_ = new Ribbon(this);
    auto* central = new QWidget(this);
    auto* shell = new QVBoxLayout(central);
    shell->setContentsMargins(0, 0, 0, 0);
    shell->setSpacing(0);
    shell->addWidget(ribbon_);
    shell->addWidget(pane, 1);
    setCentralWidget(central);
    connect(tabs_, &QTabBar::currentChanged, this, [this](int i) {
        if (i >= 0 && session_)
            session_->selectSpace(tabs_->tabData(i).toString());
    });
    file_ = menuBar()->addMenu(QString());
    edit_ = menuBar()->addMenu(QString());
    view_ = menuBar()->addMenu(QString());
    extents_ = view_->addAction(QString());
    extents_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+E")));
    connect(extents_, &QAction::triggered, this, [this] { canvas_->zoomExtents(); });
    settings_ = menuBar()->addMenu(QString());
    ortho_ = settings_->addAction(QString());
    polar_ = settings_->addAction(QString());
    ortho_->setCheckable(true);
    polar_->setCheckable(true);
    ortho_->setShortcut(Qt::Key_F8);
    polar_->setShortcut(Qt::Key_F10);
    using Mode = pd::edit::Tracking;
    connect(ortho_, &QAction::triggered, this,
            [this](bool on) { session_->editor().setTracking(on ? Mode::Ortho : Mode::Free); });
    connect(polar_, &QAction::triggered, this,
            [this](bool on) { session_->editor().setTracking(on ? Mode::Polar : Mode::Free); });
    polarStep_ = settings_->addMenu(QString());
    auto* increments = new QActionGroup(this);
    for (int degrees : {15, 30, 45}) {
        auto* a = polarStep_->addAction(QString::number(degrees) + QStringLiteral("°"));
        a->setActionGroup(increments);
        a->setCheckable(true);
        a->setData(degrees);
        connect(a, &QAction::triggered, this,
                [this, degrees] { session_->editor().setPolarDegrees(degrees); });
    }
    dynamic_ = settings_->addAction(QString());
    dynamic_->setObjectName("dynamicInputToggle");
    dynamic_->setCheckable(true);
    dynamic_->setShortcut(Qt::Key_F12);
    dynamic_->setChecked(QSettings().value("view/dynamicInput", true).toBool());
    connect(dynamic_, &QAction::toggled, this, [this](bool on) {
        QSettings().setValue("view/dynamicInput", on);
        if (canvas_)
            canvas_->setDynamicInput(on);
    });
    languages_ = settings_->addMenu(QString());
    new_ = file_->addAction(QString());
    new_->setShortcuts(QKeySequence::New);
    connect(new_, &QAction::triggered, this, &MainWindow::newDocument);
    auto action = [&](const char* cmd) {
        auto* a = file_->addAction(QString());
        connect(a, &QAction::triggered, this, [this, cmd] { submit(QStringLiteral("_") + cmd); });
        return a;
    };
    open_ = action("OPEN");
    save_ = action("SAVE");
    saveAs_ = action("SAVEAS");
    exportSvg_ = action("EXPORTSVG");
    exportPdf_ = action("EXPORTPDF");
    open_->setShortcuts(QKeySequence::Open);
    save_->setShortcuts(QKeySequence::Save);
    saveAs_->setShortcuts(QKeySequence::SaveAs);
    file_->addSeparator();
    close_ = file_->addAction(QString());
    close_->setShortcuts(QKeySequence::Close);
    connect(close_, &QAction::triggered, this, [this] { closeDocument(documents_->currentIndex()); });
    load_ = file_->addAction(QString());
    loadFolder_ = file_->addAction(QString());
    unload_ = file_->addAction(QString());
    runScript_ = file_->addAction(QString());
    quit_ = file_->addAction(QString());
    undo_ = edit_->addAction(QString());
    redo_ = edit_->addAction(QString());
    undo_->setShortcuts(QKeySequence::Undo);
    redo_->setShortcuts(QKeySequence::Redo);
    edit_->addSeparator();
    for (auto entry : EditController::vocabulary()) {
        auto cmd = QString::fromLatin1(entry.global);
        auto* a = edit_->addAction(QString());
        a->setData(cmd);
        editActions_.append(a);
        commands_.insert(cmd, a);
        connect(a, &QAction::triggered, this, [this, cmd] { submit("_" + cmd); });
    }
    // Clipboard and context-menu commands. Shortcuts live on the drawing canvas, so a focused
    // text field keeps its own Ctrl+C/V behaviour.
    auto canvasAction = [this](const QKeySequence& key) {
        auto* a = new QAction(this);
        a->setShortcut(key);
        a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        return a;
    };
    cut_ = canvasAction(QKeySequence::Cut);
    copy_ = canvasAction(QKeySequence::Copy);
    paste_ = canvasAction(QKeySequence::Paste);
    copyBase_ = canvasAction(QKeySequence(QStringLiteral("Ctrl+Shift+C")));
    repeat_ = canvasAction(QKeySequence());
    cut_->setObjectName("cutClipboard");
    copy_->setObjectName("copyClipboard");
    paste_->setObjectName("pasteClipboard");
    copyBase_->setObjectName("copyBaseClipboard");
    repeat_->setObjectName("repeatCommand");
    connect(cut_, &QAction::triggered, this, [this] { submit("_CUTCLIP"); });
    connect(copy_, &QAction::triggered, this, [this] { submit("_COPYCLIP"); });
    connect(paste_, &QAction::triggered, this, [this] {
        auto p = canvas_ ? canvas_->cursorPoint() : std::nullopt;
        submit(p ? QStringLiteral("_PASTECLIP %1,%2").arg(p->x).arg(p->y) : QStringLiteral("_PASTECLIP"));
    });
    connect(copyBase_, &QAction::triggered, this, [this] {
        auto p = canvas_ ? canvas_->cursorPoint() : std::nullopt;
        submit(p ? QStringLiteral("_COPYBASE %1,%2").arg(p->x).arg(p->y)
                 : QStringLiteral("_COPYBASE 0,0"));
    });
    connect(repeat_, &QAction::triggered, this, [this] { submit(session_->repeatLast()); });
    erase_ = canvasAction(QKeySequence::Delete);
    erase_->setObjectName("eraseSelection");
    connect(erase_, &QAction::triggered, this, [this] { submit("_ERASE"); });
    properties_ = edit_->addAction(QString());
    properties_->setObjectName("selectionProperties");
    connect(properties_, &QAction::triggered, this, [this] {
        inspector_->raise();
        inspector_->refresh();
    });
    recent_ = edit_->addMenu(QString());
    contextMenu_ = new QMenu(this);
    contextMenu_->setObjectName("drawingContextMenu");
    // AutoCAD's shortcut menu during a command starts with the two keys that matter: Enter for the
    // next step and Cancel for the way out.
    contextEnter_ = contextMenu_->addAction(QString());
    contextEnter_->setObjectName("contextEnter");
    connect(contextEnter_, &QAction::triggered, this, [this] { submit(QString()); });
    contextCancel_ = contextMenu_->addAction(QString());
    contextCancel_->setObjectName("contextCancel");
    connect(contextCancel_, &QAction::triggered, this, [this] { canvas_->escape(); });
    contextStepSeparator_ = contextMenu_->addSeparator();
    contextRepeat_ = contextMenu_->addAction(QString());
    contextRepeat_->setObjectName("contextRepeat");
    connect(contextRepeat_, &QAction::triggered, repeat_, &QAction::trigger);
    contextMenu_->addMenu(recent_);
    recent_->setObjectName("contextRecent");
    contextMenu_->addSeparator();
    for (auto* action : {cut_, copy_, copyBase_, paste_})
        contextMenu_->addAction(action);
    contextMenu_->addSeparator();
    contextMenu_->addAction(erase_);
    contextMenu_->addAction(properties_);
    contextMenu_->addSeparator();
    contextMenu_->addAction(extents_);
    auto* group = new QActionGroup(this);
    english_ = languages_->addAction(QStringLiteral("English"));
    russian_ = languages_->addAction(QStringLiteral("Русский"));
    for (auto* action : {english_, russian_}) {
        action->setCheckable(true);
        group->addAction(action);
    }

    console_ = new QDockWidget(this);
    console_->setObjectName("CommandLine");
    auto* panel = new QWidget(console_);
    auto* layout = new QVBoxLayout(panel);
    history_ = new QPlainTextEdit(panel);
    history_->setObjectName("commandHistory");
    history_->setReadOnly(true);
    history_->setMaximumBlockCount(150);
    history_->setMaximumHeight(80);
    input_ = new QLineEdit(panel);
    input_->setObjectName("commandInput");
    completions_ = new QStringListModel(this);
    completer_ = new QCompleter(completions_, this);
    completer_->setCaseSensitivity(Qt::CaseInsensitive);
    input_->installEventFilter(this);
    layout->addWidget(history_);
    // AutoCAD names the line it is waiting on: "Command:" before anything is typed.
    commandPrompt_ = new QLabel(panel);
    commandPrompt_->setObjectName("commandPrompt");
    auto* line = new QHBoxLayout;
    line->setContentsMargins(0, 0, 0, 0);
    line->setSpacing(4);
    line->addWidget(commandPrompt_);
    line->addWidget(input_, 1);
    layout->addLayout(line);
    console_->setWidget(panel);
    // AutoCAD's command line has no caption bar: the prompt and the history above it are the whole
    // widget, and the dock is still reachable from View - Panels and from Ctrl+9.
    console_->setTitleBarWidget(new QWidget(console_));
    addDockWidget(Qt::BottomDockWidgetArea, console_);
    entityCount_ = new QLabel(this);
    inspector_ = new PropertyInspector(this);
    layers_ = new LayerManager(this);
    addDockWidget(Qt::RightDockWidgetArea, inspector_);
    addDockWidget(Qt::RightDockWidgetArea, layers_);
    tabifyDockWidget(inspector_, layers_);
    inspector_->raise();
    auto note = [this](const QString& s) {
        page_->append(s);
        // The prompt returns on its own: a hint must not sit in the status bar forever.
        if (statusMessage_)
            statusMessage_->setText(s);
        QTimer::singleShot(5000, this, [this] { updatePrompt(); });
    };
    connect(inspector_, &PropertyInspector::message, this, note);
    connect(layers_, &LayerManager::message, this, note);
    setupChrome();
    connect(quit_, &QAction::triggered, this, &QWidget::close);
    connect(load_, &QAction::triggered, this, [this] { choosePlugin(false); });
    connect(unload_, &QAction::triggered, this, [this] { choosePlugin(true); });
    connect(loadFolder_, &QAction::triggered, this, [this] {
        session_->loadPluginDirectory(QCoreApplication::applicationDirPath() + "/../plugins");
    });
    connect(runScript_, &QAction::triggered, this, [this] {
        auto path = QFileDialog::getOpenFileName(
            this, tr("Run script…"), QCoreApplication::applicationDirPath() + "/../examples/scripts",
            tr("PlaneDraft Lisp (*.pdl)"));
        if (!path.isEmpty())
            submit("_RUNSCRIPT \"" + path + "\"");
    });
    connect(undo_, &QAction::triggered, this, [this] { submit("_UNDO"); });
    connect(redo_, &QAction::triggered, this, [this] { submit("_REDO"); });
    connect(english_, &QAction::triggered, this, [this] { chooseLanguage(false); });
    connect(russian_, &QAction::triggered, this, [this] { chooseLanguage(true); });
    connect(input_, &QLineEdit::returnPressed, this, [this] {
        const auto text = input_->text();
        input_->clear();
        submit(text);
    });
    connect(documents_, &QTabWidget::currentChanged, this, &MainWindow::activate);
    connect(documents_, &QTabWidget::tabCloseRequested, this, &MainWindow::closeDocument);
    attach(new DrawingPage(&session, ++serial_, documents_));
    retranslate();
    resize(1240, 800);
    resizeDocks({inspector_}, {290}, Qt::Horizontal);
    QSettings cfg;
    if (cfg.contains("window/geometry"))
        restoreGeometry(cfg.value("window/geometry").toByteArray());
    restoreState(cfg.value("window/state").toByteArray(), 1);
    input_->setFocus();
}

void MainWindow::submit(const QString& text) {
    if (text.trimmed().isEmpty() && !session_->editor().active()) {
        enterOrRepeat();
        return;
    }
    if (!text.trimmed().isEmpty()) {
        page_->append("> " + text);
        if (typed_.isEmpty() || typed_.last() != text) {
            typed_.append(text);
            if (typed_.size() > 200)
                typed_.removeFirst();
        }
    }
    typedIndex_ = -1;
    draft_.clear();
    auto result = session_->execute(text); // OPEN can activate a different page.
    page_->append(result);
    history_->verticalScrollBar()->setValue(history_->verticalScrollBar()->maximum());
}

// The prompt is always there; the history above it can collapse to one line, the way the command
// line does in AutoCAD, and F2 brings it back.
void MainWindow::showCommandHistory(bool on) {
    if (historyToggle_ && historyToggle_->isChecked() != on)
        historyToggle_->setChecked(on);
    if (on && console_)
        console_->show();
    if (history_)
        history_->setVisible(on);
    QSettings().setValue("view/commandHistory", on);
}

// Clean screen keeps the drawing, the tabs and the prompt, and puts the rest away; the explicit
// hidden state is what gets remembered, so a palette the user had closed stays closed afterwards.
void MainWindow::applyCleanScreen(bool on) {
    if (on) {
        cleanBars_.clear();
        for (auto* bar : bars_)
            cleanBars_.append(!bar->isHidden());
        cleanRibbon_ = ribbon_ && !ribbon_->isHidden();
        cleanDocks_ = {!inspector_->isHidden(), !layers_->isHidden()};
        cleanMenu_ = !menuBar()->isHidden();
        cleanStatus_ = !statusBar()->isHidden();
        for (auto* bar : bars_)
            bar->hide();
        if (ribbon_)
            ribbon_->hide();
        inspector_->hide();
        layers_->hide();
        menuBar()->hide();
        statusBar()->hide();
        return;
    }
    for (qsizetype i = 0; i < bars_.size() && i < cleanBars_.size(); ++i)
        bars_[i]->setVisible(cleanBars_[i]);
    if (ribbon_)
        ribbon_->setVisible(cleanRibbon_);
    if (cleanDocks_.size() == 2) {
        inspector_->setVisible(cleanDocks_[0]);
        layers_->setVisible(cleanDocks_[1]);
    }
    menuBar()->setVisible(cleanMenu_);
    statusBar()->setVisible(cleanStatus_);
    cleanBars_.clear();
    cleanDocks_.clear();
}

QString MainWindow::pickFile(const QString& command, const QString& currentFile) {
    if (command == "OPEN") {
        return QFileDialog::getOpenFileName(this, tr("Open drawing"), currentFile,
                                            tr("CAD drawings (*.dxf *.dwg)"));
    }
    bool svg = command == "EXPORTSVG", pdf = command == "EXPORTPDF";
    auto filter = svg   ? tr("SVG drawing (*.svg)")
                  : pdf ? tr("PDF document (*.pdf)")
                        : tr("ASCII DXF (*.dxf);;DWG drawing (*.dwg)");
    QFileDialog dlg(this,
                    svg   ? tr("Export SVG…")
                    : pdf ? tr("Export PDF…")
                          : tr("Save drawing as…"),
                    QString(), filter);
    dlg.setAcceptMode(QFileDialog::AcceptSave);
    dlg.setDefaultSuffix(svg ? "svg" : pdf ? "pdf" : "dxf");
    connect(&dlg, &QFileDialog::filterSelected, &dlg, [&dlg](const QString& f) {
        if (f.contains("*.dwg"))
            dlg.setDefaultSuffix("dwg");
        else if (f.contains("*.dxf"))
            dlg.setDefaultSuffix("dxf");
    });
    return dlg.exec() == QDialog::Accepted ? dlg.selectedFiles().value(0) : QString{};
}
void MainWindow::updateTitle() {
    for (int i = 0; i < documents_->count(); ++i) {
        auto p = static_cast<DrawingPage*>(documents_->widget(i));
        auto path = p->session->currentFile();
        auto title = path.isEmpty() ? tr("Untitled %1").arg(p->number) : QFileInfo(path).fileName();
        if (p->session->modified())
            title += " *";
        documents_->setTabText(i, title);
        documents_->setTabToolTip(i, path);
        if (p == page_)
            setWindowTitle(title + " — PlaneDraft 2D");
    }
}
void MainWindow::choosePlugin(bool unload) {
    const QString path = QFileDialog::getOpenFileName(
        this, unload ? tr("Unload plugin…") : tr("Load a trusted plugin"),
        QCoreApplication::applicationDirPath() + "/../plugins", tr("Native libraries (*.dll *.so *.dylib)"));
    if (!path.isEmpty()) {
        const auto error = unload ? session_->unloadPlugin(path) : session_->loadPlugin(path);
        history_->appendPlainText(error.isEmpty()
                                      ? (unload ? tr("Plugin unloaded: %1") : tr("Plugin loaded: %1"))
                                            .arg(QDir::toNativeSeparators(path))
                                      : error);
    }
}

void MainWindow::chooseLanguage(bool russian) {
    if (language_.setRussian(russian)) {
        QSettings().setValue("ui/language", russian ? "ru" : "en");
    } else {
        QMessageBox::warning(this, tr("Language"), tr("Could not load the translation catalog."));
    }
    (language_.russian() ? russian_ : english_)->setChecked(true);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* e) {
    if (watched == input_ && e->type() == QEvent::KeyPress) {
        auto key = static_cast<QKeyEvent*>(e);
        // Space confirms the current step or repeats the last command, exactly like Enter.
        // Text entry (RENAME, MTEXT content, file names) keeps the literal space.
        if (key->key() == Qt::Key_Space && !session_->editor().expectsFreeText() &&
            !(key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
            const auto text = input_->text().trimmed();
            input_->clear();
            if (text.isEmpty())
                enterOrRepeat();
            else
                submit(text);
            return true;
        }
        if (key->key() == Qt::Key_Escape) {
            input_->clear();
            canvas_->escape();
            return true;
        }
        if (key->key() == Qt::Key_Delete && input_->text().isEmpty()) {
            submit("_ERASE");
            return true;
        }
        // Up/Down walk what was typed, like the prompt in AutoCAD. While the completion popup is
        // open they keep moving inside the list instead.
        if ((key->key() == Qt::Key_Up || key->key() == Qt::Key_Down) &&
            !completer_->popup()->isVisible()) {
            if (key->key() == Qt::Key_Up && typedIndex_ + 1 < typed_.size()) {
                if (typedIndex_ < 0)
                    draft_ = input_->text();
                ++typedIndex_;
                input_->setText(typed_[typed_.size() - 1 - typedIndex_]);
            } else if (key->key() == Qt::Key_Down && typedIndex_ >= 0) {
                --typedIndex_;
                input_->setText(typedIndex_ >= 0 ? typed_[typed_.size() - 1 - typedIndex_] : draft_);
            }
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, e);
}
void MainWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space && event->modifiers() == Qt::NoModifier &&
        qobject_cast<QLineEdit*>(QApplication::focusWidget()) == nullptr &&
        !(session_ && session_->editor().expectsFreeText())) {
        enterOrRepeat();
        event->accept();
        return;
    }
    QMainWindow::keyPressEvent(event);
}

void MainWindow::enterOrRepeat() {
    if (!session_)
        return;
    if (session_->editor().active()) {
        submit("");
        return;
    }
    auto reply = session_->repeatLast();
    if (reply.isEmpty())
        return;
    page_->append("> " + tr("repeat"));
    page_->append(reply);
    history_->verticalScrollBar()->setValue(history_->verticalScrollBar()->maximum());
}

// Enter and Cancel belong to the state of the command rather than to the menu: they follow the prompt
// so that a right click during a command always offers the two keys AutoCAD puts there.
void MainWindow::refreshContextStep() {
    if (!session_)
        return;
    const bool running = session_->editor().active();
    contextEnter_->setText(tr("Enter"));
    contextCancel_->setText(tr("Cancel"));
    contextEnter_->setVisible(running);
    contextCancel_->setVisible(running);
    contextStepSeparator_->setVisible(running);
}

void MainWindow::refreshContextMenu() {
    if (!session_)
        return;
    refreshContextStep();
    const bool running = session_->editor().active();
    contextRepeat_->setText(repeat_->text());
    contextRepeat_->setEnabled(!running && !session_->recentCommands().isEmpty());
    recent_->clear();
    recent_->setTitle(tr("Recent commands"));
    for (const auto& name : session_->recentCommands()) {
        auto* a = recent_->addAction(name);
        connect(a, &QAction::triggered, this, [this, name] { submit("_" + name); });
    }
    recent_->setEnabled(!recent_->isEmpty());
}

void MainWindow::showContextMenu(const QPoint& globalPos) {
    if (!session_)
        return;
    refreshContextMenu();
    // The clipboard is asked when the menu opens, not on every prompt: a paste action that is asked
    // mid-command can be a step behind the command that filled the clipboard.
    paste_->setEnabled(!session_->clipboardEmpty());
    contextMenu_->exec(globalPos);
}

void MainWindow::applyRibbonMode(bool on) {
    if (!ribbon_)
        return;
    if (on) {
        if (classicVisibility_.size() != bars_.size()) {
            classicVisibility_.clear();
            for (auto* bar : bars_)
                classicVisibility_.append(!bar->isHidden());
        }
        for (auto* bar : bars_)
            bar->hide();
    } else {
        for (int i = 0; i < bars_.size(); ++i)
            bars_[i]->setVisible(i < classicVisibility_.size() ? classicVisibility_[i] : true);
        classicVisibility_.clear();
    }
    ribbon_->setVisible(on);
    QSettings().setValue("ui/ribbon", on);
    if (on)
        syncRibbon();
}

void MainWindow::syncRibbon() {
    if (!ribbon_ || ribbon_->isHidden() || !session_)
        return;
    QStringList names;
    QString current;
    for (auto id : session_->document().layerIds()) {
        const auto* layer = session_->document().layer(id);
        names << QString::fromStdString(layer->name);
        if (id == session_->editor().activeLayer())
            current = QString::fromStdString(layer->name);
    }
    ribbon_->refreshLayers(names, current.isEmpty() && !names.isEmpty() ? names.front() : current);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    for (int i = 0; i < documents_->count(); ++i) {
        auto p = static_cast<DrawingPage*>(documents_->widget(i));
        if (p->session->modified()) {
            documents_->setCurrentIndex(i);
            if (!confirmClose(p)) {
                event->ignore();
                return;
            }
        }
    }
    saveWorkspace();
    event->accept();
}
void MainWindow::changeEvent(QEvent* event) {
    QMainWindow::changeEvent(event);
    if (page_ && event->type() == QEvent::LanguageChange) {
        retranslate();
    }
}

void MainWindow::updateEditState() {
    const auto& document = session_->document();
    undo_->setEnabled(document.canUndo());
    redo_->setEnabled(document.canRedo());
    entityCount_->setText(tr("Entities: %1").arg(document.size()));
}

void MainWindow::updateTabs() {
    QSignalBlocker guard(tabs_);
    while (tabs_->count())
        tabs_->removeTab(0);
    tabs_->addTab(tr("Model"));
    for (auto& name : session_->layoutNames()) {
        int i = tabs_->addTab(name == "Layout1" ? tr("Sheet 1") : name);
        tabs_->setTabData(i, name);
        if (name == session_->space())
            tabs_->setCurrentIndex(i);
    }
}
void MainWindow::retranslate() {
    updateTabs();
    updateTitle();
    open_->setText(tr("Open drawing…"));
    save_->setText(tr("Save"));
    saveAs_->setText(tr("Save drawing as…"));
    exportSvg_->setText(tr("Export SVG…"));
    exportPdf_->setText(tr("Export PDF…"));
    file_->setTitle(tr("&File"));
    edit_->setTitle(tr("&Edit"));
    for (auto* a : editActions_) {
        const auto cmd = a->data().toByteArray();
        // AutoCAD writes the ellipsis only on an entry that opens a dialog: a command that asks for
        // its arguments in the command line keeps its plain name on the ribbon and in the menus.
        const auto label = commandTitle(cmd, language_.russian());
        a->setText(label);
        a->setToolTip(label + " (_" + QString::fromLatin1(cmd) + ")");
    }
    view_->setTitle(tr("&View"));
    extents_->setText(tr("Zoom Extents"));
    ortho_->setText(tr("Ortho"));
    polar_->setText(tr("Polar tracking"));
    polarStep_->setTitle(tr("Polar increment"));
    settings_->setTitle(tr("&Settings"));
    languages_->setTitle(tr("Language"));
    load_->setText(tr("Load plugin…"));
    unload_->setText(tr("Unload plugin…"));
    loadFolder_->setText(tr("Load trusted plugins from plugins/"));
    runScript_->setText(tr("Run script…"));
    quit_->setText(tr("Exit"));
    undo_->setText(tr("Undo"));
    redo_->setText(tr("Redo"));
    cut_->setText(tr("Cut"));
    copy_->setText(tr("Copy"));
    copyBase_->setText(tr("Copy with base point"));
    paste_->setText(tr("Paste"));
    erase_->setText(tr("Erase"));
    properties_->setText(tr("Properties"));
    repeat_->setText(session_ && !session_->recentCommands().isEmpty()
                         ? tr("Repeat %1").arg(session_->recentCommands().front())
                         : tr("Repeat last command"));
    dynamic_->setText(tr("Dynamic input"));
    dynamic_->setToolTip(tr("Length and angle beside the crosshair (F12)"));
    ribbonToggle_->setText(tr("Ribbon"));
    new_->setText(tr("New drawing"));
    close_->setText(tr("Close tab"));
    translateChrome();
    inspector_->retranslate();
    layers_->retranslate();
    console_->setWindowTitle(tr("Command line"));
    commandPrompt_->setText(tr("Command:"));
    completions_->setStringList(session_->completions());
    if (ribbon_)
        ribbon_->retranslate();
    (language_.russian() ? russian_ : english_)->setChecked(true);
    updatePrompt();
    updateEditState();
}

void MainWindow::updatePrompt() {
    auto& e = session_->editor();
    // Arguments (especially R, XY and #IDs) are not command-name completions.
    auto* desired = e.active() ? nullptr : completer_;
    if (input_->completer() != desired)
        input_->setCompleter(desired);
    refreshContextStep();
    ortho_->setChecked(e.tracking() == pd::edit::Tracking::Ortho);
    polar_->setChecked(e.tracking() == pd::edit::Tracking::Polar);
    for (auto* a : polarStep_->actions())
        a->setChecked(a->data().toInt() == e.polarDegrees());
    input_->setPlaceholderText(e.active() ? e.prompt() : tr("Example: _LINE 0,0 80,40"));
    if (statusMessage_)
        statusMessage_->setText(
            e.active() ? e.prompt()
                       : tr("Middle: pan   •   Wheel: zoom   •   Left drag: window → / crossing ←"));
}
MainWindow::~MainWindow() { disconnect(documents_, nullptr, this, nullptr); }
void MainWindow::attach(DrawingPage* p) {
    auto s = p->session;
    auto v = p->canvas;
    s->setFileRouter([w = QPointer<MainWindow>(this), p](const QString& cmd,
                                                         const QString& path) -> std::optional<QString> {
        return w ? w->routeDrawingFile(p, cmd, path) : std::nullopt;
    });
    s->setFilePicker([w = QPointer<MainWindow>(this), s](const QString& cmd) {
        return w ? w->pickFile(cmd, s->currentFile()) : QString{};
    });
    connect(s, &CommandSession::documentChanged, p, [this, p](const QList<pd::Change>& batch) {
        updateTitle();
        if (page_ != p)
            return;
        updateEditState();
        syncRibbon();
        if (batch.front().kind == pd::Change::Kind::Reset)
            updateTabs();
    });
    connect(s, &CommandSession::fileChanged, p, [this] { updateTitle(); });
    connect(s, &CommandSession::spaceChanged, p, [this, p] {
        if (page_ == p)
            updateTabs();
    });
    connect(s, &CommandSession::vocabularyChanged, p, [this, p] {
        if (page_ == p)
            completions_->setStringList(session_->completions());
    if (ribbon_)
        ribbon_->retranslate();
    });
    connect(&s->editor(), &EditController::stateChanged, p, [this, p] {
        if (page_ == p)
            updatePrompt();
    });
    connect(v, &Viewport::commandText, p, [this, p](const QString& text) {
        if (page_ == p) {
            input_->setFocus();
            input_->insert(text);
        }
    });
    connect(v, &Viewport::eraseRequested, p, [this, p] {
        if (page_ == p)
            submit("_ERASE");
    });
    connect(v, &Viewport::contextMenuRequested, p, [this, p](const QPoint& global) {
        if (page_ == p)
            showContextMenu(global);
    });
    connect(v, &Viewport::enterRequested, p, [this, p] {
        if (page_ == p)
            enterOrRepeat();
    });
    s->setZoomExtentsHandler([v] { v->zoomExtents(); });
    v->setDynamicInput(dynamic_->isChecked());
    for (auto* action : {cut_, copy_, copyBase_, paste_, erase_})
        v->addAction(action);
    connect(v, &Viewport::cursorPosition, p, [this, p](bool valid, double x, double y) {
        if (p == page_)
            showCoordinates(valid, x, y);
    });
    int index = documents_->addTab(p, QString{});
    // AutoCAD's file tabs carry one plain cross, in the text colour, whether the drawing has unsaved
    // edits or not; the star beside the name says that instead. The style's own close button came
    // out red on the current tab, and the fix is to draw the glyph here.
    auto* close = new QToolButton(documents_);
    close->setObjectName("documentClose");
    close->setText(QStringLiteral("\u2715"));
    close->setAutoRaise(true);
    close->setFocusPolicy(Qt::NoFocus);
    close->setCursor(Qt::ArrowCursor);
    close->setToolTip(tr("Close the drawing"));
    QFont closeFont = close->font();
    closeFont.setPixelSize(11);
    close->setFont(closeFont);
    close->setFixedSize(16, 16);
    connect(close, &QToolButton::clicked, this, [this, p] { closeDocument(documents_->indexOf(p)); });
    documents_->tabBar()->setTabButton(index, QTabBar::RightSide, close);
    documents_->setCurrentIndex(index);
    if (loadTrustedPlugins_)
        s->loadPluginDirectory(QCoreApplication::applicationDirPath() + "/../plugins");
    if (s->document().size())
        v->zoomExtents();
}
void MainWindow::activate(int index) {
    if (index < 0)
        return;
    if (page_)
        page_->draft = input_->text();
    page_ = static_cast<DrawingPage*>(documents_->widget(index));
    session_ = page_->session;
    canvas_ = page_->canvas;
    // The F12 toggle is restored before any canvas exists, so a stored "on" never reached the
    // viewport. Apply it with the rest of the per-page view state.
    if (dynamic_)
        canvas_->setDynamicInput(dynamic_->isChecked());
    history_->setDocument(page_->transcript);
    input_->setText(page_->draft);
    inspector_->bind(session_, canvas_);
    layers_->bind(session_);
    completions_->setStringList(session_->completions());
    if (ribbon_)
        ribbon_->retranslate();
    updateTitle();
    updateTabs();
    updateEditState();
    updatePrompt();
    syncRibbon();
    showCoordinates(false, 0, 0);
    canvas_->setGrid(grid_->isChecked());
    updateSnap();
}
void MainWindow::newDocument() { attach(new DrawingPage(nullptr, ++serial_, documents_)); }
bool MainWindow::openDrawing(QString path) {
    submit("_OPEN \"" + path + "\"");
    return !session_->currentFile().isEmpty() && QFileInfo(session_->currentFile()) == QFileInfo(path);
}
std::optional<QString> MainWindow::routeDrawingFile(DrawingPage* from, const QString& cmd,
                                                    const QString& path) {
    for (int i = 0; i < documents_->count(); ++i) {
        auto s = static_cast<DrawingPage*>(documents_->widget(i))->session;
        if (!s->currentFile().isEmpty() && QFileInfo(s->currentFile()) == QFileInfo(path)) {
            if (cmd != "OPEN") {
                if (s != from->session)
                    return tr("This file is open in another tab. Choose a different name.");
                return std::nullopt;
            }
            documents_->setCurrentIndex(i);
            return tr("Already open: %1").arg(path);
        }
    }
    if (cmd != "OPEN")
        return std::nullopt;
    auto s = from->session;
    if (s->currentFile().isEmpty() && !s->modified() && !s->document().canUndo() && !s->document().canRedo())
        return std::nullopt;
    // Stage the import off the tab strip. A failure leaves selection, history and camera untouched.
    auto p = std::make_unique<DrawingPage>(nullptr, ++serial_, documents_);
    auto result = p->session->execute("_OPEN \"" + path + "\"");
    if (p->session->currentFile().isEmpty())
        from->append(p->transcript->toPlainText());
    else
        attach(p.release());
    return result;
}
bool MainWindow::confirmClose(DrawingPage* p) {
    if (!p->session->modified())
        return true;
    auto answer = QMessageBox::question(
        this, tr("Close drawing"),
        tr("Save changes to %1 before closing?").arg(documents_->tabText(documents_->indexOf(p))),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Discard)
        return true;
    if (answer != QMessageBox::Save)
        return false;
    p->append(p->session->execute(p->session->needsSaveAs() ? "_SAVEAS" : "_SAVE"));
    return !p->session->modified();
}
bool MainWindow::closeDocument(int index) {
    if (index < 0 || index >= documents_->count())
        return false;
    documents_->setCurrentIndex(index);
    auto p = page_;
    if (!confirmClose(p))
        return false;
    if (documents_->count() == 1)
        newDocument();
    documents_->removeTab(documents_->indexOf(p));
    delete p;
    return true;
}
void MainWindow::saveWorkspace() {
    QSettings cfg;
    cfg.setValue("window/geometry", saveGeometry());
    cfg.setValue("window/state", saveState(1));
}
