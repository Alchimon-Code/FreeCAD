// SPDX-License-Identifier: LGPL-2.1-or-later
#include "command_session.h"
#include "language.h"

#include <QCoreApplication>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <gtest/gtest.h>
#include <planedraft/entities/primitives.hpp>

TEST(Session, LanguageSwitchChangesVocabularyNotDrawing) {
    Language language;
    CommandSession session;
    session.execute("line 0,0 1.25,-3");
    ASSERT_EQ(session.document().size(), 1u);
    ASSERT_TRUE(language.setRussian(true));
    EXPECT_TRUE(session.completions().contains("ОТРЕЗОК"));
    EXPECT_EQ(session.execute("отрезок 2,2 3,3"), QStringLiteral("Добавлен отрезок №2."));
    session.execute("_LiNe 3,4 5,6");
    EXPECT_EQ(session.document().size(), 3u);
    session.execute("отменить");
    session.execute("повторить");
    EXPECT_EQ(session.document().revision(), 5u);
    session.execute("_ОТРЕЗОК 7,7 8,8"); // '_' bypasses localized names
    EXPECT_EQ(session.document().revision(), 5u);
    ASSERT_TRUE(language.setRussian(false));
    EXPECT_FALSE(session.completions().contains("ОТРЕЗОК"));
    EXPECT_TRUE(session.completions().contains("_LINE"));
    session.execute("ОТРЕЗОК 7,7 8,8");
    session.execute("undo");
    EXPECT_EQ(session.document().size(), 2u);
}

TEST(Session, InvalidInputPublishesNoChange) {
    CommandSession session;
    int changes = 0;
    QObject::connect(&session, &CommandSession::documentChanged, &session,
                     [&](const QList<pd::Change>& batch) {
                         ASSERT_EQ(batch.size(), 1);
                         EXPECT_EQ(++changes, batch.front().revision);
                     });
    for (auto input : {" ", "NONE", "_LINE", "_LINE 0,0 0,0", "LINE 0 1,2", "LINE 1,2,3 4,5",
                       "LINE nan,0 1,1", "LINE 1,1 1e999,1", "UNDO", "REDO wrong"}) {
        session.execute(input);
    }
    EXPECT_EQ(changes, 0);
    session.execute("  _line\t -1,0  .5,1e2 ");
    session.execute("_UNDO");
    session.execute("_REDO");
    EXPECT_EQ(changes, 3);
}

TEST(Session, OptionalViewportExampleIsOneUndoableEditAtSurveyCoordinates) {
    CommandSession session;
    session.addViewportExample();
    const auto& doc = session.document();
    EXPECT_EQ(doc.size(), 6u);
    EXPECT_EQ(doc.revision(), 1u);
    EXPECT_GT(doc.extents().min().x, 9.9e6);
    const auto ids = doc.entityIds();
    session.execute("_UNDO");
    EXPECT_TRUE(doc.extents().empty());
    session.execute("_REDO");
    EXPECT_EQ(doc.entityIds(), ids);
}

TEST(Session, AliasesCannotHideGlobalCommands) {
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());
    auto path = temp.filePath("aliases.ini");
    {
        QSettings ini(path, QSettings::IniFormat);
        ini.setValue("aliases/ОТ", "line");
        ini.setValue("aliases/LINE", "UNDO");
        ini.setValue("aliases/X", "missing");
    }
    CommandSession session;
    session.loadAliases(path);
    EXPECT_FALSE(session.completions().contains("X"));
    session.execute("от 0,0 2,3");
    session.execute("LINE 2,3 4,5");
    session.execute("_ОТ 5,6 7,8");
    EXPECT_EQ(session.document().size(), 2u);
}

