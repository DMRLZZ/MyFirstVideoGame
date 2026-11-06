"""LEFTRIGHT parametric car generator.

Builds original car bodies from design curves (silhouette, beltline, plan width...) as a dense lofted surface, carves
wheel arches, assigns material regions and adds wheels, glass, lights and a simple interior. No external 3D tools.

Coordinates of a design: meters, X forward (nose at +L/2), Y right, Z up, ground at Z = 0.

Usage:
    python -I Tools/CarGen/lrcargen.py <design.json> --preview out.png
    python -I Tools/CarGen/lrcargen.py <design.json> --glb out_dir
"""
import argparse
import json
import math
import pathlib
import struct

import numpy as np
from matplotlib.path import Path as PolyPath
from PIL import Image, ImageDraw
from scipy.interpolate import PchipInterpolator

MATERIALS = ["Paint", "Glass", "Trim", "Gap", "HeadLight", "TailLight", "Metal", "Interior", "Tire", "Rim", "Brake"]

# Grid layout of one half section: (segment name, number of quads). Top center -> bottom center.
SEGMENTS = [("roof_center", 14), ("roof_edge", 12), ("cabin_side", 26), ("shoulder", 14), ("side_upper", 22), ("side_lower", 18), ("rocker", 8), ("floor", 8)]


def curve(points):
    """Monotone cubic through [[x, value], ...] (no overshoot), constant outside the range."""
    pts = np.asarray(sorted(points), dtype=float)
    interp = PchipInterpolator(pts[:, 0], pts[:, 1], extrapolate=False)
    lo, hi = pts[0, 0], pts[-1, 0]
    return lambda x: interp(np.clip(x, lo, hi))


def polygon(points):
    return PolyPath(np.asarray(points, dtype=float))


class Mesh:
    """Triangle soup grouped by material, with per-vertex normals."""

    def __init__(self):
        self.parts = {m: {"v": [], "n": [], "f": [], "count": 0} for m in MATERIALS}

    def add(self, material, vertices, normals, faces):
        part = self.parts[material]
        part["v"].append(np.asarray(vertices, dtype=np.float32))
        part["n"].append(np.asarray(normals, dtype=np.float32))
        part["f"].append(np.asarray(faces, dtype=np.uint32) + part["count"])
        part["count"] += len(vertices)

    def arrays(self):
        result = {}
        for name, part in self.parts.items():
            if part["count"]:
                result[name] = (np.concatenate(part["v"]), np.concatenate(part["n"]), np.concatenate(part["f"]))
        return result

    def triangle_count(self):
        return sum(len(a[2]) for a in self.arrays().values())


def normalize(v):
    return v / np.maximum(np.linalg.norm(v, axis=-1, keepdims=True), 1e-9)


def grid_normals(P):
    """Vertex normals of a (stations, section) grid. Stations go rear to front, sections top to bottom on the +Y side."""
    du = np.gradient(P, axis=0)
    dv = np.gradient(P, axis=1)
    return normalize(np.cross(du, dv))


def add_grid(mesh, material_of_face, P, N, keep, mirror=True):
    """Adds a grid split by material. material_of_face: (stations-1, section-1) array of indices into MATERIALS."""
    ni, nj = P.shape[:2]
    index = np.arange(ni * nj).reshape(ni, nj)
    a, b = index[:-1, :-1].ravel(), index[1:, :-1].ravel()
    c, d = index[1:, 1:].ravel(), index[:-1, 1:].ravel()
    mats = material_of_face.ravel()
    keep = keep.ravel()
    verts, norms = P.reshape(-1, 3), N.reshape(-1, 3)
    for m, name in enumerate(MATERIALS):
        sel = (mats == m) & keep
        if not sel.any():
            continue
        # Front faces: seen from outside, the order is (origin, +section, +station), as Unreal expects.
        faces = np.concatenate([np.stack([a[sel], c[sel], b[sel]], 1), np.stack([a[sel], d[sel], c[sel]], 1)])
        add_faces(mesh, name, verts, norms, faces, mirror)


def add_faces(mesh, name, verts, norms, faces, mirror=True):
    used, inverse = np.unique(faces, return_inverse=True)
    v, n, f = verts[used], norms[used], inverse.reshape(-1, 3)
    mesh.add(name, v, n, f)
    if mirror:
        flip = np.array([1.0, -1.0, 1.0])
        mesh.add(name, v * flip, n * flip, f[:, ::-1])


