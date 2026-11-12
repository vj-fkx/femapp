"""Generate the sample .msh files used by the io tests.

Usage (needs the Gmsh Python package, e.g. `pip install gmsh`):

    python tests/data/gmsh/generate_test_meshes.py            # sample files
    python tests/data/gmsh/generate_test_meshes.py --large DIR  # ~1M Tet4 box

Every sample is written twice, as ASCII (`*.msh`) and binary (`*_bin.msh`),
in Gmsh format 4.1. Gmsh is used only as a tool: the files it writes are not
covered by its license. See README.md in this folder.
"""

import math
import pathlib
import sys

import gmsh

HERE = pathlib.Path(__file__).resolve().parent


# --- helpers ------------------------------------------------------------------

def write_both(stem, folder=HERE):
    gmsh.option.setNumber("Mesh.MshFileVersion", 4.1)
    for binary, suffix in ((0, ""), (1, "_bin")):
        gmsh.option.setNumber("Mesh.Binary", binary)
        path = folder / f"{stem}{suffix}.msh"
        gmsh.write(str(path))
        print(f"  {path.relative_to(folder.parent.parent.parent) if folder == HERE else path}")


def all_types_transform(gmsh_type, point):
    """Affine map applied to the reference nodes of each type in all_types.msh.

    Must match allTypesTransform() in tests/unit/io/test_gmsh_reader.cpp.
    """
    angle = 0.3 + 0.05 * gmsh_type
    axis = (1.0 / math.sqrt(14.0), 2.0 / math.sqrt(14.0), 3.0 / math.sqrt(14.0))
    scale = 1.5 + 0.1 * (gmsh_type % 7)
    shift = (20.0 * gmsh_type, -3.0 * gmsh_type, 1.5 * gmsh_type)

    x, y, z = (scale * c for c in point)
    # Rodrigues' rotation formula.
    kx, ky, kz = axis
    c, s = math.cos(angle), math.sin(angle)
    dot = kx * x + ky * y + kz * z
    cross = (ky * z - kz * y, kz * x - kx * z, kx * y - ky * x)
    rotated = [v * c + cr * s + k * dot * (1.0 - c)
               for v, cr, k in zip((x, y, z), cross, axis)]
    return [r + t for r, t in zip(rotated, shift)]


def fixed_size_types():
    """Gmsh element types with a fixed number of nodes (design section 5.1)."""
    result = []
    for gmsh_type in range(1, 1000):
        try:
            name, dim, order, nodes, coords, vertices = (
                gmsh.model.mesh.getElementProperties(gmsh_type))
        except Exception:
            continue
        family = name.split()[0]
        if nodes < 1 or family in ("Polygon", "Polyhedron") or "Xfem" in name:
            continue
        if order == 0 and family != "Point":
            continue
        result.append((gmsh_type, dim, nodes, coords))
    return result


# --- samples ------------------------------------------------------------------

# Incomplete hexahedra of order 4 to 9 and incomplete pyramids of order 3 to 9:
# gmsh.model.mesh.addElementsByType crashes in Gmsh 4.15.2 for these types, so
# Gmsh cannot write them. femapp does not support them either.
GMSH_CANNOT_WRITE = {100, 101, 102, 103, 104, 105, 125, 126, 127, 128, 129, 130, 131}


def all_types():
    """One element of each supported type; element tag = Gmsh type number."""
    gmsh.model.add("all_types")
    node_tag = 1
    for gmsh_type, dim, nodes, coords in fixed_size_types():
        if gmsh_type in GMSH_CANNOT_WRITE:
            continue
        entity = gmsh.model.addDiscreteEntity(dim)
        tags, xyz = [], []
        for i in range(nodes):
            local = list(coords[i * dim:(i + 1) * dim]) if dim > 0 else []
            point = (local + [0.0, 0.0, 0.0])[:3]
            xyz.extend(all_types_transform(gmsh_type, point))
            tags.append(node_tag)
            node_tag += 1
        gmsh.model.mesh.addNodes(dim, entity, tags, xyz)
        gmsh.model.mesh.addElementsByType(entity, gmsh_type, [gmsh_type], tags)
    gmsh.option.setNumber("Mesh.SaveAll", 1)
    write_both("all_types")
    gmsh.option.setNumber("Mesh.SaveAll", 0)
    gmsh.model.remove()


def segment(order):
    """Segment of length 2, physical group 'segment'."""
    gmsh.model.add("segment")
    geo = gmsh.model.geo
    p1 = geo.addPoint(0, 0, 0)
    p2 = geo.addPoint(2, 0, 0)
    line = geo.addLine(p1, p2)
    geo.synchronize()
    gmsh.model.addPhysicalGroup(1, [line], name="segment")
    gmsh.model.mesh.setTransfiniteCurve(line, 6)
    gmsh.model.mesh.generate(1)
    gmsh.model.mesh.setOrder(order)
    write_both(f"segment_o{order}")
    gmsh.model.remove()


