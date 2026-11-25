#include <fem/core/assembler.hpp>

#include <tbb/enumerable_thread_specific.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_sort.h>
#include <tbb/task_arena.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace fem::core {

namespace {

// One element to assemble: its set and its index.
struct Item
{
    std::int32_t set = 0;
    ElementIndex element = 0;
};

// Per-thread work space, created once per thread (no allocation per element).
struct Workspace
{
    Eigen::MatrixXd matrix = Eigen::MatrixXd::Zero(kMaxElementDofs, kMaxElementDofs);
    Eigen::VectorXd vector = Eigen::VectorXd::Zero(kMaxElementDofs);
    std::array<DofIndex, kMaxElementDofs> dofs{};
    ElementNodes nodes;
};

// Global dofs of an element, in local order; returns the count.
int elementDofs(const Mesh& mesh, const DofMap& dofs, const ElementFormulation& formulation,
                ElementIndex element, std::array<DofIndex, kMaxElementDofs>& out)
{
    const ElementType type = mesh.elementType(element);
    const auto nodes = mesh.elementNodes(element);
    int count = 0;
    for (std::size_t k = 0; k < nodes.size(); ++k) {
        const DofMask used = formulation.dofsAt(type, static_cast<int>(k));
        for (int t = 0; t < kDofTypeCount; ++t) {
            const auto dofType = static_cast<DofType>(t);
            if ((used & mask(dofType)) != 0) {
                out.at(static_cast<std::size_t>(count++)) =
                    dofs.find(nodes[k], dofType).value_or(-1);
            }
        }
    }
    return count;
}

void fillNodes(const Mesh& mesh, ElementIndex element, ElementNodes& out)
{
    out.type = mesh.elementType(element);
    const auto nodes = mesh.elementNodes(element);
    out.count = static_cast<int>(nodes.size());
    for (std::size_t k = 0; k < nodes.size() && k < out.x.size(); ++k) {
        out.x.at(k) = mesh.nodeCoordinates(nodes[k]);
    }
}

std::vector<Item> itemsOf(std::span<const ElementSet> sets)
{
    std::vector<Item> items;
    for (std::size_t s = 0; s < sets.size(); ++s) {
        for (const ElementIndex e : sets[s].elements) {
            items.push_back({.set = static_cast<std::int32_t>(s), .element = e});
        }
    }
    return items;
}

// Greedy colouring: elements of one colour share no node (design 006 §5.5).
// Returns the items grouped by colour and the start of each colour.
struct Colouring
{
    std::vector<Item> items;
    std::vector<std::size_t> start; // size colours + 1
};

constexpr int kColourWords = 4; // up to 256 colours

Expected<Colouring> colour(const Mesh& mesh, const std::vector<Item>& items)
{
    std::vector<std::array<std::uint64_t, kColourWords>> used(
        static_cast<std::size_t>(mesh.nodeCount()), std::array<std::uint64_t, kColourWords>{});
    std::vector<int> colourOf(items.size(), 0);
    int colours = 0;
    for (std::size_t i = 0; i < items.size(); ++i) {
        std::array<std::uint64_t, kColourWords> taken{};
        const auto nodes = mesh.elementNodes(items[i].element);
        for (const NodeIndex n : nodes) {
            const auto& u = used[static_cast<std::size_t>(n)];
            for (std::size_t w = 0; w < kColourWords; ++w) {
                taken.at(w) |= u.at(w);
            }
        }
        int c = -1;
        for (std::size_t w = 0; w < kColourWords && c < 0; ++w) {
            if (taken.at(w) != ~std::uint64_t{0}) {
                const std::uint64_t free = ~taken.at(w);
                c = static_cast<int>(w * 64) + std::countr_zero(free);
            }
        }
        if (c < 0) {
            return makeError("The mesh needs too many element colours for parallel assembly.");
        }
        for (const NodeIndex n : nodes) {
            used[static_cast<std::size_t>(n)].at(static_cast<std::size_t>(c / 64)) |=
                std::uint64_t{1} << static_cast<unsigned>(c % 64);
        }
        colourOf[i] = c;
        colours = std::max(colours, c + 1);
    }
    Colouring result;
    result.start.assign(static_cast<std::size_t>(colours) + 1, 0);
    for (const int c : colourOf) {
        ++result.start[static_cast<std::size_t>(c) + 1];
    }
    for (std::size_t c = 1; c < result.start.size(); ++c) {
        result.start[c] += result.start[c - 1];
    }
    result.items.resize(items.size());
    std::vector<std::size_t> next(result.start.begin(), result.start.end() - 1);
    for (std::size_t i = 0; i < items.size(); ++i) {
        result.items[next[static_cast<std::size_t>(colourOf[i])]++] = items[i];
    }
    return result;
}

// Runs `work` over every element, colour by colour, in parallel within a
// colour. Stops at the first element error. With a control (REQ-011-09),
// each block checks the cancel flag first and reports the fraction of
// elements done in `stage` after its work.
template <typename Work>
Expected<void> forEachElement(const Mesh& mesh, std::span<const ElementSet> sets, int threads,
                              const RunControl* control, RunStage stage, Work&& work)
{
    const auto coloured = colour(mesh, itemsOf(sets));
    if (!coloured) {
        return tl::unexpected(coloured.error());
    }
    tbb::enumerable_thread_specific<Workspace> workspaces;
    std::atomic<bool> failed{false};
    std::mutex errorMutex;
    std::optional<Error> firstError;
    const bool watched =
        control != nullptr && (control->cancel != nullptr || control->progress != nullptr);
    const std::size_t total = coloured->items.size();
    std::atomic<std::size_t> finished{0};

    tbb::task_arena arena(threads > 0 ? threads : tbb::task_arena::automatic);
    arena.execute([&] {
        for (std::size_t c = 0; c + 1 < coloured->start.size() && !failed; ++c) {
            const std::size_t begin = coloured->start[c];
            const std::size_t end = coloured->start[c + 1];
            tbb::parallel_for(
                tbb::blocked_range<std::size_t>(begin, end),
                [&](const tbb::blocked_range<std::size_t>& range) {
                    if (watched && control->cancelled()) {
                        failed = true;
                        return;
                    }
                    Workspace& ws = workspaces.local();
                    for (std::size_t i = range.begin(); i != range.end(); ++i) {
                        if (failed) {
                            return;
                        }
                        const Item& item = coloured->items[i];
                        const ElementSet& set = sets[static_cast<std::size_t>(item.set)];
                        auto result = work(set, item.element, ws);
                        if (!result) {
                            const std::scoped_lock lock(errorMutex);
                            if (!firstError) {
                                firstError =
                                    Error{.message = "Element " +
                                                     std::to_string(mesh.elementId(item.element)) +
                                                     ": " + result.error().message,
                                          .cancelled = false};
                            }
                            failed = true;
                            return;
                        }
                    }
                    if (watched) {
                        const std::size_t done = finished.fetch_add(range.size()) + range.size();
                        control->report(
                            {.stage = stage,
                             .fraction = static_cast<double>(done) / static_cast<double>(total),
                             .iteration = 0});
                    }
                });
        }
    });
    if (firstError) {
        return tl::unexpected(*firstError);
    }
    if (watched && control->cancelled()) {
        return makeCancelled();
    }
    return {};
}

// Computes the local matrix and vector of an element into the workspace;
// returns the number of local dofs.
Expected<int> computeLocal(const Mesh& mesh, const DofMap& dofs, const ElementSet& set,
                           ElementIndex element, Workspace& ws)
{
    const int size = elementDofs(mesh, dofs, *set.formulation, element, ws.dofs);
    fillNodes(mesh, element, ws.nodes);
    auto matrix = ws.matrix.topLeftCorner(size, size);
    auto vector = ws.vector.head(size);
    matrix.setZero();
    vector.setZero();
    auto computed = set.formulation->compute(ws.nodes, element, matrix, vector);
    if (!computed) {
        return tl::unexpected(computed.error());
    }
    return size;
}

// Lower triangle pattern: (column, row) pairs of every element, sorted and
// unique.
SparseMatrix pattern(const Mesh& mesh, const DofMap& dofs, std::span<const ElementSet> sets)
{
    std::vector<std::uint64_t> pairs;
    std::array<DofIndex, kMaxElementDofs> local{};
    for (const ElementSet& set : sets) {
        for (const ElementIndex e : set.elements) {
            const int size = elementDofs(mesh, dofs, *set.formulation, e, local);
            for (int a = 0; a < size; ++a) {
                const DofIndex da = local.at(static_cast<std::size_t>(a));
                const std::int32_t row = da < 0 ? -1 : dofs.equation(da);
                if (row < 0) {
                    continue;
                }
                for (int b = 0; b < size; ++b) {
                    const DofIndex db = local.at(static_cast<std::size_t>(b));
                    const std::int32_t column = db < 0 ? -1 : dofs.equation(db);
                    if (column >= 0 && row >= column) {
                        pairs.push_back((static_cast<std::uint64_t>(column) << 32U) |
                                        static_cast<std::uint32_t>(row));
                    }
                }
            }
        }
    }
    tbb::parallel_sort(pairs.begin(), pairs.end());
    const auto duplicates = std::ranges::unique(pairs);
    pairs.erase(duplicates.begin(), duplicates.end());

    const std::int32_t n = dofs.freeCount();
    SparseMatrix matrix(n, n);
    matrix.resizeNonZeros(static_cast<Eigen::Index>(pairs.size()));
    std::int32_t* outer = matrix.outerIndexPtr();
    std::int32_t* inner = matrix.innerIndexPtr();
    double* values = matrix.valuePtr();
    std::fill(outer, outer + n + 1, 0);
    for (std::size_t k = 0; k < pairs.size(); ++k) {
        const auto column = static_cast<std::int32_t>(pairs[k] >> 32U);
        ++outer[column + 1];
        inner[k] = static_cast<std::int32_t>(pairs[k] & 0xFFFFFFFFU);
        values[k] = 0.0;
    }
    for (std::int32_t c = 0; c < n; ++c) {
        outer[c + 1] += outer[c];
    }
    return matrix;
}

// Position of (row, column) in the compressed lower triangle.
double& entry(SparseMatrix& matrix, std::int32_t row, std::int32_t column)
{
    const std::int32_t* inner = matrix.innerIndexPtr();
    const std::int32_t begin = matrix.outerIndexPtr()[column];
    const std::int32_t end = matrix.outerIndexPtr()[column + 1];
    const std::int32_t* at = std::lower_bound(inner + begin, inner + end, row);
    return matrix.valuePtr()[at - inner];
}

} // namespace