def lathe(profile, segments=48):
    """Surface of revolution around the Y axis. profile: [(y, radius), ...]. Returns vertices, normals, faces."""
    profile = np.asarray(profile, dtype=float)
    ang = np.linspace(0, 2 * np.pi, segments + 1)
    P = np.zeros((len(profile), segments + 1, 3))
    P[..., 0] = profile[:, 1, None] * np.cos(ang)
    P[..., 1] = profile[:, 0, None]
    P[..., 2] = profile[:, 1, None] * np.sin(ang)
    du, dv = np.gradient(P, axis=0), np.gradient(P, axis=1)
    N = normalize(np.cross(du, dv))
    # Orient outwards from the axis where it is well defined.
    radial = P * np.array([1.0, 0.0, 1.0])
    if (N * radial).sum() < 0:
        N = -N
        flip = True
    else:
        flip = False
    ni, nj = P.shape[:2]
    idx = np.arange(ni * nj).reshape(ni, nj)
    a, b, c, d = idx[:-1, :-1].ravel(), idx[1:, :-1].ravel(), idx[1:, 1:].ravel(), idx[:-1, 1:].ravel()
    f = np.concatenate([np.stack([a, b, c], 1), np.stack([a, c, d], 1)])
    if not flip:
        f = f[:, ::-1]
    return P.reshape(-1, 3), N.reshape(-1, 3), f


