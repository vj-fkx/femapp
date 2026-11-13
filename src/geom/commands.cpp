#include <fem/geom/commands.hpp>

#include <algorithm>
#include <utility>
#include <variant>

namespace fem::geom {

CommandSession::CommandSession(Drawing& drawing)
    : m_drawing(drawing)
{}

bool CommandSession::isActive() const noexcept
{
    return m_command.has_value();
}

std::optional<CommandName> CommandSession::activeCommand() const noexcept
{
    return m_command;
}

Prompt CommandSession::prompt() const
{
    switch (m_step) {
    case Step::Idle:
        return {.id = PromptId::Command, .options = {}};
    case Step::Point:
        return {.id = PromptId::Point, .options = {}};
    case Step::First:
        return {.id = PromptId::FirstPoint, .options = {}};
    case Step::Next: {
        Prompt next{.id = PromptId::NextPoint, .options = {OptionId::Undo}};
        const bool canClose =
            m_command == CommandName::Line ? m_lines.size() >= 2 : m_points.size() >= 3;
        if (canClose) {
            next.options.push_back(OptionId::Close);
        }
        return next;
    }
    case Step::Select:
        return {.id = PromptId::SelectObjects, .options = {}};
    case Step::Base:
        return {.id = PromptId::BasePoint, .options = {}};
    case Step::Second:
        return {.id = m_command == CommandName::Copy ? PromptId::SecondPointOrEnter
                                                     : PromptId::SecondPoint,
                .options = {}};
    }
    return {};
}

bool CommandSession::wantsPoint() const noexcept
{
    return m_step == Step::Point || m_step == Step::First || m_step == Step::Next ||
           m_step == Step::Base || m_step == Step::Second;
}

bool CommandSession::wantsSelection() const noexcept
{
    return m_step == Step::Select;
}

std::optional<Vec3> CommandSession::referencePoint() const
{
    if (m_step == Step::Next && !m_points.empty()) {
        return m_points.back();
    }
    if (m_step == Step::Second) {
        return m_base;
    }
    return std::nullopt;
}

void CommandSession::start(CommandName name, std::span<const EntityId> preselected)
{
    if (isActive()) {
        cancel();
    }
    if (name == CommandName::Undo || name == CommandName::Redo) {
        m_undoRequest = name;
        return;
    }
    m_command = name;
    m_lastCommand = name;
    m_points.clear();
    m_lines.clear();
    m_selection.clear();
    m_base.reset();
    m_edit = {};
    m_count = 0;

    switch (name) {
    case CommandName::Point:
        m_step = Step::Point;
        break;
    case CommandName::Line:
    case CommandName::Pline:
        m_step = Step::First;
        break;
    case CommandName::Move:
    case CommandName::Copy:
    case CommandName::Erase:
        // REQ-005-27: a selection made before the command is used.
        for (const EntityId id : preselected) {
            if (m_drawing.find(id) != nullptr &&
                std::ranges::find(m_selection, id) == m_selection.end()) {
                m_selection.push_back(id);
            }
        }
        if (m_selection.empty()) {
            m_step = Step::Select;
        } else {
            confirmSelection();
        }
        break;
    case CommandName::Undo:
    case CommandName::Redo:
        break;
    }
}

void CommandSession::feedText(std::string_view text, const Vec3& cursorDirection)
{
    const auto input = parseInput(text);
    if (!input) {
        message(MessageId::InvalidInput);
        return;
    }
    if (std::holds_alternative<Empty>(*input)) {
        enter();
        return;
    }
    if (const auto* keyword = std::get_if<Keyword>(&*input)) {
        // Options of the prompt come first: "U" is Undo inside LINE.
        if (m_step == Step::Next && keyword->text == "U") {
            handleOption(OptionId::Undo);
            return;
        }
        if (m_step == Step::Next && keyword->text == "C") {
            handleOption(OptionId::Close);
            return;
        }
        if (const auto command = findCommand(keyword->text)) {
            start(*command);
            return;
        }
        message(isActive() ? MessageId::InvalidInput : MessageId::UnknownCommand);
        return;
    }
    if (!wantsPoint()) {
        message(MessageId::InvalidInput);
        return;
    }
    // @ refers to the previous point of the command, else to the last point
    // given, as in AutoCAD.
    const std::optional<Vec3> reference = referencePoint() ? referencePoint() : m_lastPoint;
    const std::optional<Vec3> from =
        std::holds_alternative<Distance>(*input) ? referencePoint() : reference;
    const auto point = resolvePoint(*input, from, cursorDirection);
    if (!point) {
        message(MessageId::NoPreviousPoint);
        return;
    }
    feedPoint(*point);
}

void CommandSession::feedPoint(const Vec3& point)
{
    if (!wantsPoint()) {
        return;
    }
    m_lastPoint = point;
    switch (m_step) {
    case Step::Point: {
        const Entity entity{.id = m_drawing.nextId(), .shape = PointEntity{.position = point}};
        apply({.removed = {}, .added = {entity}});
        ++m_count;
        break;
    }
    case Step::First:
        m_points = {point};
        m_step = Step::Next;
        break;
    case Step::Next:
        if (m_command == CommandName::Line) {
            const Entity line{.id = m_drawing.nextId(),
                              .shape = LineEntity{.start = m_points.back(), .end = point}};
            apply({.removed = {}, .added = {line}});
            m_lines.push_back(line);
        }
        m_points.push_back(point);
        break;
    case Step::Base:
        m_base = point;
        m_step = Step::Second;
        break;
    case Step::Second: {
        const Vec3 delta = point - m_base.value_or(point);
        const std::vector<Entity> entities = selectedEntities();
        DrawingEdit edit;
        for (const Entity& entity : entities) {
            if (m_command == CommandName::Move) {
                edit.removed.push_back(entity);
                edit.added.push_back(translated(entity, delta, entity.id));
            } else {
                edit.added.push_back(translated(entity, delta, m_drawing.nextId()));
            }
        }
        apply(edit);
        m_count += static_cast<int>(entities.size());
        if (m_command == CommandName::Move) {
            message(MessageId::EntitiesMoved, m_count);
            finish();
        }
        break;
    }
    case Step::Idle:
    case Step::Select:
        break;
    }
}

void CommandSession::feedSelection(std::span<const EntityId> ids)
{
    if (m_step != Step::Select) {
        return;
    }
    m_selection.clear();
    for (const EntityId id : ids) {
        if (m_drawing.find(id) != nullptr &&
            std::ranges::find(m_selection, id) == m_selection.end()) {
            m_selection.push_back(id);
        }
    }
}

void CommandSession::enter()
{
    if (!isActive()) {
        // REQ-005-06: Enter on an empty line repeats the last command.
        if (m_lastCommand) {
            start(*m_lastCommand);
        }
        return;
    }
    if (m_step == Step::Select) {
        confirmSelection();
        return;
    }
    finish();
}

void CommandSession::cancel()
{
    // REQ-005-07: what the command already did stays.
    if (isActive()) {
        finish();
    }
}

void CommandSession::handleOption(OptionId option)
{
    const bool line = m_command == CommandName::Line;
    if (option == OptionId::Undo) {
        if (line && !m_lines.empty()) {
            apply({.removed = {m_lines.back()}, .added = {}});
            m_lines.pop_back();
        }
        if (m_points.size() > 1) {
            m_points.pop_back();
        } else {
            m_points.clear();
            m_step = Step::First;
        }
        return;
    }
    // Close.
    if (line) {
        if (m_lines.size() < 2) {
            message(MessageId::NeedTwoSegments);
            return;
        }
        const Entity closing{.id = m_drawing.nextId(),
                             .shape =
                                 LineEntity{.start = m_points.back(), .end = m_points.front()}};
        apply({.removed = {}, .added = {closing}});
        m_lines.push_back(closing);
        finish();
        return;
    }
    if (m_points.size() < 3) {
        message(MessageId::NeedTwoSegments);
        return;
    }
    const Entity polyline{.id = m_drawing.nextId(),
                          .shape = PolylineEntity{.vertices = m_points, .closed = true}};
    apply({.removed = {}, .added = {polyline}});
    m_points.clear();
    message(MessageId::PolylineCreated, 1);
    finish();
}

void CommandSession::confirmSelection()
{
    if (m_selection.empty()) {
        message(MessageId::NothingSelected);
        finish();
        return;
    }
    if (m_command == CommandName::Erase) {
        const std::vector<Entity> entities = selectedEntities();
        apply({.removed = entities, .added = {}});
        message(MessageId::EntitiesErased, static_cast<int>(entities.size()));
        finish();
        return;
    }
    m_step = Step::Base;
}

std::vector<Entity> CommandSession::selectedEntities() const
{
    std::vector<Entity> entities;
    entities.reserve(m_selection.size());
    for (const EntityId id : m_selection) {
        if (const Entity* entity = m_drawing.find(id)) {
            entities.push_back(*entity);
        }
    }
    return entities;
}

void CommandSession::finish()
{
    if (m_command == CommandName::Pline && m_points.size() >= 2) {
        const Entity polyline{.id = m_drawing.nextId(),
                              .shape = PolylineEntity{.vertices = m_points, .closed = false}};
        apply({.removed = {}, .added = {polyline}});
        message(MessageId::PolylineCreated, 1);
    }
    if (m_command == CommandName::Point && m_count > 0) {
        message(MessageId::PointsCreated, m_count);
    }
    if (m_command == CommandName::Line && !m_lines.empty()) {
        message(MessageId::LinesCreated, static_cast<int>(m_lines.size()));
    }
    if (m_command == CommandName::Copy && m_count > 0) {
        message(MessageId::EntitiesCopied, m_count);
    }
    if (!m_edit.empty()) {
        m_finished.push_back(std::move(m_edit));
    }
    m_edit = {};
    m_command.reset();
    m_step = Step::Idle;
    m_points.clear();
    m_lines.clear();
    m_selection.clear();
    m_base.reset();
    m_count = 0;
}

void CommandSession::apply(const DrawingEdit& edit)
{
    m_drawing.apply(edit);
    m_edit.append(edit);
}

void CommandSession::message(MessageId id, int count)
{
    m_messages.push_back({.id = id, .count = count});
}

Preview CommandSession::preview(const std::optional<Vec3>& cursor) const
{
    Preview result;
    const auto reference = referencePoint();
    if (cursor && reference) {
        result.rubberBand = std::make_pair(*reference, *cursor);
    }
    if (m_command == CommandName::Pline && m_points.size() >= 2) {
        result.ghosts.push_back(
            {.id = EntityId{}, .shape = PolylineEntity{.vertices = m_points, .closed = false}});
    }
    if (m_step == Step::Second && cursor && m_base) {
        const Vec3 delta = *cursor - *m_base;
        for (const Entity& entity : selectedEntities()) {
            result.ghosts.push_back(translated(entity, delta, entity.id));
        }
    }
    return result;
}

std::vector<Message> CommandSession::takeMessages()
{
    return std::exchange(m_messages, {});
}

std::vector<DrawingEdit> CommandSession::takeFinishedEdits()
{
    return std::exchange(m_finished, {});
}

std::optional<CommandName> CommandSession::takeUndoRequest()
{
    return std::exchange(m_undoRequest, std::nullopt);
}

} // namespace fem::geom