Expected<AssembledSystem> assemble(const Mesh& mesh, const DofMap& dofs,
                                   std::span<const ElementSet> sets,
                                   std::span<const NodalLoad> loads, int threads,
                                   const RunControl* control)
{
    AssembledSystem system;
    const std::int32_t n = dofs.freeCount(); // numbers the equations before going parallel
    system.matrix = pattern(mesh, dofs, sets);
    system.rhs = Eigen::VectorXd::Zero(n);

    auto done = forEachElement(
        mesh, sets, threads, control, RunStage::Assembly,
        [&](const ElementSet& set, ElementIndex element, Workspace& ws) -> Expected<void> {
            const auto size = computeLocal(mesh, dofs, set, element, ws);
            if (!size) {
                return tl::unexpected(size.error());
            }
            for (int a = 0; a < *size; ++a) {
                const DofIndex da = ws.dofs.at(static_cast<std::size_t>(a));
                const std::int32_t row = dofs.equation(da);
                if (row < 0) {
                    continue;
                }
                system.rhs[row] += ws.vector[a];
                for (int b = 0; b < *size; ++b) {
                    const DofIndex db = ws.dofs.at(static_cast<std::size_t>(b));
                    const std::int32_t column = dofs.equation(db);
                    if (column < 0) {
                        // REQ-006-10: settlements on the right side.
                        system.rhs[row] -= ws.matrix(a, b) * dofs.prescribedValue(db);
                    } else if (row >= column) {
                        entry(system.matrix, row, column) += ws.matrix(a, b);
                    }
                }
            }
            return {};
        });
    if (!done) {
        return tl::unexpected(done.error());
    }

    // REQ-006-14
    for (const NodalLoad& load : loads) {
        const auto dof = dofs.find(load.node, load.type);
        if (!dof) {
            return makeError("Node " + std::to_string(mesh.nodeId(load.node)) + " has no dof " +
                             std::string(dofName(load.type)) + " for a load.");
        }
        const std::int32_t row = dofs.equation(*dof);
        if (row >= 0) {
            system.rhs[row] += load.value;
        }
    }
    return system;
}