def box(center, size, yaw_deg=0.0, pitch_deg=0.0):
    """Axis-aligned box with hard edges, rotated around its center."""
    c, s = np.asarray(center, float), np.asarray(size, float) * 0.5
    dirs = [((1, 0, 0), (0, 1, 0), (0, 0, 1)), ((-1, 0, 0), (0, 0, 1), (0, 1, 0)), ((0, 1, 0), (0, 0, 1), (1, 0, 0)),
            ((0, -1, 0), (1, 0, 0), (0, 0, 1)), ((0, 0, 1), (1, 0, 0), (0, 1, 0)), ((0, 0, -1), (0, 1, 0), (1, 0, 0))]
    V, N, F = [], [], []
    for k, (n, u, v) in enumerate(dirs):
        n, u, v = np.array(n, float), np.array(u, float), np.array(v, float)
        corners = [n + a * u + b * v for a, b in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
        V += [p * s for p in corners]
        N += [n] * 4
        F += [[4 * k, 4 * k + 2, 4 * k + 1], [4 * k, 4 * k + 3, 4 * k + 2]]
    V, N = np.array(V), np.array(N)
    y, p = math.radians(yaw_deg), math.radians(pitch_deg)
    Ry = np.array([[math.cos(p), 0, math.sin(p)], [0, 1, 0], [-math.sin(p), 0, math.cos(p)]])
    Rz = np.array([[math.cos(y), -math.sin(y), 0], [math.sin(y), math.cos(y), 0], [0, 0, 1]])
    R = Rz @ Ry
    return (V @ R.T) + c, N @ R.T, np.array(F)


def ellipsoid(center, radii, rings=14, segments=20):
    th = np.linspace(0, np.pi, rings + 1)[:, None]
    ph = np.linspace(0, 2 * np.pi, segments + 1)[None, :]
    unit = np.stack([np.sin(th) * np.cos(ph), np.sin(th) * np.sin(ph), np.cos(th) * np.ones_like(ph)], -1)
    radii = np.asarray(radii, float)
    P = unit * radii + np.asarray(center, float)
    N = normalize(unit / radii)
    idx = np.arange(P.shape[0] * P.shape[1]).reshape(P.shape[:2])
    a, b, c, d = idx[:-1, :-1].ravel(), idx[1:, :-1].ravel(), idx[1:, 1:].ravel(), idx[:-1, 1:].ravel()
    f = np.concatenate([np.stack([a, c, b], 1), np.stack([a, d, c], 1)])
    return P.reshape(-1, 3), N.reshape(-1, 3), f


def add_mirrored(mesh, material, part):
    v, n, f = part
    mesh.add(material, v, n, f)
    flip = np.array([1.0, -1.0, 1.0])
    mesh.add(material, v * flip, n * flip, f[:, ::-1])


def build_body(design):
    """Lofted body shell. Returns a Mesh."""
    L = design["Length"]
    c = {k: curve(design["Curves"][k]) for k in ("Top", "Belt", "HalfWidth", "RoofHalfWidth", "BeltHalfWidth", "Bottom", "SideZ")}

    # Stations clustered at the nose and the tail, where curvature is highest.
    n_st = max(40, int(design.get("Stations", 440) * design.get("Quality", 1.0)))
    t = np.linspace(0, 1, n_st)
    x = -L / 2 + L * (0.5 - 0.5 * np.cos(np.pi * (0.35 * t + 0.65 * (t * t * (3 - 2 * t)))))
    x = -L / 2 + L * (0.75 * t + 0.25 * (0.5 - 0.5 * np.cos(np.pi * t)))
    x[0], x[-1] = -L / 2, L / 2

    quality = design.get("Quality", 1.0)
    counts = [max(2, int(round(n * quality))) for _, n in SEGMENTS]
    n_sec = sum(counts) + 1
    knots = np.arange(len(SEGMENTS) + 1, dtype=float)
    params = np.concatenate([np.linspace(k, k + 1, n, endpoint=False) for k, n in enumerate(counts)] + [[float(len(counts))]])

    P = np.zeros((n_st, n_sec, 3))
    for i, xi in enumerate(x):
        zt, zb, wm = float(c["Top"](xi)), float(c["Belt"](xi)), float(c["HalfWidth"](xi))
        wr, wb, zbot, zs = float(c["RoofHalfWidth"](xi)), float(c["BeltHalfWidth"](xi)), float(c["Bottom"](xi)), float(c["SideZ"](xi))
        zt = max(zt, zb + 0.004)
        wb = min(wb, wm - 0.005)
        wr = min(wr, wb - 0.002)
        crown = 0.012 + 0.02 * min(1.0, wr / 0.5)
        ctrl = [
            (0.0, zt),
            (0.62 * wr, zt - 0.32 * crown),
            (wr, zt - crown),
            (wb, zb + 0.004),
            (wm - 0.012, zb - 0.075),
            (wm, zs),
            (wm - 0.035, zbot + 0.11),
            (wm - 0.075, zbot + 0.012),
            (0.0, zbot),
        ]
        ctrl = np.array(ctrl)
        P[i, :, 1] = PchipInterpolator(knots, ctrl[:, 0])(params)
        P[i, :, 2] = PchipInterpolator(knots, ctrl[:, 1])(params)
        P[i, :, 0] = xi

    seg_start = np.cumsum([0] + counts)
    j_cabin0, j_side0, j_side1 = seg_start[2], seg_start[4], seg_start[6]

    # --- Sculpting on the flanks ------------------------------------------------------------------
    for dent in design.get("Dents", []):
        d = np.exp(-(((P[..., 0] - dent["X"]) / dent["SX"]) ** 2) - (((P[..., 2] - dent["Z"]) / dent["SZ"]) ** 2))
        P[..., 1] -= dent["Depth"] * d * np.clip(P[..., 1] / max(float(c["HalfWidth"](dent["X"])), 0.1), 0, 1) ** 3

    # --- Wheel arches: flare, then snap the rim of the opening onto an exact circle ------------------
    keep = np.ones((n_st - 1, n_sec - 1), dtype=bool)
    arches = []
    for ax, radius in ((design["FrontAxleX"], design["FrontWheelRadius"]), (design["RearAxleX"], design["RearWheelRadius"])):
        R = radius + design.get("ArchGap", 0.05)
        arches.append((ax, radius, R))
        d = np.hypot(P[..., 0] - ax, P[..., 2] - radius)
        side = np.arange(n_sec) >= j_side0 - 8
        flare = design.get("ArchFlare", 0.022) * np.exp(-(((d - R) / 0.10) ** 2)) * (d > R - 0.02)
        P[..., 1] += flare * side[None, :]
        snap = (np.abs(d - R) < 0.018) & side[None, :]
        ang = np.arctan2(P[..., 2] - radius, P[..., 0] - ax)
        P[..., 0] = np.where(snap, ax + R * np.cos(ang), P[..., 0])
        P[..., 2] = np.where(snap, radius + R * np.sin(ang), P[..., 2])

    N = grid_normals(P)

    # --- Materials from the face centers -----------------------------------------------------------
    F = 0.25 * (P[:-1, :-1] + P[1:, :-1] + P[1:, 1:] + P[:-1, 1:])
    fx, fy, fz = F[..., 0], F[..., 1], F[..., 2]
    fj = np.arange(n_sec - 1)[None, :] * np.ones((n_st - 1, 1), dtype=int)
    mats = np.zeros(fx.shape, dtype=np.int32)
    idx = {m: k for k, m in enumerate(MATERIALS)}

    def inside(poly_pts, a, b, where=None):
        pts = np.stack([a.ravel(), b.ravel()], 1)
        hit = polygon(poly_pts).contains_points(pts).reshape(a.shape)
        return hit if where is None else hit & where

    regions = design["Regions"]
    upper = fj < j_side0  # Everything above the shoulder.
    for name, material, plane, condition in (
        ("Windshield", "Glass", "xy", upper),
        ("RearGlass", "Glass", "xy", upper),
        ("SideWindow", "Glass", "xz", fj < j_side0 + 2),
        ("HoodVent", "Trim", "xy", upper),
        ("DeckVent", "Trim", "xy", upper),
        ("HeadLight", "HeadLight", "xy", fx > 0),
        ("Grille", "Trim", "yz", fx > 0),
        ("LowerIntake", "Trim", "yz", fx > 0),
        ("TailLight", "TailLight", "yz", fx < 0),
        ("RearBlackout", "Trim", "yz", fx < 0),
        ("Diffuser", "Trim", "yz", fx < 0),
        ("SideIntake", "Trim", "xz", fj >= j_side0 - 4),
        ("Skirt", "Trim", "xz", fj >= j_side0),
    ):
        for poly in regions.get(name, []):
            hit = inside(poly, fx, fy if plane == "xy" else (fz if plane == "xz" else fy), None) if plane != "yz" else inside(poly, fy, fz)
            if plane == "xz":
                hit = inside(poly, fx, fz)
            mats[hit & condition] = idx[material]

    # Panel gaps: thin dark lines drawn as material bands.
    gap = design.get("GapWidth", 0.009)
    for line in design.get("GapLines", []):
        pts = np.asarray(line["Points"], float)
        if line["Plane"] == "xz":
            a, b = fx, fz
            cond = (fj >= j_cabin0)
        else:
            a, b = fx, fy
            cond = upper
        for p0, p1 in zip(pts[:-1], pts[1:]):
            seg = p1 - p0
            tt = np.clip(((a - p0[0]) * seg[0] + (b - p0[1]) * seg[1]) / max(seg @ seg, 1e-9), 0, 1)
            dist = np.hypot(a - (p0[0] + tt * seg[0]), b - (p0[1] + tt * seg[1]))
            sel = (dist < gap) & cond & (mats == 0)
            mats[sel] = idx["Gap"]

    # Bottom of the car is dark.
    mats[fj >= seg_start[6]] = idx["Trim"]

    for ax, radius, R in arches:
        keep &= ~((np.hypot(fx - ax, fz - radius) < R) & (fj >= j_side0 - 8))

    mesh = Mesh()
    add_grid(mesh, mats, P, N, keep)
    build_details(mesh, design, c, arches)
    return mesh, arches


def build_details(mesh, design, c, arches):
    # Wheel wells: dark drum behind each opening.
    for ax, radius, R in arches:
        outer = float(c["HalfWidth"](ax)) - 0.015
        v, n, f = lathe([(outer, R + 0.01), (outer - 0.42, R + 0.01), (outer - 0.42, 0.02)], 56)
        v = v + np.array([ax, 0.0, radius])
        n = -n
        add_mirrored(mesh, "Gap", (v, n, f[:, ::-1]))

    # Cabin interior: tub, seats, dash and wheel. Seen through the glass.
    cab = design.get("Cabin")
    if cab:
        z = cab["FloorZ"]
        add_mirrored(mesh, "Interior", box((cab["SeatX"], cab["SeatY"], z + 0.26), (0.46, 0.46, 0.14)))
        add_mirrored(mesh, "Interior", box((cab["SeatX"] - 0.27, cab["SeatY"], z + 0.56), (0.13, 0.44, 0.62), pitch_deg=-14))
        add_mirrored(mesh, "Interior", box((cab["SeatX"] - 0.36, cab["SeatY"], z + 0.92), (0.11, 0.24, 0.16), pitch_deg=-14))
        add_mirrored(mesh, "Interior", box((cab["DashX"], 0.45, z + 0.46), (0.5, 0.9, 0.24), pitch_deg=8))
        add_mirrored(mesh, "Interior", box((cab["TubX"], 0.48, z + 0.12), (cab["TubLength"], 1.0, 0.1)))
        add_mirrored(mesh, "Interior", box((cab["SeatX"] - 0.62, 0.45, z + 0.32), (0.1, 0.9, 0.58)))
        add_mirrored(mesh, "Interior", box((cab["TubX"], 0.03, z + 0.28), (cab["TubLength"], 0.2, 0.3)))
        wheel = lathe([(-0.012, 0.165), (0.0, 0.18), (0.012, 0.165), (0.0, 0.15), (-0.012, 0.165)], 28)
        v, n, f = wheel
        tilt = math.radians(68)
        R = np.array([[0, -math.cos(0), 0], [1, 0, 0], [0, 0, 1]], float)
        v2 = np.stack([v[:, 1] * math.cos(tilt) - 0.0, v[:, 0], v[:, 2] + v[:, 1] * math.sin(tilt)], 1) + [cab["SeatX"] + 0.42, -cab["SeatY"], z + 0.62]
        n2 = np.stack([n[:, 1] * math.cos(tilt), n[:, 0], n[:, 2] + n[:, 1] * math.sin(tilt)], 1)
        mesh.add("Trim", v2, normalize(n2), f)

    # Mirrors.
    m = design.get("Mirror")
    if m:
        add_mirrored(mesh, "Paint", ellipsoid((m["X"], m["Y"], m["Z"]), (0.06, 0.085, 0.042)))
        add_mirrored(mesh, "Trim", box((m["X"] + 0.02, m["Y"] - 0.08, m["Z"] - 0.02), (0.04, 0.11, 0.018)))

    # Exhausts.
    for e in design.get("Exhausts", []):
        v, n, f = lathe([(-0.22, 0.042), (0.0, 0.047), (0.006, 0.042), (-0.05, 0.034)], 28)
        v = np.stack([v[:, 1] + e["X"] + 0.0, v[:, 0] * 0 + v[:, 0] * 0 + v[:, 2] + e["Y"], v[:, 0] * 0 + v[:, 2] * 0 + e["Z"] + v[:, 2] * 0], 1) if False else v
        # Axis along X: swap X and Y of the lathe.
        vv = np.stack([-v[:, 1] + e["X"], v[:, 0] + e["Y"], v[:, 2] + e["Z"]], 1)
        nn = np.stack([-n[:, 1], n[:, 0], n[:, 2]], 1)
        add_mirrored(mesh, "Metal", (vv, nn, f))

    # Splitter / wing.
    split = design.get("Splitter")
    if split:
        add_mirrored(mesh, "Trim", box((split["X"], split["HalfWidth"] / 2, split["Z"]), (split["Depth"], split["HalfWidth"], 0.022)))
    wing = design.get("Wing")
    if wing:
        add_mirrored(mesh, "Trim", box((wing["X"], wing["HalfSpan"] / 2, wing["Z"]), (wing["Chord"], wing["HalfSpan"], 0.02), pitch_deg=wing.get("Pitch", -8)))
        add_mirrored(mesh, "Trim", box((wing["X"] + 0.04, wing["PostY"], wing["Z"] - wing["PostH"] / 2), (0.1, 0.018, wing["PostH"])))
        add_mirrored(mesh, "Trim", box((wing["X"], wing["HalfSpan"], wing["Z"] - 0.03), (wing["Chord"] + 0.05, 0.012, 0.1)))


def build_wheel(radius, width, spokes=10, rim_style="split", segments=72):
    """One wheel: origin at the hub, +Y is the outer face. Separate mesh so the game can spin it."""
    mesh = Mesh()
    rim_r = radius * 0.72
    w = width / 2
    tire = [(-w * 0.82, rim_r * 0.99), (-w, rim_r * 1.06), (-w, radius * 0.93), (-w * 0.85, radius * 0.99), (0.0, radius), (w * 0.85, radius * 0.99), (w, radius * 0.93), (w, rim_r * 1.06), (w * 0.82, rim_r * 0.99)]
    mesh.add("Tire", *lathe(tire, segments))
    barrel = [(w * 0.9, rim_r * 1.0), (w * 0.82, rim_r * 0.94), (w * 0.2, rim_r * 0.9), (-w * 0.8, rim_r * 0.88), (-w * 0.95, rim_r * 0.97)]
    v, n, f = lathe(barrel, segments)
    mesh.add("Rim", v, -n, f[:, ::-1])
    # Brake disc and caliper seen through the spokes.
    mesh.add("Brake", *lathe([(-w * 0.2, 0.0), (-w * 0.2, rim_r * 0.82), (-w * 0.05, rim_r * 0.82), (-w * 0.05, 0.0)], 48))
    hub = [(w * 0.62, 0.0), (w * 0.62, rim_r * 0.2), (w * 0.5, rim_r * 0.23), (w * 0.1, rim_r * 0.2)]
    mesh.add("Rim", *lathe(hub, 32))
    # Spokes: pairs of thin tapered bars.
    pairs = spokes // 2 if rim_style == "split" else spokes
    for k in range(pairs):
        a0 = 2 * math.pi * k / pairs
        for off in ((-0.075, 0.075) if rim_style == "split" else (0.0,)):
            inner, outer = rim_r * 0.2, rim_r * 0.97
            a = a0 + off * 0.35
            b = a0 + off * 1.15
            p0 = np.array([inner * math.cos(a), w * 0.58, inner * math.sin(a)])
            p1 = np.array([outer * math.cos(b), w * 0.8, outer * math.sin(b)])
            axis = p1 - p0
            length = np.linalg.norm(axis)
            u = axis / length
            side = normalize(np.cross(u, [0, 1, 0]))
            up = np.cross(side, u)
            hw0, hw1, ht = 0.02, 0.014, 0.018
            corners = np.array([p0 - side * hw0 - up * ht, p0 + side * hw0 - up * ht, p0 + side * hw0 + up * ht, p0 - side * hw0 + up * ht,
                                p1 - side * hw1 - up * ht, p1 + side * hw1 - up * ht, p1 + side * hw1 + up * ht, p1 - side * hw1 + up * ht])
            quads = [(0, 1, 5, 4, -up), (1, 2, 6, 5, side), (2, 3, 7, 6, up), (3, 0, 4, 7, -side)]
            V, N, F = [], [], []
            for q, (i0, i1, i2, i3, nrm) in enumerate(quads):
                V += [corners[i0], corners[i1], corners[i2], corners[i3]]
                N += [nrm] * 4
                F += [[4 * q, 4 * q + 1, 4 * q + 2], [4 * q, 4 * q + 2, 4 * q + 3]]
            mesh.add("Rim", np.array(V), np.array(N), np.array(F))
    return mesh


def place_wheels(mesh, design, segments=72):
    """Bakes the four wheels into a mesh, for preview and for the traffic model."""
    for ax, radius, name in ((design["FrontAxleX"], design["FrontWheelRadius"], "Front"), (design["RearAxleX"], design["RearWheelRadius"], "Rear")):
        width = design[name + "WheelWidth"]
        y = design[name + "TrackHalf"]
        wheel = build_wheel(radius, width, design.get("Spokes", 10), design.get("RimStyle", "split"), segments)
        for mat, (v, n, f) in wheel.arrays().items():
            vv = v + np.array([ax, y, radius], dtype=np.float32)
            add_mirrored(mesh, mat, (vv, n, f))


# --- Preview ----------------------------------------------------------------------------------------

PREVIEW_COLORS = {
    "Paint": (205, 40, 28), "Glass": (28, 36, 44), "Trim": (24, 24, 26), "Gap": (6, 6, 6), "HeadLight": (235, 240, 250),
    "TailLight": (170, 12, 16), "Metal": (190, 190, 195), "Interior": (40, 34, 30), "Tire": (20, 20, 20), "Rim": (175, 178, 185), "Brake": (90, 90, 95),
}


def render(mesh, eye, target, size=(900, 520), fov=26.0, stride=1, paint=None):
    """Very small z-buffered rasterizer with a sky gradient reflection; good enough to judge shape."""
    W, H = size
    eye, target = np.asarray(eye, float), np.asarray(target, float)
    f = normalize(target - eye)
    r = normalize(np.cross(f, [0, 0, 1.0]))
    u = np.cross(r, f)
    focal = 0.5 * H / math.tan(math.radians(fov) / 2)
    color_buf = np.zeros((H, W, 3), dtype=np.float32)
    grad = np.linspace(1, 0, H)[:, None, None]
    color_buf[:] = (grad * np.array([120, 130, 145]) + (1 - grad) * np.array([62, 60, 58]))
    z_buf = np.full((H, W), 1e9)
    light = normalize(np.array([0.45, 0.35, 0.85]))
    palette = dict(PREVIEW_COLORS)
    if paint:
        palette["Paint"] = tuple(paint)
    for name, (V, Nv, F) in mesh.arrays().items():
        base = np.array(palette[name], dtype=np.float32)
        T = V.astype(np.float64)[F]
        c = T - eye
        cx, cy, cz = c @ r, c @ u, c @ f
        front = cz.min(axis=1) > 0.05
        sx, sy = W / 2 + focal * cx / np.maximum(cz, 0.05), H / 2 - focal * cy / np.maximum(cz, 0.05)
        n_tri = normalize(Nv.astype(np.float64)[F].mean(axis=1))
        view = normalize(T.mean(axis=1) - eye)
        lit = np.clip(n_tri @ light, 0, 1)
        refl = view - 2 * (view * n_tri).sum(1, keepdims=True) * n_tri
        sky = np.clip(refl[:, 2] * 0.5 + 0.5, 0, 1)
        spec = np.clip(refl @ light, 0, 1) ** 40
        fres = (1 - np.clip(-(view * n_tri).sum(1), 0, 1)) ** 3
        amount = {"Paint": 0.55, "Glass": 0.9, "Metal": 0.9, "Rim": 0.7, "HeadLight": 0.8, "TailLight": 0.4}.get(name, 0.08)
        shade = (0.22 + 0.72 * lit)[:, None] * base + amount * (sky * (60 + 170 * fres))[:, None] + (amount * 240 * spec)[:, None]
        min_x = np.floor(sx.min(1)).astype(int).clip(0, W - 1)
        max_x = np.ceil(sx.max(1)).astype(int).clip(0, W - 1)
        min_y = np.floor(sy.min(1)).astype(int).clip(0, H - 1)
        max_y = np.ceil(sy.max(1)).astype(int).clip(0, H - 1)
        # Fast path: most triangles cover a few pixels, splat them with a small vectorized footprint.
        small = front & ((max_x - min_x) <= 3) & ((max_y - min_y) <= 3)
        if small.any():
            order = np.nonzero(small)[0]
            depth_t = cz[order].mean(1)
            order, depth_t = order[np.argsort(-depth_t)], np.sort(depth_t)[::-1]
            for ox, oy in ((0, 0), (1, 0), (0, 1), (1, 1)):
                px = np.clip(min_x[order] + ox, 0, W - 1)
                py = np.clip(min_y[order] + oy, 0, H - 1)
                inside_box = (px <= max_x[order]) & (py <= max_y[order])
                ok = inside_box & (depth_t < z_buf[py, px])
                # Painter order (far to near): later writes win.
                color_buf[py[ok], px[ok]] = shade[order[ok]]
                np.minimum.at(z_buf, (py[ok], px[ok]), depth_t[ok])
        for t in np.nonzero(front & ~small)[0][::stride]:
            x0, x1, y0, y1 = min_x[t], max_x[t], min_y[t], max_y[t]
            if x1 < x0 or y1 < y0 or (x1 - x0) * (y1 - y0) > 40000:
                continue
            xs, ys = np.meshgrid(np.arange(x0, x1 + 1), np.arange(y0, y1 + 1))
            ax, ay, bx, by, ccx, ccy = sx[t, 0], sy[t, 0], sx[t, 1], sy[t, 1], sx[t, 2], sy[t, 2]
            den = (by - ccy) * (ax - ccx) + (ccx - bx) * (ay - ccy)
            if abs(den) < 1e-9:
                continue
            w0 = ((by - ccy) * (xs - ccx) + (ccx - bx) * (ys - ccy)) / den
            w1 = ((ccy - ay) * (xs - ccx) + (ax - ccx) * (ys - ccy)) / den
            w2 = 1 - w0 - w1
            mask = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
            if not mask.any():
                continue
            depth = w0 * cz[t, 0] + w1 * cz[t, 1] + w2 * cz[t, 2]
            region = z_buf[y0:y1 + 1, x0:x1 + 1]
            mask &= depth < region
            region[mask] = depth[mask]
            color_buf[y0:y1 + 1, x0:x1 + 1][mask] = shade[t]
    return Image.fromarray(np.clip(color_buf, 0, 255).astype(np.uint8))


def preview_sheet(mesh, path, design):
    L = design["Length"]
    views = [
        ((L * 1.05, -L * 1.25, 1.25), (0.05, 0, 0.5)),
        ((-L * 1.1, -L * 1.2, 1.35), (0, 0, 0.5)),
        ((0.0, -L * 2.3, 0.55), (0, 0, 0.5)),
        ((L * 2.2, 0.01, 0.75), (0, 0, 0.5)),
        ((-L * 2.2, 0.01, 0.85), (0, 0, 0.5)),
        ((0.15, 0.01, L * 2.4), (0, 0, 0.3)),
    ]
    tiles = [render(mesh, eye, tgt, size=(800, 460)) for eye, tgt in views]
    sheet = Image.new("RGB", (1600, 1380))
    for k, tile in enumerate(tiles):
        sheet.paste(tile, ((k % 2) * 800, (k // 2) * 460))
    sheet.save(path)


# --- Export -----------------------------------------------------------------------------------------

def write_glb(mesh, path):
    """Single-mesh glTF: one primitive per material, so Unreal gets one static mesh with named slots."""
    arrays = mesh.arrays()
    blob = bytearray()
    views, accessors, primitives, materials = [], [], [], []

    def add_view(data, target):
        offset = len(blob)
        blob.extend(data)
        while len(blob) % 4:
            blob.append(0)
        views.append({"buffer": 0, "byteOffset": offset, "byteLength": len(data), "target": target})
        return len(views) - 1

    for k, (name, (V, N, F)) in enumerate(arrays.items()):
        # Unreal's glTF import maps (x, y, z) to (x, z, y), so swapping Y and Z here lands on design axes in Unreal.
        gltf_v = np.stack([V[:, 0], V[:, 2], V[:, 1]], 1).astype(np.float32)
        gltf_n = np.stack([N[:, 0], N[:, 2], N[:, 1]], 1).astype(np.float32)
        indices = F.astype(np.uint32)
        vp = add_view(gltf_v.tobytes(), 34962)
        accessors.append({"bufferView": vp, "componentType": 5126, "count": len(gltf_v), "type": "VEC3", "min": gltf_v.min(0).tolist(), "max": gltf_v.max(0).tolist()})
        pos = len(accessors) - 1
        vn = add_view(gltf_n.tobytes(), 34962)
        accessors.append({"bufferView": vn, "componentType": 5126, "count": len(gltf_n), "type": "VEC3"})
        nrm = len(accessors) - 1
        vi = add_view(indices.tobytes(), 34963)
        accessors.append({"bufferView": vi, "componentType": 5125, "count": int(indices.size), "type": "SCALAR"})
        idx = len(accessors) - 1
        materials.append({"name": name, "pbrMetallicRoughness": {"baseColorFactor": [c / 255 for c in PREVIEW_COLORS[name]] + [1.0]}})
        primitives.append({"attributes": {"POSITION": pos, "NORMAL": nrm}, "indices": idx, "material": k})

    doc = {
        "asset": {"version": "2.0", "generator": "lrcargen"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": path.stem}],
        "meshes": [{"name": path.stem, "primitives": primitives}],
        "materials": materials,
        "buffers": [{"byteLength": len(blob)}],
        "bufferViews": views,
        "accessors": accessors,
    }
    text = json.dumps(doc, separators=(",", ":")).encode()
    text += b" " * ((4 - len(text) % 4) % 4)
    with open(path, "wb") as out:
        out.write(struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(text) + 8 + len(blob)))
        out.write(struct.pack("<II", len(text), 0x4E4F534A) + text)
        out.write(struct.pack("<II", len(blob), 0x004E4942) + bytes(blob))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("design")
    ap.add_argument("--preview")
    ap.add_argument("--glb")
    args = ap.parse_args()

    design = json.loads(pathlib.Path(args.design).read_text(encoding="utf-8"))
    mesh, _arches = build_body(design)

    if args.preview:
        full = Mesh()
        for mat, part in mesh.arrays().items():
            full.add(mat, *part)
        place_wheels(full, design)
        preview_sheet(full, args.preview, design)
        print("triangles (with wheels):", full.triangle_count())

    if args.glb:
        out = pathlib.Path(args.glb)
        out.mkdir(parents=True, exist_ok=True)
        car_id = pathlib.Path(args.design).stem
        write_glb(mesh, out / f"SM_{car_id}_Body.glb")
        # Light variant for traffic: coarser body, wheels baked in (traffic wheels do not need to spin up close).
        low_design = dict(design, Quality=0.3)
        low, _ = build_body(low_design)
        place_wheels(low, low_design, segments=20)
        write_glb(low, out / f"SM_{car_id}_Traffic.glb")
        print("traffic triangles:", low.triangle_count())
        write_glb(build_wheel(design["RearWheelRadius"], design["RearWheelWidth"], design.get("Spokes", 10), design.get("RimStyle", "split")), out / f"SM_{car_id}_Wheel.glb")
        print("written to", out, "body triangles:", mesh.triangle_count())


if __name__ == "__main__":
    main()
