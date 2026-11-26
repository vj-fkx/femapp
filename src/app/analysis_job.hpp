#pragma once

#include <fem/core/run_control.hpp>
#include <fem/geom/drawing.hpp>
#include <fem/setup/analysis.hpp>
#include <fem/setup/build.hpp>
#include <fem/setup/document.hpp>

#include <QObject>

#include <memory>
#include <vector>

class QThread;

namespace fem::app {

// REQ-011-03, 04, 06, 07, 08 (design 011 §5.4): analyses load cases and
// combinations one after the other on a worker thread, over copies of the
// document and the drawing, so that the interface stays responsive and its
// edits do not reach the run. Outcomes come back on the thread of this
// object, in order.
class AnalysisJob : public QObject
{
    Q_OBJECT

public:
    explicit AnalysisJob(QObject* parent = nullptr);
    // Cancels a run still going and waits for its thread.
    ~AnalysisJob() override;
    AnalysisJob(const AnalysisJob&) = delete;
    AnalysisJob& operator=(const AnalysisJob&) = delete;
    AnalysisJob(AnalysisJob&&) = delete;
    AnalysisJob& operator=(AnalysisJob&&) = delete;

    // Copies the document and the drawing; refused (false) while running.
    bool start(const setup::Document& document, const geom::Drawing& drawing,
               const setup::MeshContext& mesh, std::vector<setup::ItemId> items);
    // The core stops at its next check; the cases after it do not run.
    void cancel();
    [[nodiscard]] bool isRunning() const noexcept { return m_running; }

    // Read by a timer of the interface (atomics written by the worker).
    struct Progress
    {
        int index = 0; // of the item running, from 0
        int count = 0;
        setup::ItemId item{};
        core::RunProgress core;
        bool building = false; // the model is being built and checked
    };
    [[nodiscard]] Progress progress() const;

signals:
    void caseFinished(const fem::setup::CaseOutcome& outcome); // one per item run, in order
    void finished(bool cancelled);
    // Internal: from the worker thread to the thread of the job.
    void outcomeReady(std::shared_ptr<const fem::setup::CaseOutcome> outcome,
                      QPrivateSignal /*unused*/);

private:
    struct Shared;
    std::shared_ptr<Shared> m_shared;
    QThread* m_thread = nullptr;
    bool m_running = false;
};

} // namespace fem::app
