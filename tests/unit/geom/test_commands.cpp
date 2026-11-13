#include <fem/geom/commands.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <utility>
#include <variant>
#include <vector>

using fem::geom::CommandName;
using fem::geom::CommandSession;
using fem::geom::Drawing;
using fem::geom::DrawingEdit;
using fem::geom::Entity;
using fem::geom::EntityId;
using fem::geom::LineEntity;
using fem::geom::MessageId;
using fem::geom::OptionId;
using fem::geom::PointEntity;
using fem::geom::PolylineEntity;
using fem::geom::PromptId;
using fem::geom::Vec3;

namespace {

template <typename Shape>
std::size_t count(const Drawing& drawing)
{
    return static_cast<std::size_t>(std::ranges::count_if(drawing.entities(), [](const Entity& e) {
        return std::holds_alternative<Shape>(e.shape);
    }));
}

bool hasMessage(const std::vector<fem::geom::Message>& messages, MessageId id)
{
    return std::ranges::any_of(messages, [id](const fem::geom::Message& m) { return m.id == id; });
}

// A drawing with two lines, for the editing commands.
std::vector<EntityId> twoLines(Drawing& drawing)
{
    const EntityId a = drawing.nextId();
    const EntityId b = drawing.nextId();
    drawing.apply(
        {.removed = {},
         .added = {{.id = a, .shape = LineEntity{.start = Vec3(0, 0, 0), .end = Vec3(1, 0, 0)}},
                   {.id = b, .shape = LineEntity{.start = Vec3(0, 1, 0), .end = Vec3(1, 1, 0)}}}});
    return {a, b};
}

const LineEntity& lineOf(const Drawing& drawing, EntityId id)
{
    const Entity* entity = drawing.find(id);
    REQUIRE(entity != nullptr);
    const auto* line = std::get_if<LineEntity>(&entity->shape);
    REQUIRE(line != nullptr);
    return *line;
}

} // namespace

// REQ-005-15, REQ-005-31
TEST_CASE("POINT makes a point per point given, one undo step", "[geom][commands]")
{
    Drawing drawing;
    CommandSession session(drawing);
    CHECK(session.prompt().id == PromptId::Command);
    session.feedText("po");
    CHECK(session.prompt().id == PromptId::Point);
    CHECK(session.wantsPoint());
    session.feedText("1,2");
    session.feedPoint(Vec3(3, 4, 5));
    CHECK(count<PointEntity>(drawing) == 2);
    session.enter();
    CHECK_FALSE(session.isActive());
    const auto edits = session.takeFinishedEdits();
    REQUIRE(edits.size() == 1);
    CHECK(edits[0].added.size() == 2);
    CHECK(hasMessage(session.takeMessages(), MessageId::PointsCreated));
}

// REQ-005-16, 18: LINE with Undo, Close, Enter and Esc.
TEST_CASE("LINE draws segments with undo and close", "[geom][commands]")
{
    Drawing drawing;
    CommandSession session(drawing);
    session.start(CommandName::Line);
    CHECK(session.prompt().id == PromptId::FirstPoint);
    CHECK_FALSE(session.referencePoint().has_value());
    session.feedText("0,0");
    CHECK(session.prompt().id == PromptId::NextPoint);
    session.feedText("@4,0");
    session.feedText("@0,3");
    CHECK(count<LineEntity>(drawing) == 2);
    CHECK(session.prompt().options == std::vector<OptionId>{OptionId::Undo, OptionId::Close});

    // The rubber band starts at the last point.
    const auto preview = session.preview(Vec3(9, 9, 0));
    REQUIRE(preview.rubberBand.has_value());
    CHECK(preview.rubberBand.value_or(std::pair{Vec3::Zero(), Vec3::Zero()}).first ==
          Vec3(4, 3, 0));

    SECTION("undo then close")
    {
        session.feedText("u");
        CHECK(count<LineEntity>(drawing) == 1);
        CHECK(session.referencePoint() == Vec3(4, 0, 0));
        session.feedText("C"); // needs two segments
        CHECK(hasMessage(session.takeMessages(), MessageId::NeedTwoSegments));
        session.feedText("@0,3");
        session.feedText("c");
        CHECK_FALSE(session.isActive());
        CHECK(count<LineEntity>(drawing) == 3);
        const auto edits = session.takeFinishedEdits();
        REQUIRE(edits.size() == 1);
        CHECK(edits[0].added.size() == 3);
        CHECK(edits[0].removed.empty());
    }
    SECTION("Esc keeps the segments")
    {
        session.cancel();
        CHECK_FALSE(session.isActive());
        CHECK(count<LineEntity>(drawing) == 2);
        CHECK(session.takeFinishedEdits().size() == 1);
    }
    SECTION("undo back to the first point")
    {
        session.feedText("U");
        session.feedText("U");
        session.feedText("U");
        CHECK(session.prompt().id == PromptId::FirstPoint);
        CHECK(drawing.empty());
        session.enter();
        CHECK(session.takeFinishedEdits().empty()); // nothing left to undo
    }
}

