#pragma once

// Feature 010 (design §5.5, REQ-010-06): the drawing of a section: its
// outline, the principal axes, the centroid, the shear centre and the
// stress points. y up, z to the right, as the shapes are defined.

#include "section_geometry.hpp"

#include <fem/core/section.hpp>

#include <QWidget>

#include <optional>
#include <vector>

namespace fem::app {

class SectionView : public QWidget
{
    Q_OBJECT

public:
    explicit SectionView(QWidget* parent = nullptr);

    // The shape and, when it is valid, what the core computed for it.
    void setSection(const core::SectionShape& shape, const core::SectionSolution* solution);

    [[nodiscard]] QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    struct Marker
    {
        Eigen::Vector2d at;
        QString name;
    };

    std::vector<Ring> m_rings;
    std::optional<core::SectionProperties> m_properties;
    std::vector<Marker> m_points;
};

} // namespace fem::app
