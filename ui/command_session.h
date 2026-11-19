// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "edit_controller.hpp"
#include "planedraft/document.hpp"
#include "plugin_manager.h"
#include "script_engine.hpp"
#include <planedraft/io.hpp>

#include <QHash>
#include <QObject>
#include <optional>

// UI, DLLs and Lisp commands share this single document-editing path.
class CommandSession : public QObject {
    Q_OBJECT
  public:
    explicit CommandSession(QObject* parent = nullptr);
    [[nodiscard]] const pd::Document& document() const { return document_; }
    pd::Document& document() { return document_; }
    // The workspace can route OPEN or reject a save over another open drawing.
    // nullopt: perform normal single-document I/O (the default for a standalone command host).
    using FileRouter =
        std::function<std::optional<QString>(const QString& command, const QString& absolutePath)>;
    void setFileRouter(FileRouter router) { fileRouter_ = std::move(router); }
    bool needsSaveAs() const { return !protectedSource_.isEmpty() && currentFile_ == protectedSource_; }
    EditController& editor() { return editor_; }
    [[nodiscard]] QStringList completions() const;
    QString execute(const QString& input);
    QString drawLine(pd::Vec2 first, pd::Vec2 last);
    QStringList layoutNames() const;
    const QString& space() const { return space_; }
    void selectSpace(const QString&);
    std::optional<pd::Layout> activeLayout() const;
    void addViewportExample(); // optional --viewport-demo, one undo step
    QString loadPlugin(const QString& absolutePath);
    QString unloadPlugin(const QString& absolutePath);
    void loadPluginDirectory(const QString& absolutePath);
    void setSelectionProvider(std::function<std::vector<pd::EntityId>()> provider) {
        selection_ = std::move(provider);
    }
    const QString& currentFile() const { return currentFile_; }
    bool modified() const { return document_.stateId() != savedState_; }
    using FilePicker = std::function<QString(const QString& global)>;
    void setFilePicker(FilePicker picker) { picker_ = std::move(picker); }
    void loadAliases(const QString& iniPath);

  signals:
    void documentChanged(const QList<pd::Change>& changes);
    void vocabularyChanged();
    void spaceChanged();
    void fileChanged();

    void consoleOutput(bool stderrStream, const QString& text);

  private:
    [[nodiscard]] QString resolve(QString token) const;
    std::optional<pd::EntityId> addLine(pd::Vec2 start, pd::Vec2 end);
    pd::ExtensionContext context();
    QString fileCommand(const QString& name, QString tail);
    QString layoutCommand(const QString&);
    QString space_;
    pd::io::IoRegistry io_;
    QString currentFile_, protectedSource_;
    std::uint64_t savedState_ = 0;
    FilePicker picker_;
    FileRouter fileRouter_;
    QString runScript(QString path);
    pd::Document document_;
    EditController editor_;
    pd::Document::Subscription subscription_;
    pd::CommandRegistry registry_;
    pd::PluginManager plugins_;
    std::unique_ptr<pd::IScriptEngine> scripts_;
    std::function<std::vector<pd::EntityId>()> selection_;
    bool executing_ = false;
    QHash<QString, QString> aliases_;
};