// REQ-005-17
TEST_CASE("PLINE makes one polyline", "[geom][commands]")
{
    Drawing drawing;
    CommandSession session(drawing);
    session.start(CommandName::Pline);
    session.feedPoint(Vec3(0, 0, 0));
    session.feedPoint(Vec3(1, 0, 0));
    session.feedPoint(Vec3(1, 1, 2));
    CHECK(drawing.empty()); // built as a preview
    CHECK(session.preview(std::nullopt).ghosts.size() == 1);

    SECTION("open with Enter")
    {
        session.enter();
        REQUIRE(count<PolylineEntity>(drawing) == 1);
        const auto& polyline = std::get<PolylineEntity>(drawing.entities()[0].shape);
        CHECK(polyline.vertices.size() == 3);
        CHECK_FALSE(polyline.closed);
    }
    SECTION("closed with C")
    {
        session.feedText("c");
        REQUIRE(count<PolylineEntity>(drawing) == 1);
        CHECK(std::get<PolylineEntity>(drawing.entities()[0].shape).closed);
    }
    SECTION("a single vertex makes nothing")
    {
        session.feedText("u");
        session.feedText("u");
        session.enter();
        CHECK(drawing.empty());
    }
    CHECK_FALSE(session.isActive());
}

// REQ-005-06, 07, 08: repetition, cancelling by a new command, messages.
TEST_CASE("session repeats, cancels and reports errors", "[geom][commands]")
{
    Drawing drawing;
    CommandSession session(drawing);

    session.feedText("circle");
    CHECK(hasMessage(session.takeMessages(), MessageId::UnknownCommand));
    session.feedText("1,");
    CHECK(hasMessage(session.takeMessages(), MessageId::InvalidInput));

    session.start(CommandName::Line);
    session.feedText("@1,0"); // no previous point at all
    CHECK(hasMessage(session.takeMessages(), MessageId::NoPreviousPoint));
    session.feedText("0,0");
    session.feedText("5"); // direct distance along the cursor direction
    CHECK(lineOf(drawing, drawing.entities()[0].id).end == Vec3(5, 0, 0));

    // A new command ends the running one.
    session.feedText("point");
    CHECK(session.activeCommand() == CommandName::Point);
    CHECK(session.takeFinishedEdits().size() == 1);
    session.cancel();

    // Enter on an empty line repeats the last command.
    session.feedText("");
    CHECK(session.activeCommand() == CommandName::Point);
    session.cancel();

    // @ refers to the last point given, also in a new command.
    session.start(CommandName::Point);
    session.feedText("@0,1");
    CHECK(std::get<PointEntity>(drawing.entities().back().shape).position == Vec3(5, 1, 0));
    session.cancel();

    // UNDO is handed to the caller.
    session.feedText("u");
    CHECK(session.takeUndoRequest() == CommandName::Undo);
    session.feedText("REDO");
    CHECK(session.takeUndoRequest() == CommandName::Redo);
    CHECK_FALSE(session.isActive());
}