TEST(Plugins, ExplicitLoadRegistersARealUndoableCommand) {
    CommandSession session;
    EXPECT_FALSE(session.loadPlugin("not-an-absolute-path").isEmpty());
    const QString path = QString::fromUtf8(PD_SAMPLE_PATH);
    const auto error = session.loadPlugin(path);
    ASSERT_TRUE(error.isEmpty()) << error.toStdString();
    EXPECT_FALSE(session.loadPlugin(path).isEmpty());
    EXPECT_TRUE(session.completions().contains("_SAMPLELINE"));
    session.execute("_SAMPLELINE");
    ASSERT_EQ(session.document().size(), 1u);
    auto* line =
        dynamic_cast<const pd::Line*>(session.document().entity(session.document().entityIds().front()));
    ASSERT_NE(line, nullptr);
    EXPECT_DOUBLE_EQ(line->start().x, -60);
    session.execute("_UNDO");
    EXPECT_TRUE(session.document().size() == 0);
}

TEST(Plugins, RejectsIncompleteAbiWithoutRegisteringACommand) {
    CommandSession session;
    const auto vocabulary = session.completions();
    EXPECT_FALSE(session.loadPlugin(QString::fromUtf8(PD_SHORT_TABLE_PATH)).isEmpty());
    EXPECT_EQ(session.completions(), vocabulary);
    EXPECT_TRUE(session.document().size() == 0);
}

TEST(Session, NativeFrameCommandPublishesOnePacketAndUndoAlsoRemovesItsLayer) {
    CommandSession session;
    const auto path = QString::fromUtf8(PD_FRAME_PATH);
    ASSERT_TRUE(session.loadPlugin(path).isEmpty());
    Language language;
    ASSERT_TRUE(language.setRussian(true));
    EXPECT_TRUE(session.completions().contains("РАМКА"));
    EXPECT_TRUE(session.completions().contains("_FRAME"));
    const auto& doc = session.document();
    session.execute("_РАМКА");
    EXPECT_EQ(doc.revision(), 0u);
    int packets = 0;
    QObject::connect(&session, &CommandSession::documentChanged, &session,
                     [&](const QList<pd::Change>& batch) {
                         ++packets;
                         EXPECT_EQ(batch.size(), 7);
                     });
    QStringList log;
    QObject::connect(&session, &CommandSession::consoleOutput, &session,
                     [&](bool, QString msg) { log << msg; });
    session.execute("рамка 100");
    ASSERT_EQ(doc.size(), 6u);
    EXPECT_EQ(doc.revision(), 1u);
    EXPECT_EQ(packets, 1);
    EXPECT_FALSE(log.isEmpty());
    auto ids = doc.query({{0, 0}, {100, 50}});
    EXPECT_EQ(ids.size(), 6u);
    EXPECT_EQ(doc.query({{97, 47}, {99, 49}}).size(), 2u);
    EXPECT_DOUBLE_EQ(doc.extents().max().x, 100);
    ASSERT_EQ(doc.layerIds().size(), 2u);
    EXPECT_EQ(doc.layer(doc.layerIds().back())->name, "Frame");
    session.execute("_UNDO");
    EXPECT_EQ(packets, 2);
    EXPECT_TRUE(doc.query({{0, 0}, {100, 50}}).empty());
    EXPECT_EQ(doc.layerIds().size(), 1u);
    EXPECT_FALSE(doc.canUndo());
    ASSERT_TRUE(session.unloadPlugin(path).isEmpty());
    EXPECT_FALSE(session.completions().contains("_FRAME"));
    session.execute("_REDO");
    EXPECT_EQ(doc.size(), 6u); // no vtables/allocations in the unloaded module
    ASSERT_TRUE(language.setRussian(false));
}
TEST(Session, RunScriptPreservesQuotedUnicodePathAndRegistersBilingualCommand) {
    CommandSession session;
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());
    auto path = temp.filePath("мой  чертёж.pdl");
    ASSERT_TRUE(QFile::copy(QString::fromUtf8(PD_SCRIPT_PATH), path));
    QStringList log;
    QObject::connect(&session, &CommandSession::consoleOutput, &session,
                     [&](bool e, QString msg) { log << (e ? "err: " + msg : msg); });
    Language language;
    ASSERT_TRUE(language.setRussian(true));
    session.execute("запуститьскрипт \"" + path + "\"");
    const auto& doc = session.document();
    ASSERT_EQ(doc.size(), 9u);
    ASSERT_EQ(doc.revision(), 1u);
    EXPECT_TRUE(log.contains("Created eight holes; _SHIFTSEL is ready."));
    EXPECT_TRUE(session.completions().contains("СДВИГВЫБОРА"));
    auto selected = doc.entityIds().front();
    session.setSelectionProvider([=] { return std::vector<pd::EntityId>{selected}; });
    session.execute("сдвигвыбора 17");
    ASSERT_EQ(doc.revision(), 2u);
    EXPECT_DOUBLE_EQ(dynamic_cast<const pd::Circle&>(*doc.entity(selected)).center().x, 17);
    session.execute("_UNDO");
    EXPECT_DOUBLE_EQ(dynamic_cast<const pd::Circle&>(*doc.entity(selected)).center().x, 0);
    session.execute("_UNDO");
    EXPECT_TRUE(doc.extents().empty());
    EXPECT_EQ(doc.layerIds().size(), 1u);
    EXPECT_TRUE(session.completions().contains("_SHIFTSEL"));
    ASSERT_TRUE(language.setRussian(false));
    QFile bad(path);
    ASSERT_TRUE(bad.open(QIODevice::WriteOnly | QIODevice::Truncate));
    bad.write("(with-transaction doc \"bad\" (add_circle doc (list 0 0) 3) (error \"abort here\"))");
    bad.close();
    auto revision = doc.revision();
    session.execute("_RUNSCRIPT \"" + path + "\"");
    EXPECT_TRUE(doc.canRedo());
    EXPECT_EQ(doc.revision(), revision);
    EXPECT_TRUE(doc.extents().empty());
    ASSERT_FALSE(log.isEmpty());
    EXPECT_TRUE(log.back().startsWith("err:"));
    EXPECT_TRUE(log.back().contains("abort here"));
    EXPECT_TRUE(log.back().contains(path));
}

