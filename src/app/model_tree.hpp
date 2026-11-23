#pragma once

// Feature 010 (design §5.5, REQ-010-19): the model tree, a Qt item model
// over the document of a ModelController.

#include <fem/setup/document.hpp>

#include <QAbstractItemModel>
#include <QIcon>
#include <QString>

#include <cstdint>
#include <optional>
#include <vector>

namespace fem::app {

class ModelController;

class ModelTree : public QAbstractItemModel
{
    Q_OBJECT

public:
    // The groups at the top of the tree, in this order.
    enum class Group : std::uint8_t {
        Units,
        Materials,
        Sections,
        Bars,
        Details,
        Supports,
        LoadCases,
        Combinations,
        Mesh
    };

    enum Role : std::uint16_t {
        ItemIdRole = Qt::UserRole + 1, // quint64, 0 for rows that are not items
        GroupRole,                     // int (Group)
        StateRole,                     // int (setup::TargetState) of the targets
        ActiveRole                     // bool: the active load case or combination
    };

    explicit ModelTree(ModelController* controller, QObject* parent = nullptr);

    // The icon of an item whose targets are not valid (set by the window,
    // which has a style).
    void setWarningIcon(QIcon icon);

    [[nodiscard]] QModelIndex index(int row, int column,
                                    const QModelIndex& parent = {}) const override;
    [[nodiscard]] QModelIndex parent(const QModelIndex& child) const override;
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;

    // The item of a row, if it is one.
    [[nodiscard]] std::optional<setup::ItemId> itemAt(const QModelIndex& index) const;
    [[nodiscard]] std::optional<Group> groupAt(const QModelIndex& index) const;
    // The index of the row of an item (invalid when not shown).
    [[nodiscard]] QModelIndex indexOf(setup::ItemId id) const;
    // A short description of an item, as the tree shows it.
    [[nodiscard]] QString label(const setup::Item& item) const;

    // Rebuilt after every change of the document, the mesh or the drawing.
    void refresh();

private:
    struct Node
    {
        int parent = -1;
        Group group = Group::Units;
        std::optional<setup::ItemId> item;
        std::vector<int> children;
    };

    [[nodiscard]] QString groupName(Group group) const;
    [[nodiscard]] QString infoText(Group group) const;
    // The index of the new node.
    int addNode(int parent, Group group, std::optional<setup::ItemId> item);

    ModelController* m_controller = nullptr;
    QIcon m_warning;
    std::vector<Node> m_nodes; // all rows; the first ones are the groups
    std::vector<int> m_top;    // the group nodes, in Group order
};

} // namespace fem::app