Expected<Eigen::VectorXd> reactions(const Mesh& mesh, const DofMap& dofs,
                                    std::span<const ElementSet> sets,
                                    std::span<const NodalLoad> loads, const Eigen::VectorXd& values,
                                    int threads, const RunControl* control,
                                    Eigen::VectorXd* applied)
{
    Eigen::VectorXd result = Eigen::VectorXd::Zero(dofs.size());
    if (applied != nullptr) {
        *applied = Eigen::VectorXd::Zero(dofs.size());
    }
    auto done = forEachElement(
        mesh, sets, threads, control, RunStage::Recovery,
        [&](const ElementSet& set, ElementIndex element, Workspace& ws) -> Expected<void> {
            const auto size = computeLocal(mesh, dofs, set, element, ws);
            if (!size) {
                return tl::unexpected(size.error());
            }
            for (int a = 0; a < *size; ++a) {
                const DofIndex da = ws.dofs.at(static_cast<std::size_t>(a));
                if (applied != nullptr) {
                    (*applied)[da] += ws.vector[a]; // colours: no other element writes here
                }
                if (!dofs.isPrescribed(da)) {
                    continue;
                }
                double force = -ws.vector[a];
                for (int b = 0; b < *size; ++b) {
                    force += ws.matrix(a, b) * values[ws.dofs.at(static_cast<std::size_t>(b))];
                }
                result[da] += force;
            }
            return {};
        });
    if (!done) {
        return tl::unexpected(done.error());
    }
    for (const NodalLoad& load : loads) {
        const auto dof = dofs.find(load.node, load.type);
        if (dof && applied != nullptr) {
            (*applied)[*dof] += load.value;
        }
        if (dof && dofs.isPrescribed(*dof)) {
            result[*dof] -= load.value;
        }
    }
    return result;
}

} // namespace fem::core
