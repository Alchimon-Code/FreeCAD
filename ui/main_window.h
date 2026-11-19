// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <QMainWindow>
#include <optional>
class Viewport;
class QTabBar;
class QTabWidget;
class CommandSession;
class Language;
class DrawingPage;
class PropertyInspector;
class LayerManager;
class QCompleter;
class QDockWidget;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QStringListModel;
class QToolBar;
class QToolButton;
class MainWindow : public QMainWindow {
    Q_OBJECT
  public:
    MainWindow(CommandSession&, Language&, bool loadTrustedPlugins = false);
    ~MainWindow() override;
    CommandSession& activeSession() const { return *session_; }
    Viewport* activeView() const { return canvas_; }
    void newDocument();
    bool openDrawing(QString path);
    bool closeDocument(int index);

  protected:
    bool eventFilter(QObject*, QEvent*) override;
    void closeEvent(QCloseEvent*) override;
    void changeEvent(QEvent*) override;

  private:
    void attach(DrawingPage*);
    std::optional<QString> routeDrawingFile(DrawingPage*, const QString& command, const QString& path);
    void activate(int index);
    bool confirmClose(DrawingPage*);
    void updateTabs();
    QString pickFile(const QString&, const QString& currentFile);
    void updateTitle();
    void retranslate();
    void updateEditState();
    void updatePrompt();
    void submit(const QString&);
    void chooseLanguage(bool);
    void choosePlugin(bool);
    void setupChrome();
    void translateChrome();
    void applyTheme(bool dark);
    void updateSnap();
    void showCoordinates(bool, double, double);
    void saveWorkspace();
    CommandSession* session_ = nullptr;
    DrawingPage* page_ = nullptr;
    Language& language_;
    Viewport* canvas_ = nullptr;
    QTabWidget* documents_;
    QTabBar* tabs_;
    int serial_ = 0;
    bool loadTrustedPlugins_;
    PropertyInspector* inspector_;
    LayerManager* layers_;
    QMenu *file_, *edit_, *view_, *settings_, *languages_, *polarStep_, *snapMenu_, *panels_;
    QAction *new_, *close_, *ortho_, *polar_, *grid_, *osnap_, *theme_, *lang_, *open_, *save_, *saveAs_,
        *exportSvg_, *exportPdf_, *load_, *loadFolder_, *unload_, *runScript_, *quit_, *undo_, *redo_,
        *extents_, *english_, *russian_;
    QList<QAction*> editActions_;
    QList<QToolBar*> bars_;
    QDockWidget* console_;
    QLineEdit* input_;
    QPlainTextEdit* history_;
    QStringListModel* completions_;
    QCompleter* completer_;
    QLabel *entityCount_, *coords_;
};
