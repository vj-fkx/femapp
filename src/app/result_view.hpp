#pragma once

// Feature 012 (design §6; REQ-012-01, 02, 04, 08, 13): the display of a
// result: which result is shown (the one chosen in the Results dock), its
// samples prepared on a worker thread, and what the viewport gets: the
// scene, the values, the frame options, the texts and the symbols. It talks
// to the viewport through ResultSink, so that it is tested without a
// window.

#include "result_display_settings.hpp"

#include <fem/core/result_sampling.hpp>
#include <fem/render/result_display.hpp>
#include <fem/render/result_picker.hpp>
#include <fem/render/result_scene.hpp>
#include <fem/setup/analysis.hpp>
#include <fem/setup/document.hpp>

#include <QObject>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace fem::app {

class ModelController;

// What the viewport offers to the result display (render::Viewport, through
// an adapter of the main window).
class ResultSink
{
public:
    ResultSink() = default;
    virtual ~ResultSink() = default;
    ResultSink(const ResultSink&) = delete;
    ResultSink& operator=(const ResultSink&) = delete;
    ResultSink(ResultSink&&) = delete;
    ResultSink& operator=(ResultSink&&) = delete;

    virtual void setResultScene(std::shared_ptr<const render::ResultScene> scene,
                                std::shared_ptr<const std::vector<float>> values) = 0;
    virtual void setResultValues(std::shared_ptr<const std::vector<float>> values) = 0;
    virtual void setResultDisplay(const render::ResultDisplay& display) = 0;
    virtual void setResultAnnotations(render::ResultAnnotations annotations) = 0;
    virtual void setResultsShown(bool shown) = 0;
};

// A result made ready to draw: a copy of the result (the analysis may
// replace it meanwhile), its fields, samples, scene and values.
struct PreparedResult
{
    std::shared_ptr<const setup::AnalysisResult> result;
    int mode = 0;
    core::ResultFields fields; // views into *result
    std::shared_ptr<const core::ShapeSamples> shape;
    core::FieldSamples field;
    std::shared_ptr<const render::ResultScene> scene;
    std::shared_ptr<const std::vector<float>> values;
    double autoScale = 1.0;
};

// The work of the worker thread, without Qt: the whole result, or the
// values of another quantity on the samples already made.
[[nodiscard]] std::shared_ptr<const PreparedResult>
prepareResult(std::shared_ptr<const setup::AnalysisResult> result, int mode,
              core::ResultQuantity quantity);
[[nodiscard]] std::shared_ptr<const PreparedResult> revalueResult(const PreparedResult& prepared,
                                                                  core::ResultQuantity quantity);

class ResultView : public QObject
{
    Q_OBJECT

public:
    ResultView(ModelController* controller, ResultSink* sink, QObject* parent = nullptr);

    // REQ-012-01: the result drawn in place of the mesh, or not; refused
    // while no result is chosen.
    void setShown(bool shown);
    [[nodiscard]] bool isShown() const noexcept { return m_shown; }
    [[nodiscard]] bool isAvailable() const noexcept { return m_available; }

    // REQ-012-04: the result chosen in the Results dock (and its mode).
    void showResult(setup::ItemId item, int mode);

    void setSettings(const ResultDisplaySettings& settings);
    [[nodiscard]] const ResultDisplaySettings& settings() const noexcept { return m_settings; }

    // REQ-012-18: the point clicked, or none.
    void setQuery(std::optional<render::ResultPick> query);

    // For the tests (REQ-012-08, 13): the jobs started so far.
    [[nodiscard]] int sceneBuilds() const noexcept { return m_sceneBuilds; }
    [[nodiscard]] int valueBuilds() const noexcept { return m_valueBuilds; }
    [[nodiscard]] const std::shared_ptr<const PreparedResult>& prepared() const noexcept
    {
        return m_prepared;
    }

signals:
    void availabilityChanged(bool available);
    // The scene or the values of the last request are with the viewport.
    void ready();

private:
    enum class Job : std::uint8_t { None, Values, Whole };

    void resultsChanged();
    void modelChanged();
    void updateAvailability();
    void request(Job job);
    void start(Job job);
    void finished(std::uint64_t id, Job job, std::shared_ptr<const PreparedResult> prepared);
    void apply();
    void updateSymbols();
    [[nodiscard]] const setup::AnalysisResult* chosen() const;
    [[nodiscard]] core::ResultQuantity quantityFor(const setup::AnalysisResult& result) const;

    ModelController* m_controller = nullptr;
    ResultSink* m_sink = nullptr;
    ResultDisplaySettings m_settings;
    std::optional<setup::ItemId> m_item;
    int m_mode = 0;
    std::shared_ptr<const PreparedResult> m_prepared;
    std::optional<render::ResultPick> m_query;
    std::uint64_t m_requestId = 0; // the last request: older jobs are dropped
    Job m_pending = Job::None;
    int m_sceneBuilds = 0;
    int m_valueBuilds = 0;
    bool m_shown = false;
    bool m_available = false;
    bool m_running = false;
};

} // namespace fem::app