TEST(Session, DirectoryFailureIsLoggedAndDoesNotHideWorkingLibrariesOrAliases) {
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());
    auto bad = QString::fromUtf8(PD_SHORT_TABLE_PATH), good = QString::fromUtf8(PD_FRAME_PATH);
    auto suffix = good.mid(good.lastIndexOf('.'));
    auto first = temp.filePath("a_bad" + suffix), second = temp.filePath("б_рамка" + suffix);
    ASSERT_TRUE(QFile::copy(bad, first));
    ASSERT_TRUE(QFile::copy(good, second));
    const auto path = temp.filePath("aliases.ini");
    {
        QSettings ini(path, QSettings::IniFormat);
        ini.setValue("aliases/F", "FRAME");
    }
    CommandSession session;
    session.loadAliases(path);
    EXPECT_FALSE(session.completions().contains("F"));
    QStringList failures, loaded;
    QObject::connect(&session, &CommandSession::consoleOutput, &session,
                     [&](bool err, const QString& m) { (err ? failures : loaded) << m; });
    session.loadPluginDirectory(temp.path());
    ASSERT_EQ(failures.size(), 1);
    EXPECT_TRUE(failures.front().contains("incompatible"));
    ASSERT_EQ(loaded.size(), 1);
    EXPECT_TRUE(loaded.front().contains("б_рамка"));
    EXPECT_TRUE(session.completions().contains("F"));
    session.execute("f 44.5");
    EXPECT_DOUBLE_EQ(session.document().extents().max().x, 44.5);
    EXPECT_TRUE(session.unloadPlugin(second).isEmpty());
    EXPECT_FALSE(session.completions().contains("F"));
    session.execute("_F");
    EXPECT_EQ(session.document().revision(), 1u);
}

