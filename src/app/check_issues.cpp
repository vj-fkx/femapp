#include "check_issues.hpp"

#include <QCoreApplication>

#include <algorithm>
#include <cctype>
#include <string>

namespace fem::app {

namespace {

struct CheckText
{
    Q_DECLARE_TR_FUNCTIONS(CheckIssues)
};

// The words of a message, without the punctuation around them.
std::vector<std::string> wordsOf(std::string_view message)
{
    std::vector<std::string> words;
    std::string word;
    const auto flush = [&] {
        if (!word.empty()) {
            words.push_back(word);
            word.clear();
        }
    };
    for (const char c : message) {
        if (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-') {
            word += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else {
            flush();
        }
    }
    flush();
    return words;
}

bool isNumber(const std::string& word)
{
    const auto digits = word.front() == '-' ? word.substr(1) : word;
    return !digits.empty() && std::ranges::all_of(digits, [](char c) {
        return std::isdigit(static_cast<unsigned char>(c)) != 0;
    });
}

} // namespace

QString issueText(const setup::BuildIssue& issue)
{
    QString detail = QString::fromStdString(issue.detail);
    using K = setup::IssueKind;
    switch (issue.kind) {
    case K::NoMesh:
        return CheckText::tr("There is no mesh: run MESH or import one.");
    case K::MeshOutOfDate:
        return CheckText::tr("The drawing changed after MESH: mesh it again.");
    case K::MissingEntity:
        return CheckText::tr("The entity %1 is not in the drawing.").arg(detail);
    case K::EntityNotMeshed:
        return CheckText::tr("The entity %1 has nothing in the mesh.").arg(detail);
    case K::MissingGroup:
        return CheckText::tr("The group \"%1\" is not in the mesh.").arg(detail);
    case K::StaleSelection:
        return CheckText::tr("The elements or nodes were picked on another mesh: pick them again.");
    case K::MissingElement:
        return CheckText::tr("The element %1 is not in the mesh.").arg(detail);
    case K::MissingNode:
        return CheckText::tr("The node %1 is not in the mesh.").arg(detail);
    case K::UnknownMaterial:
        return CheckText::tr("The material \"%1\" is not in the model.").arg(detail);
    case K::UnknownSection:
        return CheckText::tr("The section \"%1\" is not in the model.").arg(detail);
    case K::UnknownLoad:
        return CheckText::tr("No load case or combination is active.");
    case K::SettlementNotHeld:
        return CheckText::tr("A settlement on a dof that no support holds (%1).").arg(detail);
    case K::TrussBuckling:
        return CheckText::tr("Truss bars do not buckle between their nodes in the second order "
                             "or the buckling analysis: model them as beams for that.");
    case K::Core:
        return detail;
    }
    return detail;
}

NamedIds idsInMessage(std::string_view message)
{
    NamedIds ids;
    const std::vector<std::string> words = wordsOf(message);
    // The ids follow "element(s)" or "node(s)", maybe after "without an
    // assignment", and run until another word; "and N more" is a count.
    std::vector<core::ExternalId>* into = nullptr;
    for (std::size_t i = 0; i < words.size(); ++i) {
        const std::string& w = words[i];
        if (w == "element" || w == "elements") {
            into = &ids.elements;
        } else if (w == "node" || w == "nodes") {
            into = &ids.nodes;
        } else if (isNumber(w)) {
            const bool count = i + 1 < words.size() && words[i + 1] == "more";
            if (into != nullptr && !count) {
                into->push_back(std::stoll(w));
            }
        } else if (w != "without" && w != "an" && w != "assignment" && w != "and") {
            into = nullptr;
        }
    }
    for (auto* list : {&ids.elements, &ids.nodes}) {
        std::ranges::sort(*list);
        const auto [first, last] = std::ranges::unique(*list);
        list->erase(first, last);
    }
    return ids;
}

} // namespace fem::app
