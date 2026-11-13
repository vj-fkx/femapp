#pragma once

#include <fem/geom/command_input.hpp>
#include <fem/geom/drawing.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace fem::geom {

// What the command line asks for; translated by the application (REQ-005-04).
enum class PromptId : std::uint8_t {
    Command,            // no command running
    Point,              // POINT
    FirstPoint,         // LINE, PLINE
    NextPoint,          // LINE, PLINE
    SelectObjects,      // MOVE, COPY, ERASE
    BasePoint,          // MOVE, COPY
    SecondPoint,        // MOVE
    SecondPointOrEnter, // COPY
};

// Options of a prompt, typed by their letter (REQ-005-16, 17).
enum class OptionId : std::uint8_t { Undo, Close }; // "U", "C"

// Messages for the history (REQ-005-08 and the results of the commands).
enum class MessageId : std::uint8_t {
    InvalidInput,
    UnknownCommand,
    NoPreviousPoint,
    NeedTwoSegments,
    NothingSelected,
    PointsCreated,
    LinesCreated,
    PolylineCreated,
    EntitiesMoved,
    EntitiesCopied,
    EntitiesErased,
};

struct Prompt
{
    PromptId id = PromptId::Command;
    std::vector<OptionId> options;
};

struct Message
{
    MessageId id = MessageId::InvalidInput;
    int count = 0;
};

// What to draw while a command runs (REQ-005-18, 28, 29).
struct Preview
{
    std::optional<std::pair<Vec3, Vec3>> rubberBand; // from the last point to the cursor
    std::vector<Entity> ghosts;                      // entities being built or moved
};

// Runs one drafting command at a time on a drawing (REQ-005-06, 07, 15 to
// 18, 27 to 31). Entities appear in the drawing as soon as they are made;
// each finished command is reported as one DrawingEdit, a single undo step.
class CommandSession
{
public:
    explicit CommandSession(Drawing& drawing);

    [[nodiscard]] bool isActive() const noexcept;
    [[nodiscard]] std::optional<CommandName> activeCommand() const noexcept;
    [[nodiscard]] Prompt prompt() const;
    [[nodiscard]] bool wantsPoint() const noexcept;
    [[nodiscard]] bool wantsSelection() const noexcept;
    // The previous point of the running command: rubber band, ortho and
    // direct distance start from it.
    [[nodiscard]] std::optional<Vec3> referencePoint() const;

    // A running command is finished first (as with Esc). With a selection
    // given, MOVE, COPY and ERASE skip "Select objects".
    void start(CommandName name, std::span<const EntityId> preselected = {});
    // A line typed on the command line. `cursorDirection` is used by a
    // single number (direct distance).
    void feedText(std::string_view text, const Vec3& cursorDirection = Vec3::UnitX());
    void feedPoint(const Vec3& point);
    // The objects selected so far, while "Select objects" is asked.
    void feedSelection(std::span<const EntityId> ids);
    void enter();
    void cancel();

    [[nodiscard]] Preview preview(const std::optional<Vec3>& cursor) const;

    // Drained by the caller after every call.
    [[nodiscard]] std::vector<Message> takeMessages();
    [[nodiscard]] std::vector<DrawingEdit> takeFinishedEdits();
    // UNDO or REDO typed; the caller owns the undo history.
    [[nodiscard]] std::optional<CommandName> takeUndoRequest();

private:
    enum class Step : std::uint8_t { Idle, Point, First, Next, Select, Base, Second };

    void finish();
    void apply(const DrawingEdit& edit);
    void message(MessageId id, int count = 0);
    void handleOption(OptionId option);
    void confirmSelection();
    [[nodiscard]] std::vector<Entity> selectedEntities() const;

    Drawing& m_drawing;
    std::optional<CommandName> m_command;
    std::optional<CommandName> m_lastCommand; // repeated by Enter (REQ-005-06)
    Step m_step = Step::Idle;
    std::vector<Vec3> m_points;  // LINE and PLINE vertices so far
    std::vector<Entity> m_lines; // lines made by the running LINE, for U
    std::vector<EntityId> m_selection;
    std::optional<Vec3> m_base;      // MOVE and COPY base point
    std::optional<Vec3> m_lastPoint; // last point given, for @ between commands
    DrawingEdit m_edit;              // everything the running command changed
    int m_count = 0;                 // entities made, moved or erased

    std::vector<Message> m_messages;
    std::vector<DrawingEdit> m_finished;
    std::optional<CommandName> m_undoRequest;
};

} // namespace fem::geom