TEST(Session, BilingualFileCommandsPreserveUnicodePathsAndResetOnlyOnSuccessfulOpen) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    QString p = tmp.filePath("чертёж  с пробелом.dxf"), svg = tmp.filePath("figure.svg"),
            pdf = tmp.filePath("figure.pdf");
    CommandSession session;
    Language lang;
    ASSERT_TRUE(lang.setRussian(true));
    EXPECT_TRUE(session.completions().contains("СОХРАНИТЬКАК"));
    session.execute("_LINE 1.125,2 8,11");
    session.execute("сохранитькак \"" + p + "\"");
    ASSERT_TRUE(QFile::exists(p));
    EXPECT_EQ(session.currentFile(), p);
    EXPECT_FALSE(session.modified());
    session.execute("_LINE 4,2 9,13");
    session.execute("сохранить");
    session.execute("_UNDO");
    ASSERT_EQ(session.document().size(), 1u);
    QStringList diagnostics;
    QObject::connect(&session, &CommandSession::consoleOutput, &session,
                     [&](bool, QString m) { diagnostics << m; });
    auto revision = session.document().revision();
    session.execute("_OPEN \"" + tmp.filePath("absent.dxf") + "\"");
    EXPECT_EQ(session.document().revision(), revision);
    EXPECT_TRUE(session.document().canRedo());
    EXPECT_EQ(session.currentFile(), p);
    EXPECT_FALSE(diagnostics.empty());
    auto info = session.execute("открыть \"" + p + "\"");
    EXPECT_TRUE(info.contains("Объектов: 2"));
    EXPECT_EQ(session.document().size(), 2u);
    EXPECT_FALSE(session.document().canUndo());
    EXPECT_FALSE(session.document().canRedo());
    EXPECT_FALSE(session.modified());
    session.execute("экспортsvg " + svg);
    session.execute("_EXPORTPDF " + pdf);
    EXPECT_TRUE(QFile::exists(svg));
    EXPECT_TRUE(QFile::exists(pdf));
    QFile svgFile(svg);
    ASSERT_TRUE(svgFile.open(QIODevice::ReadOnly));
    QXmlStreamReader xml(&svgFile);
    int paths = 0;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == QStringLiteral("path"))
            ++paths;
    }
    EXPECT_FALSE(xml.hasError()) << xml.errorString().toStdString();
    EXPECT_EQ(paths, 2);
    EXPECT_EQ(session.currentFile(), p);
    ASSERT_TRUE(lang.setRussian(false));
}
TEST(Session, PartialForeignImportRequiresSeparateSaveAsAndReportsFirstLineAndCount) {
    QTemporaryDir tmp;
    CommandSession session;
    auto p = tmp.filePath("foreign.dxf");
    QFile file(p);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    const QByteArray source = "0\nSECTION\n2\nENTITIES\n0\nHATCH\n0\nHATCH\n0\nLINE\n10\n1\n20\n2\n11\n4\n21"
                              "\n6\n0\nENDSEC\n0\nEOF\n";
    file.write(source);
    file.close();
    QStringList logs;
    QObject::connect(&session, &CommandSession::consoleOutput, &session, [&](bool, QString s) { logs << s; });
    EXPECT_TRUE(session.execute("_OPEN " + p).contains("2 skipped"));
    ASSERT_EQ(logs.size(), 1);
    EXPECT_TRUE(logs.front().contains("HATCH"));
    EXPECT_TRUE(logs.front().contains("Line 5"));
    EXPECT_TRUE(session.execute("_SAVE").contains("Save As"));
    session.execute("_SAVEAS " + tmp.filePath("copy.dxf"));
    EXPECT_TRUE(QFile::exists(tmp.filePath("copy.dxf")));
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    EXPECT_EQ(file.readAll(), source);
}

// The LINE options a typed chain understands: C closes back to the first point, U drops the point
// just given.
TEST(Session, TypedLineChainClosesAndTakesPointsBack) {
    CommandSession s;
    auto& d = s.document();
    s.execute("_LINE 0,0 10,0 10,10 C");
    ASSERT_EQ(d.size(), 3u);
    const auto* closing = dynamic_cast<const pd::Line*>(d.entity(3));
    ASSERT_NE(closing, nullptr);
    EXPECT_EQ(closing->start(), (pd::Vec2{10, 10}));
    EXPECT_EQ(closing->end(), (pd::Vec2{0, 0}));
    EXPECT_TRUE(d.undo()); // one command, one undo step
    EXPECT_EQ(d.size(), 0u);
    s.execute("_LINE 0,0 10,0 U 0,10");
    ASSERT_EQ(d.size(), 1u);
    const auto* second = dynamic_cast<const pd::Line*>(d.entity(d.entityIds().back()));
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(second->end(), (pd::Vec2{0, 10}));
}