def square(stem, order, quads, incomplete=False):
    """Square 2 x 3 in the XY plane: 'plate' (2D) and an unnamed 1D group."""
    gmsh.model.add(stem)
    geo = gmsh.model.geo
    pts = [geo.addPoint(x, y, 0) for x, y in ((0, 0), (2, 0), (2, 3), (0, 3))]
    lines = [geo.addLine(pts[i], pts[(i + 1) % 4]) for i in range(4)]
    loop = geo.addCurveLoop(lines)
    surface = geo.addPlaneSurface([loop])
    geo.synchronize()
    gmsh.model.addPhysicalGroup(2, [surface], name="plate")
    gmsh.model.addPhysicalGroup(1, lines)  # no name: the reader makes one up
    gmsh.model.addPhysicalGroup(1, [lines[3]], name="left")
    gmsh.option.setNumber("Mesh.MeshSizeMax", 0.6)
    if quads:
        gmsh.model.mesh.setRecombine(2, surface)
    gmsh.model.mesh.generate(2)
    gmsh.option.setNumber("Mesh.SecondOrderIncomplete", 1 if incomplete else 0)
    gmsh.model.mesh.setOrder(order)
    gmsh.option.setNumber("Mesh.SecondOrderIncomplete", 0)
    write_both(stem)
    gmsh.model.remove()


def box_extruded(stem, order, quads, incomplete=False):
    """Box 1 x 2 x 3 extruded from its base: hexahedra or wedges."""
    gmsh.model.add(stem)
    geo = gmsh.model.geo
    pts = [geo.addPoint(x, y, 0) for x, y in ((0, 0), (1, 0), (1, 2), (0, 2))]
    lines = [geo.addLine(pts[i], pts[(i + 1) % 4]) for i in range(4)]
    base = geo.addPlaneSurface([geo.addCurveLoop(lines)])
    for line in lines:
        geo.mesh.setTransfiniteCurve(line, 3 if line in (lines[0], lines[2]) else 4)
    geo.mesh.setTransfiniteSurface(base)
    if quads:
        geo.mesh.setRecombine(2, base)
    out = geo.extrude([(2, base)], 0, 0, 3, numElements=[4], recombine=True)
    geo.synchronize()
    volume = [tag for dim, tag in out if dim == 3]
    gmsh.model.addPhysicalGroup(3, volume, name="solid")
    gmsh.model.addPhysicalGroup(2, [base], name="bottom")
    gmsh.model.mesh.generate(3)
    gmsh.option.setNumber("Mesh.SecondOrderIncomplete", 1 if incomplete else 0)
    gmsh.model.mesh.setOrder(order)
    gmsh.option.setNumber("Mesh.SecondOrderIncomplete", 0)
    write_both(stem)
    gmsh.model.remove()


def box_tets(stem, order, size, folder=HERE, shift=0.0):
    """Box 1 x 2 x 3 meshed with tetrahedra, optionally moved by `shift` on each axis."""
    gmsh.model.add(stem)
    box = gmsh.model.occ.addBox(shift, shift, shift, 1, 2, 3)
    gmsh.model.occ.synchronize()
    gmsh.model.addPhysicalGroup(3, [box], name="solid")
    faces = [tag for _, tag in gmsh.model.getBoundary([(3, box)], oriented=False)]
    gmsh.model.addPhysicalGroup(2, faces)  # no name
    gmsh.option.setNumber("Mesh.MeshSizeMax", size)
    gmsh.option.setNumber("Mesh.MeshSizeMin", size)
    gmsh.model.mesh.generate(3)
    gmsh.model.mesh.setOrder(order)
    write_both(stem, folder)
    gmsh.model.remove()


def main():
    gmsh.initialize()
    gmsh.option.setNumber("General.Terminal", 0)
    print(f"Gmsh {gmsh.__version__}")
    try:
        if len(sys.argv) == 3 and sys.argv[1] == "--large":
            # NFR-003-02: about one million Tet4, outside the repository.
            folder = pathlib.Path(sys.argv[2]).resolve()
            folder.mkdir(parents=True, exist_ok=True)
            box_tets("box_1m_tet4", 1, 0.030, folder)
            # REQ-004-18: a small mesh 1e6 units away from the origin.
            box_tets("box_shifted_1e6", 1, 0.25, folder, shift=1.0e6)
            return
        all_types()
        segment(1)
        segment(2)
        square("square_tri3", 1, quads=False)
        square("square_tri6", 2, quads=False)
        square("square_quad4", 1, quads=True)
        square("square_quad8", 2, quads=True, incomplete=True)
        square("square_quad9", 2, quads=True)
        box_extruded("box_hex8", 1, quads=True)
        box_extruded("box_hex20", 2, quads=True, incomplete=True)
        box_extruded("box_wedge6", 1, quads=False)
        box_extruded("box_wedge15", 2, quads=False, incomplete=True)
        box_tets("box_tet4", 1, 0.5)
        box_tets("box_tet10", 2, 0.5)
    finally:
        gmsh.finalize()


if __name__ == "__main__":
    main()
