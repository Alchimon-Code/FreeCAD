// SPDX-License-Identifier: LGPL-2.1-or-later
#include "plugin_manager.h"
#include "script_engine.hpp"
#include <gtest/gtest.h>
#include <planedraft/entities/primitives.hpp>

namespace {
using namespace pd;
struct Host : testing::Test {
    Document doc;
    CommandRegistry registry{{"LINE", "ОТРЕЗОК"}};
    PluginManager plugins{registry};
    std::vector<EntityId> selection;
    std::vector<std::string> out, err;
    ExtensionContext ctx{doc, [&] { return selection; },
                         [&](bool e, std::string_view s) { (e ? err : out).emplace_back(s); }};
    std::unique_ptr<IScriptEngine> lisp = makeLispEngine(registry);
    void exec(std::string_view s) { lisp->execute(s, "test.pdl", ctx); }
};
TEST_F(Host, V2SealsOnlyOnSuccessfulReturnAndDiscardsUnclosedPoisonedOrCancelledGroup) {
    plugins.load(PD_PROBE_PATH);
    auto old = doc.add(Circle({100, 100}, 1));
    ASSERT_TRUE(doc.undo());
    auto rev = doc.revision();
    for (auto args : {"forget", "error", "poison"}) {
        EXPECT_THROW(registry.run("PROBE", ctx, args), std::runtime_error);
        EXPECT_EQ(doc.revision(), rev);
        EXPECT_TRUE(doc.extents().empty());
        EXPECT_TRUE(doc.canRedo());
    }
    EXPECT_NO_THROW(registry.run("PROBE", ctx, "cancel"));
    EXPECT_EQ(doc.revision(), rev);
    EXPECT_TRUE(doc.redo());
    EXPECT_TRUE(doc.entity(old));
}
TEST_F(Host, RealDllRoundTripsDescriptorsSnapshotOwnershipSelectionAndUndo) {
    plugins.load(PD_PROBE_PATH);
    registry.run("PROBE", ctx, "roundtrip");
    ASSERT_EQ(doc.size(), 1u);
    selection = doc.entityIds();
    registry.run("PROBE2", ctx, "read");
    EXPECT_TRUE(err.empty());
    EXPECT_EQ(doc.revision(), 1u);
    EXPECT_TRUE(doc.undo());
    EXPECT_TRUE(doc.extents().empty());
    plugins.unload(PD_PROBE_PATH);
    EXPECT_TRUE(registry.entries().empty());
    plugins.load(PD_PROBE_PATH);
    EXPECT_EQ(registry.entries().size(), 2u);
}
TEST_F(Host, RejectAndUnloadPartialInitWithoutPublishingAnyCallbacks) {
    for (int i = 0; i < 3; ++i) {
        EXPECT_THROW(plugins.load(PD_FAIL_PATH), std::runtime_error);
        EXPECT_TRUE(registry.entries().empty());
    }
    EXPECT_THROW(plugins.load(PD_SHORT_TABLE_PATH), std::runtime_error);
    exec("(defcommand \"USERCMD\" \"ПРОБА\" (print 123))");
    EXPECT_THROW(plugins.load(PD_PROBE_PATH), std::runtime_error);
    ASSERT_EQ(registry.entries().size(), 1u);
    registry.run("USERCMD", ctx, {});
    EXPECT_EQ(out.back(), "123");
}
TEST_F(Host, LegacyAndV2BinariesUseOnlyPublicEntryPoints) {
    // Never scan the developer's production plugins/ during CTest. Discovery has its own
    // isolated temporary-directory integration test (including an incompatible library).
    plugins.load(PD_SAMPLE_PATH);
    plugins.load(PD_FRAME_PATH);
    EXPECT_TRUE(err.empty());
    ASSERT_EQ(registry.entries().size(), 2u);
    EXPECT_EQ(registry.resolve("РАМКА", true), "FRAME");
    EXPECT_TRUE(registry.resolve("РАМКА", false).empty());
    registry.run("SAMPLELINE", ctx, {});
    ASSERT_EQ(doc.size(), 1u);
    EXPECT_TRUE(doc.undo());
    registry.run("FRAME", ctx, "40");
    ASSERT_EQ(doc.query({{0, 0}, {40, 20}}).size(), 6u);
    EXPECT_EQ(doc.layerIds().size(), 2u);
    EXPECT_TRUE(doc.undo());
    EXPECT_EQ(doc.layerIds().size(), 1u);
    EXPECT_TRUE(doc.extents().empty());
}
TEST_F(Host, LispErrorRollsBackLayersEntitiesIndexHistoryAndPendingCommands) {
    doc.add(Circle({}, 1));
    ASSERT_TRUE(doc.undo());
    auto revision = doc.revision();
    size_t notices = 0;
    auto token = doc.observe([&](auto) { ++notices; });
    EXPECT_THROW(exec(R"((defcommand "GHOST" "" (print 1))
        (with-transaction doc "example" (define l (add_layer doc "tmp"))
          (add_line doc (list 0 0) (list 8 9) l) (eprint "diagnostic") (error "stop")))"),
                 std::runtime_error);
    EXPECT_EQ(doc.revision(), revision);
    EXPECT_EQ(notices, 0u);
    EXPECT_TRUE(doc.extents().empty());
    EXPECT_TRUE(doc.query({{0, 0}, {9, 10}}).empty());
    EXPECT_EQ(doc.layerIds().size(), 1u);
    EXPECT_TRUE(registry.entries().empty());
    ASSERT_EQ(err.size(), 2u);
    EXPECT_EQ(err[0], "diagnostic");
    EXPECT_NE(err.back().find("test.pdl:3:"), std::string::npos);
    // The same guarantee on a real geometry validation error, without an explicit (error ...).
    EXPECT_THROW(exec(R"((with-transaction doc "bad geometry" (add_circle doc (list 1 2) 7)
                          (add_line doc (list 2 3) (list 2 3))))"),
                 std::runtime_error);
    EXPECT_EQ(doc.revision(), revision);
    EXPECT_EQ(notices, 0u);
    EXPECT_TRUE(doc.extents().empty());
    EXPECT_TRUE(doc.redo());
}
TEST_F(Host, LispLexicalCommandsAndDomUseStableIdsAndReadStagedObjects) {
    exec(R"((let ((dx 3))
        (defun move (e) (transform doc e (list 1 0 dx 0 1 0)))
        (defcommand "MOVESEL" "СДВИГ" (with-transaction doc "move" (foreach (id (selection doc)) (move id)))))
        (with-transaction doc "draw" (define l (add_layer doc "L"))
            (define p (add_polyline doc (list (list 1 2 .5) (list 4 7)) #f l))
            (print (get (entity doc p) "vertices")) (print (get (layer doc l) "name"))
            (define t (add_circle doc (list 0 0) 2 l)) (delete doc t)))");
    EXPECT_EQ(doc.size(), 1u);
    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(out.back(), "L");
    selection = doc.entityIds();
    auto bounds = doc.extents();
    registry.run("MOVESEL", ctx, {});
    EXPECT_EQ(doc.revision(), 2u);
    EXPECT_NEAR(doc.extents().min().x, bounds.min().x + 3, 1e-9);
    EXPECT_TRUE(doc.undo());
    EXPECT_EQ(doc.extents(), bounds);
    EXPECT_TRUE(doc.undo());
    EXPECT_TRUE(doc.extents().empty());
    lisp.reset();
    EXPECT_TRUE(registry.entries().empty());
}
TEST_F(Host, LispRejectsWritesOutsideBlocksInvalidSyntaxAndRegistrationCollisions) {
    for (auto s : {"(add_line doc (list 0 0) (list 2 1))", "(print 1",
                   "(with-transaction doc \"t\" (add_circle doc (list 0 0) -1))",
                   "(defcommand \"LINE\" \"\" 1)", "(defcommand \"ABC\" \"ОТРЕЗОК\" 1)",
                   "(with-transaction doc \"outer\" (with-transaction doc \"inner\" 1))"}) {
        EXPECT_THROW(exec(s), std::runtime_error);
        EXPECT_EQ(doc.revision(), 0u);
        EXPECT_TRUE(registry.entries().empty());
    }
    exec("(print 18446744073709551615)");
    EXPECT_EQ(out.back(), "18446744073709551615");
}
TEST_F(Host, RejectsBadVersionOrSizeBeforeInitAndDoesNotDowngrade) {
    for (auto path : {PD_WRONG_VERSION_PATH, PD_SHORT_V2_PATH}) {
        try {
            plugins.load(path);
            FAIL() << "accepted incompatible descriptor";
        } catch (const std::runtime_error& e) {
            EXPECT_TRUE(std::string_view(e.what()).starts_with("incompatible v2 descriptor:")) << e.what();
        }
        EXPECT_TRUE(registry.entries().empty());
    }
}
TEST_F(Host, LegacyBinaryRetainsPerAddUndoAndCanRecoverFromAnInvalidAdd) {
    plugins.load(PD_LEGACY_EDITS_PATH);
    registry.run("OLDEDITS", ctx, {});
    EXPECT_EQ(doc.revision(), 2u);
    ASSERT_TRUE(doc.undo());
    EXPECT_EQ(doc.size(), 1u);
    ASSERT_TRUE(doc.undo());
    EXPECT_TRUE(doc.extents().empty());
}
TEST_F(Host, InFlightCallbackKeepsModuleMappedUntilItReturns) {
    plugins.load(PD_FRAME_PATH);
    bool logged = false;
    ctx.output = [&](bool, std::string_view) {
        plugins.unload(PD_FRAME_PATH);
        logged = true;
        EXPECT_TRUE(registry.entries().empty());
    };
    registry.run("FRAME", ctx, "64.5");
    EXPECT_TRUE(logged);
    ASSERT_EQ(doc.size(), 6u);
    EXPECT_DOUBLE_EQ(doc.extents().max().x, 64.5);
    EXPECT_TRUE(doc.undo());
    EXPECT_TRUE(doc.redo()); // contains only host-owned values and vtables
}
TEST_F(Host, ReentrantInstallationCannotStealPendingCommand) {
    ctx.output = [&](bool, std::string_view) { EXPECT_THROW(plugins.load(PD_FRAME_PATH), std::logic_error); };
    exec("(defcommand \"FRAME\" \"РАМКА\" #t) (print 1)");
    ASSERT_EQ(registry.entries().size(), 1u);
    registry.run("FRAME", ctx, {});
    EXPECT_TRUE(doc.extents().empty());
}
TEST_F(Host, InvalidSourceIsDiagnosedWithoutPublishing) {
    for (const auto& s :
         {std::string("(print \"x\0y\")", 13), std::string("(print \"\xc0\x80\")"), std::string(100, '(')}) {
        EXPECT_THROW(exec(s), std::runtime_error);
        EXPECT_EQ(doc.revision(), 0u);
    }
}
TEST_F(Host, DeepValueIsRejectedWithoutPublishing) {
    EXPECT_THROW(exec("(define x (list)) (for (i 200) (define x (list x)))"), std::runtime_error);
    EXPECT_EQ(doc.revision(), 0u);
}
TEST_F(Host, RecursionLimitReturnsOneLocatedErrorToTheCaller) {
    EXPECT_THROW(exec("(defun recurse ()\n  (recurse))\n(recurse)"), std::runtime_error);
    ASSERT_EQ(err.size(), 1u);
    EXPECT_EQ(err.front(), "test.pdl:2:3: evaluation limit exceeded");
    EXPECT_EQ(doc.revision(), 0u);
}
} // namespace