// AutoCAD coordinate entry: "x,y" absolute, "@dx,dy" and "@d<a" relative to the last point, "d<a"
// polar from the origin. Typists expect all four at the prompt, not only the first.
TEST(Session, CoordinateEntryAcceptsRelativeAndPolarForms) {
    CommandSession s;
    auto& d = s.document();
    s.execute("_LINE 0,0 @10,0 @0,10");
    ASSERT_EQ(d.size(), 2u);
    const auto* first = dynamic_cast<const pd::Line*>(d.entity(1));
    const auto* second = dynamic_cast<const pd::Line*>(d.entity(2));
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_DOUBLE_EQ(first->end().x, 10);
    EXPECT_DOUBLE_EQ(first->end().y, 0);
    EXPECT_DOUBLE_EQ(second->end().x, 10);
    EXPECT_DOUBLE_EQ(second->end().y, 10);

    // Relative polar: ten units up from the point that was just picked.
    s.execute("_LINE 100,0 @10<90");
    ASSERT_EQ(d.size(), 3u);
    const auto* polar = dynamic_cast<const pd::Line*>(d.entity(3));
    ASSERT_NE(polar, nullptr);
    EXPECT_NEAR(polar->end().x, 100, 1e-9);
    EXPECT_NEAR(polar->end().y, 10, 1e-9);

    // Absolute polar is measured from the origin, not from the previous point.
    s.execute("_LINE 0,0 20<0");
    ASSERT_EQ(d.size(), 4u);
    const auto* absolute = dynamic_cast<const pd::Line*>(d.entity(4));
    ASSERT_NE(absolute, nullptr);
    EXPECT_NEAR(absolute->end().x, 20, 1e-9);
    EXPECT_NEAR(absolute->end().y, 0, 1e-9);

    // A leading '@' before anything was ever picked has no point to be relative to: the prompt
    // repeats and nothing is drawn. (After a completed command the last point is remembered, which
    // is also what a CAD prompt does, so this needs a session that has never picked.)
    CommandSession fresh;
    fresh.execute("_LINE @5,5");
    EXPECT_TRUE(fresh.editor().active());
    EXPECT_EQ(fresh.document().size(), 0u);
    fresh.editor().cancel();
}

TEST(Session, ClipboardCopiesCutsAndPastesAsOneUndoStep) {
    CommandSession s;
    auto& d = s.document();
    s.execute("_LINE 0,0 10,0");
    s.execute("_CIRCLE 20,0 5");
    pd::Layer band("Band");
    band.color = {12, 200, 90};
    pd::LayerId lid = 0;
    d.transact([&](pd::Transaction& tx) { lid = tx.addLayer(band); });
    d.transact([&](pd::Transaction& tx) {
        auto c = d.entity(2)->clone();
        c->traits.layer = lid;
        tx.replace(2, *c);
    });
    std::vector<pd::EntityId> ids{1, 2};
    s.setSelectionProvider([&] { return ids; });
    EXPECT_TRUE(s.execute("_COPYCLIP").contains("2"));
    ASSERT_FALSE(s.clipboardEmpty());
    EXPECT_EQ(s.clipboardSize(), 2u);
    auto before = d.size();
    auto paste = s.execute("_PASTECLIP 100,0");
    EXPECT_TRUE(paste.contains("2")) << paste.toStdString();
    EXPECT_EQ(d.size(), before + 2);
    const pd::Line* line = nullptr;
    for (auto id : d.entityIds())
        if (auto l = dynamic_cast<const pd::Line*>(d.entity(id)); l && l->start().x > 50)
            line = l;
    ASSERT_NE(line, nullptr);
    EXPECT_DOUBLE_EQ(line->start().x, 95); // base point was the line's own centre (5,0)
    EXPECT_TRUE(d.undo());
    EXPECT_EQ(d.size(), before);

    auto cut = s.execute("_CUTCLIP");
    EXPECT_TRUE(cut.contains("2")) << cut.toStdString();
    EXPECT_EQ(d.size(), 0u);
    EXPECT_TRUE(d.undo());
    EXPECT_EQ(d.size(), 2u);
    EXPECT_EQ(d.entityIds().back(), 2u);

    // Pasting into the same document keeps layer identity by name.
    s.execute("_PASTECLIP 0,50");
    auto pasted = d.entityIds().back();
    EXPECT_EQ(d.entity(pasted)->traits.layer, lid);
    ids.clear();
    s.setSelectionProvider([&] { return ids; });
    EXPECT_TRUE(s.execute("_COPYCLIP").contains("Select"));
    EXPECT_TRUE(s.execute("_PASTECLIP 5,5").contains("2"));
}

