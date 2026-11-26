#include "analysis_job.hpp"

#include <QThread>

#include <atomic>
#include <cstdint>
#include <memory>
#include <utility>

namespace fem::app {

// What the worker and the interface share: the flag and the progress.
struct AnalysisJob::Shared
{
    std::atomic<bool> cancel{false};
    std::atomic<int> index{0};
    std::atomic<int> count{0};
    std::atomic<std::uint64_t> item{0};
    std::atomic<bool> building{false};
    std::atomic<int> stage{0};
    std::atomic<double> fraction{-1.0};
    std::atomic<int> iteration{0};
};

AnalysisJob::AnalysisJob(QObject* parent)
    : QObject(parent)
    , m_shared(std::make_shared<Shared>())
{
    connect(
        this, &AnalysisJob::outcomeReady, this,
        [this](const std::shared_ptr<const setup::CaseOutcome>& outcome) {
            emit caseFinished(*outcome);
        },
        Qt::QueuedConnection);
}

AnalysisJob::~AnalysisJob()
{
    if (m_thread != nullptr) {
        m_shared->cancel.store(true);
        m_thread->wait();
    }
}

bool AnalysisJob::start(const setup::Document& document, const geom::Drawing& drawing,
                        const setup::MeshContext& mesh, std::vector<setup::ItemId> items)
{
    if (m_running) {
        return false; // REQ-011-07
    }
    m_running = true;
    m_shared = std::make_shared<Shared>(); // a fresh flag and progress
    m_shared->count.store(static_cast<int>(items.size()));

    // The snapshot the worker analyses (REQ-011-06).
    struct Snapshot
    {
        setup::Document document;
        geom::Drawing drawing;
        setup::MeshContext mesh;
        std::vector<setup::ItemId> items;
        bool cancelled = false;
    };
    auto snapshot = std::make_shared<Snapshot>();
    snapshot->document = document;
    snapshot->drawing = drawing;
    snapshot->mesh = mesh;
    snapshot->items = std::move(items);
    auto shared = m_shared;

    // NOLINTNEXTLINE(bugprone-exception-escape): only allocation failures can escape.
    m_thread = QThread::create([this, snapshot, shared] {
        const core::RunControl control{
            .cancel = &shared->cancel, .progress = [shared](const core::RunProgress& p) {
                shared->building.store(false, std::memory_order_relaxed);
                shared->stage.store(static_cast<int>(p.stage), std::memory_order_relaxed);
                shared->fraction.store(p.fraction, std::memory_order_relaxed);
                shared->iteration.store(p.iteration, std::memory_order_relaxed);
            }};
        for (std::size_t i = 0; i < snapshot->items.size(); ++i) {
            if (shared->cancel.load()) {
                snapshot->cancelled = true;
                break;
            }
            const setup::ItemId item = snapshot->items[i];
            shared->index.store(static_cast<int>(i));
            shared->item.store(static_cast<std::uint64_t>(item));
            shared->stage.store(0);
            shared->fraction.store(-1.0);
            shared->iteration.store(0);
            shared->building.store(true);
            auto outcome = std::make_shared<const setup::CaseOutcome>(setup::runCase(
                snapshot->document, snapshot->drawing, snapshot->mesh, item, control));
            const bool cancelled = outcome->error && outcome->error->cancelled;
            // Emitted here, received on the thread of the job (queued),
            // before the end of the thread.
            emit outcomeReady(outcome, QPrivateSignal{});
            if (cancelled) {
                snapshot->cancelled = true;
                break;
            }
        }
    });
    // Queued: QThread::finished is emitted on the worker thread, after the
    // outcomes were posted.
    connect(m_thread, &QThread::finished, this, [this, snapshot] {
        m_running = false;
        m_thread = nullptr;
        emit finished(snapshot->cancelled);
    });
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
    m_thread->start();
    return true;
}

void AnalysisJob::cancel()
{
    m_shared->cancel.store(true);
}

AnalysisJob::Progress AnalysisJob::progress() const
{
    const Shared& s = *m_shared;
    return {.index = s.index.load(std::memory_order_relaxed),
            .count = s.count.load(std::memory_order_relaxed),
            .item = setup::ItemId{s.item.load(std::memory_order_relaxed)},
            .core = {.stage = static_cast<core::RunStage>(s.stage.load(std::memory_order_relaxed)),
                     .fraction = s.fraction.load(std::memory_order_relaxed),
                     .iteration = s.iteration.load(std::memory_order_relaxed)},
            .building = s.building.load(std::memory_order_relaxed)};
}

} // namespace fem::app