// REQ-005-27, 28: MOVE with a selection made before, keeping the ids.
TEST_CASE("MOVE translates the selected entities", "[geom][commands]")
{
    Drawing drawing;
    const auto ids = twoLines(drawing);
    CommandSession session(drawing);
    session.start(CommandName::Move, ids);
    CHECK(session.prompt().id == PromptId::BasePoint);
    session.feedPoint(Vec3(0, 0, 0));
    CHECK(session.prompt().id == PromptId::SecondPoint);

    const auto preview = session.preview(Vec3(2, 0, 0));
    REQUIRE(preview.ghosts.size() == 2);
    CHECK(std::get<LineEntity>(preview.ghosts[0].shape).start.x() == 2.0);

    session.feedText("@2,0,1");
    CHECK_FALSE(session.isActive());
    CHECK(drawing.size() == 2);
    CHECK(lineOf(drawing, ids[0]).start == Vec3(2, 0, 1));
    CHECK(hasMessage(session.takeMessages(), MessageId::EntitiesMoved));

    // Undo restores the old shape under the same id (REQ-005-32).
    const auto edits = session.takeFinishedEdits();
    REQUIRE(edits.size() == 1);
    drawing.revert(edits[0]);
    CHECK(lineOf(drawing, ids[0]).start == Vec3(0, 0, 0));
}

// REQ-005-27, 29: COPY asking for the objects, several copies.
TEST_CASE("COPY asks for objects and makes several copies", "[geom][commands]")
{
    Drawing drawing;
    const auto ids = twoLines(drawing);
    CommandSession session(drawing);
    session.start(CommandName::Copy);
    CHECK(session.wantsSelection());
    CHECK(session.prompt().id == PromptId::SelectObjects);
    session.feedSelection(std::vector<EntityId>{ids[1]});
    session.enter();
    CHECK(session.prompt().id == PromptId::BasePoint);
    session.feedPoint(Vec3(0, 0, 0));
    CHECK(session.prompt().id == PromptId::SecondPointOrEnter);
    session.feedPoint(Vec3(0, 2, 0));
    session.feedPoint(Vec3(0, 4, 0));
    session.enter();
    CHECK(drawing.size() == 4);
    const auto edits = session.takeFinishedEdits();
    REQUIRE(edits.size() == 1);
    REQUIRE(edits[0].added.size() == 2);
    // New ids, the originals untouched.
    CHECK(edits[0].added[0].id != ids[1]);
    CHECK(edits[0].added[0].id != edits[0].added[1].id);
    CHECK(lineOf(drawing, ids[1]).start == Vec3(0, 1, 0));
    CHECK(lineOf(drawing, edits[0].added[1].id).start == Vec3(0, 5, 0));
}

// REQ-005-27, 30, 32
TEST_CASE("ERASE removes, and undo brings the same ids back", "[geom][commands]")
{
    Drawing drawing;
    const auto ids = twoLines(drawing);
    CommandSession session(drawing);

    SECTION("nothing selected")
    {
        session.start(CommandName::Erase);
        session.enter();
        CHECK(hasMessage(session.takeMessages(), MessageId::NothingSelected));
        CHECK_FALSE(session.isActive());
        CHECK(drawing.size() == 2);
    }
    SECTION("with a selection made before")
    {
        session.start(CommandName::Erase, ids);
        CHECK_FALSE(session.isActive());
        CHECK(drawing.empty());
        const auto edits = session.takeFinishedEdits();
        REQUIRE(edits.size() == 1);
        drawing.revert(edits[0]);
        CHECK(drawing.find(ids[0]) != nullptr);
        CHECK(drawing.find(ids[1]) != nullptr);
    }
}