TEST(Session, RepeatLastAndRecentCommandsMatchTheTypedNames) {
    CommandSession s;
    s.execute("L 0,0 10,0");
    ASSERT_TRUE(s.recentCommands().contains("LINE"));
    EXPECT_EQ(s.recentCommands().front(), "LINE");
    auto reply = s.repeatLast();
    EXPECT_TRUE(reply.contains("point")) << reply.toStdString();
    EXPECT_TRUE(s.editor().active());
    s.execute("0,5"); // the repeated LINE draws segment after segment ...
    s.execute("10,5");
    EXPECT_TRUE(s.editor().active());
    EXPECT_EQ(s.document().size(), 2u);
    s.execute(""); // ... until Enter ends it, as in AutoCAD
    EXPECT_FALSE(s.editor().active());
    CommandSession fresh;
    EXPECT_TRUE(fresh.repeatLast().contains("No previous command"));
}

TEST(Session, RibbonStyleControlsUseOneUndoStepPerSelection) {
    CommandSession s;
    auto& d = s.document();
    s.execute("_LINE 0,0 10,0");
    s.execute("_LINE 0,10 10,10");
    pd::Layer ink("Ink");
    pd::LayerId lid = 0;
    d.transact([&](pd::Transaction& tx) { lid = tx.addLayer(ink); });
    std::vector<pd::EntityId> ids{1, 2};
    auto rev = d.revision();
    EXPECT_TRUE(s.setLayer(ids, lid).contains("2"));
    EXPECT_EQ(d.revision(), rev + 1);
    EXPECT_EQ(d.entity(1)->traits.layer, lid);
    EXPECT_EQ(d.entity(2)->traits.layer, lid);
    EXPECT_TRUE(d.undo());
    EXPECT_EQ(d.entity(1)->traits.layer, pd::DefaultLayer);
    EXPECT_TRUE(s.setColor(ids, pd::Color{7, 8, 9}).contains("2"));
    EXPECT_EQ(std::get<pd::Color>(d.entity(2)->traits.color), (pd::Color{7, 8, 9}));
    EXPECT_TRUE(d.undo());
    EXPECT_TRUE(s.setLineWeight(ids, 0.6).contains("2"));
    EXPECT_DOUBLE_EQ(std::get<double>(d.entity(1)->traits.lineWeight), 0.6);
    EXPECT_TRUE(d.undo());
    EXPECT_TRUE(s.setColor({}, pd::ByLayer{}).contains("Select"));
    // A locked layer is reported, not silently edited.
    pd::Layer locked("Locked");
    d.transact([&](pd::Transaction& tx) { lid = tx.addLayer(locked); });
    d.transact([&](pd::Transaction& tx) {
        auto e = d.entity(1)->clone();
        e->traits.layer = lid;
        tx.replace(1, *e);
    });
    locked.isLocked = true;
    d.transact([&](pd::Transaction& tx) { tx.updateLayer(lid, locked); });
    EXPECT_TRUE(s.setColor(ids, pd::ByLayer{}).contains("locked"));
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv); // No window, GL context or display server needed.
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
