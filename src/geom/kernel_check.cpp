#include <fem/geom/kernel_check.hpp>

#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <GProp_GProps.hxx>
#include <Standard_Version.hxx>
#include <TopoDS_Shape.hxx>

namespace fem::geom {

double boxVolume(double dx, double dy, double dz)
{
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(dx, dy, dz).Shape();
    GProp_GProps properties;
    BRepGProp::VolumeProperties(box, properties);
    return properties.Mass();
}

std::string kernelVersion()
{
    return OCC_VERSION_COMPLETE;
}

} // namespace fem::geom
