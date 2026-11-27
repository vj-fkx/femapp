#include "result_view.hpp"

#include "analysis_text.hpp"
#include "model_controller.hpp"
#include "symbol_builder.hpp"

#include <QThread>
#include <QUndoStack>

#include <algorithm>
#include <utility>
#include <variant>

namespace fem::app {

namespace {

// The fields of a result: its static result, or one of its buckling modes.
core::ResultFields fieldsFor(const setup::AnalysisResult& result, int mode)
{
    if (const auto* modes = std::get_if<core::BucklingResult>(&result.data)) {
        if (modes->modes.empty()) {
            return core::ResultFields{.mesh = result.mesh,
                                      .displacements = {},
                                      .rotations = {},
                                      .beams = {},
                                      .axialForce = {},
                                      .mode = true};
        }
        const auto index = static_cast<std::size_t>(
            std::clamp(mode, 0, static_cast<int>(modes->modes.size()) - 1));
        return core::fieldsOf(result.mesh, modes->modes[index]);
    }
    if (const core::StaticResult* solved = staticOf(result)) {
        return core::fieldsOf(*solved);
    }
    return {};
}

} // namespace

std::shared_ptr<const PreparedResult>
prepareResult(std::shared_ptr<const setup::AnalysisResult> result, int mode,
              core::ResultQuantity quantity)
{
    auto out = std::make_shared<PreparedResult>();
    out->result = std::move(result);
    out->mode = mode;
    out->fields = fieldsFor(*out->result, mode);
    auto shape = std::make_shared<core::ShapeSamples>(core::sampleShape(out->fields));
    out->field = core::sampleField(out->fields, *shape, quantity);
    out->autoScale = core::autoScale(*shape);
    out->scene = render::buildResultScene(*shape, out->result->mesh);
    out->values = render::segmentValues(*out->scene, out->field);
    out->shape = std::move(shape);
    return out;
}

std::shared_ptr<const PreparedResult> revalueResult(const PreparedResult& prepared,
                                                    core::ResultQuantity quantity)
{
    auto out = std::make_shared<PreparedResult>(prepared);
    out->field = core::sampleField(out->fields, *out->shape, quantity);
    out->values = render::segmentValues(*out->scene, out->field);
    return out;
}

ResultView::ResultView(ModelController* controller, ResultSink* sink, QObject* parent)
    : QObject(parent)
    , m_controller(controller)
    , m_sink(sink)
{
    connect(controller, &ModelController::resultsChanged, this, &ResultView::resultsChanged);
    // REQ-012-02: the warning follows every change of what was analysed.
    connect(controller, &ModelController::documentChanged, this, &ResultView::modelChanged);
    connect(controller, &ModelController::meshChanged, this, &ResultView::modelChanged);
    connect(controller->undoStack(), &QUndoStack::indexChanged, this, &ResultView::modelChanged);
}

const setup::AnalysisResult* ResultView::chosen() const
{
    return m_item ? m_controller->results().find(*m_item) : nullptr;
}

core::ResultQuantity ResultView::quantityFor(const setup::AnalysisResult& result) const
{
    return shownQuantity(result, m_settings.quantity);
}

void ResultView::updateAvailability()
{
    const bool available = chosen() != nullptr;
    if (available != m_available) {
        m_available = available;
        emit availabilityChanged(available);
    }
    if (!available && m_shown) {
        setShown(false);
    }
}

void ResultView::setShown(bool shown)
{
    if (shown && !m_available) {
        return;
    }
    if (shown == m_shown) {
        return;
    }
    m_shown = shown;
    m_sink->setResultsShown(shown);
    updateSymbols();
    if (shown) {
        const setup::AnalysisResult* result = chosen();
        const bool same = m_prepared && result != nullptr && m_prepared->mode == m_mode &&
                          m_prepared->result->item == result->item &&
                          m_prepared->result->state == result->state &&
                          m_prepared->result->settings == result->settings;
        if (same) {
            apply();
        } else {
            request(Job::Whole);
        }
    }
}

void ResultView::showResult(setup::ItemId item, int mode)
{
    if (m_item == item && m_mode == mode) {
        return;
    }
    m_item = item;
    m_mode = mode;
    m_query.reset();
    updateAvailability();
    if (m_shown) {
        request(Job::Whole);
    }
}

void ResultView::setSettings(const ResultDisplaySettings& settings)
{
    const bool reactions = settings.reactions != m_settings.reactions;
    m_settings = settings;
    if (!m_shown) {
        return;
    }
    if (reactions) {
        updateSymbols();
    }
    // REQ-012-13: another quantity: the values only; the rest: uniforms
    // and texts (REQ-012-08).
    if (m_prepared && quantityFor(*m_prepared->result) != m_prepared->field.quantity) {
        request(Job::Values);
    } else {
        apply();
    }
}

void ResultView::setQuery(std::optional<render::ResultPick> query)
{
    m_query = query;
    if (m_shown) {
        apply();
    }
}

void ResultView::resultsChanged()
{
    updateAvailability();
    if (!m_shown) {
        return;
    }
    const setup::AnalysisResult* result = chosen();
    // Another item analysed, or the same result: nothing to make again.
    const bool same = m_prepared && result != nullptr && m_prepared->result->item == result->item &&
                      m_prepared->result->state == result->state &&
                      m_prepared->result->settings == result->settings;
    if (!same) {
        m_query.reset();
        request(Job::Whole);
    }
}

void ResultView::modelChanged()
{
    if (m_shown) {
        apply();
    }
}

void ResultView::request(Job job)
{
    if (job == Job::Values && !m_prepared) {
        job = Job::Whole;
    }
    ++m_requestId;
    if (m_running) {
        m_pending = std::max(m_pending, job);
        return;
    }
    start(job);
}

void ResultView::start(Job job)
{
    const setup::AnalysisResult* result = chosen();
    if (result == nullptr) {
        return;
    }
    const core::ResultQuantity quantity = quantityFor(*result);
    const std::uint64_t id = m_requestId;
    auto out = std::make_shared<std::shared_ptr<const PreparedResult>>();
    QThread* thread = nullptr;
    if (job == Job::Whole || !m_prepared) {
        ++m_sceneBuilds;
        job = Job::Whole;
        // A copy: the analysis may replace the result while this runs.
        auto copy = std::make_shared<const setup::AnalysisResult>(*result);
        const int mode = m_mode;
        thread = QThread::create(
            [out, copy, mode, quantity] { *out = prepareResult(copy, mode, quantity); });
    } else {
        ++m_valueBuilds;
        const std::shared_ptr<const PreparedResult> prepared = m_prepared;
        thread = QThread::create(
            [out, prepared, quantity] { *out = revalueResult(*prepared, quantity); });
    }
    m_running = true;
    // Queued: QThread::finished is emitted on the worker thread.
    connect(
        thread, &QThread::finished, this, [this, id, job, out] { finished(id, job, *out); },
        Qt::QueuedConnection);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void ResultView::finished(std::uint64_t id, Job job, std::shared_ptr<const PreparedResult> prepared)
{
    m_running = false;
    const Job pending = std::exchange(m_pending, Job::None);
    // Older than the last request, or hidden meanwhile: dropped.
    if (id == m_requestId && prepared && m_shown) {
        m_prepared = std::move(prepared);
        if (job == Job::Whole) {
            m_sink->setResultScene(m_prepared->scene, m_prepared->values);
            updateSymbols();
        } else {
            m_sink->setResultValues(m_prepared->values);
        }
        apply();
        emit ready();
    }
    if (pending != Job::None) {
        start(pending);
    }
}

void ResultView::apply()
{
    if (!m_prepared) {
        return;
    }
    const setup::AnalysisResult& result = *m_prepared->result;
    const bool buckling = isBuckling(result);
    const render::ResultDisplay display =
        displayFor(m_settings, m_prepared->field, m_prepared->autoScale, buckling);
    m_sink->setResultDisplay(display);
    AnnotationInput in;
    in.result = &result;
    in.mode = m_prepared->mode;
    in.fields = &m_prepared->fields;
    in.shape = m_prepared->shape.get();
    in.field = &m_prepared->field;
    in.settings = m_settings;
    in.display = display;
    in.units = m_controller->document().units();
    in.caseName = tr("%1 (%2)").arg(caseName(m_controller->document(), result.item),
                                    analysisTypeName(result.settings.type));
    in.current = setup::ResultSet::isCurrent(result, m_controller->state());
    in.query = m_query;
    m_sink->setResultAnnotations(annotationsFor(in));
}

void ResultView::updateSymbols()
{
    SymbolOptions options = m_controller->symbolOptions();
    options.loadsShown = !m_shown;
    options.reactionForces.clear();
    options.reactionMoments.clear();
    if (m_shown && m_prepared && m_settings.reactions) {
        ReactionSymbols reactions = reactionSymbols(*m_prepared->result);
        options.reactionForces = std::move(reactions.forces);
        options.reactionMoments = std::move(reactions.moments);
    }
    m_controller->setSymbolOptions(std::move(options));
}

} // namespace fem::app
