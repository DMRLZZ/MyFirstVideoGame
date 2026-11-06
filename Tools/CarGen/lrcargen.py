"""LEFTRIGHT parametric car generator.

Builds original car bodies from design curves (silhouette, beltline, plan width...) as a dense lofted surface, then:
snaps the grid onto region outlines so edges are clean, recesses panel gaps, grilles and lamps into real depth, splits
the surface along character lines so reflections break sharply, and adds wheels, LED signatures and an interior.

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
from PIL import Image
from scipy.interpolate import PchipInterpolator
from scipy.ndimage import gaussian_filter, gaussian_filter1d
from scipy.spatial import cKDTree

MATERIALS = ["Paint", "Glass", "Trim", "Gap", "HeadLight", "DRL", "TailLight", "TailLED", "Metal", "Interior", "Screen", "Tire", "Rim", "Brake", "Caliper"]

# Grid layout of one half section: (segment name, number of quads). Top center -> bottom center.
SEGMENTS = [("roof_center", 14), ("roof_edge", 12), ("cabin_side", 26), ("shoulder", 14), ("side_upper", 22), ("side_lower", 18), ("rocker", 8), ("floor", 8)]

# name, material, plane, recess depth in meters.
REGIONS = [
    ("Windshield", "Glass", "xy", 0.004),
    ("RearGlass", "Glass", "xy", 0.004),
    ("SideWindow", "Glass", "xz", 0.008),
    ("HoodVent", "Trim", "xy", 0.016),
    ("DeckVent", "Trim", "xy", 0.012),
    ("HeadLight", "HeadLight", "xy", 0.014),
    ("Grille", "Trim", "yz", 0.04),
    ("LowerIntake", "Trim", "yz", 0.055),
    ("TailLight", "TailLight", "yz", 0.008),
    ("RearBlackout", "Trim", "yz", 0.006),
    ("Diffuser", "Trim", "yz", 0.035),
    ("SideIntake", "Trim", "xz", 0.05),
    ("Skirt", "Trim", "xz", 0.0),
]


def curve(points):
    """Monotone cubic through [[x, value], ...] (no overshoot), constant outside the range."""
    pts = np.asarray(sorted(points), dtype=float)
    interp = PchipInterpolator(pts[:, 0], pts[:, 1], extrapolate=False)
    lo, hi = pts[0, 0], pts[-1, 0]
    return lambda x: interp(np.clip(x, lo, hi))


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
    return normalize(np.cross(np.gradient(P, axis=0), np.gradient(P, axis=1)))


def add_faces(mesh, name, verts, norms, faces, mirror=True):
    used, inverse = np.unique(faces, return_inverse=True)
    v, n, f = verts[used], norms[used], inverse.reshape(-1, 3)
    mesh.add(name, v, n, f)
    if mirror:
        flip = np.array([1.0, -1.0, 1.0])
        mesh.add(name, v * flip, n * flip, f[:, ::-1])


def add_grid(mesh, material_of_face, P, N, keep):
    """Adds a grid split by material (one mesh section per material), mirrored to the other side."""
    ni, nj = P.shape[:2]
    index = np.arange(ni * nj).reshape(ni, nj)
    a, b = index[:-1, :-1].ravel(), index[1:, :-1].ravel()
    c, d = index[1:, 1:].ravel(), index[:-1, 1:].ravel()
    mats, keep = material_of_face.ravel(), keep.ravel()
    verts, norms = P.reshape(-1, 3), N.reshape(-1, 3)
    for m, name in enumerate(MATERIALS):
        sel = (mats == m) & keep
        if sel.any():
            # Front faces: seen from outside, the order is (origin, +section, +station), as Unreal expects.
            faces = np.concatenate([np.stack([a[sel], c[sel], b[sel]], 1), np.stack([a[sel], d[sel], c[sel]], 1)])
            add_faces(mesh, name, verts, norms, faces)


def transform(part, R=None, T=(0, 0, 0)):
    v, n, f = part
    R = np.eye(3) if R is None else np.asarray(R, float)
    return (np.asarray(v) @ R.T) + np.asarray(T, float), np.asarray(n) @ R.T, f


def add_mirrored(mesh, material, part):
    v, n, f = part
    mesh.add(material, v, n, f)
    flip = np.array([1.0, -1.0, 1.0])
    mesh.add(material, np.asarray(v) * flip, np.asarray(n) * flip, np.asarray(f)[:, ::-1])


def lathe(profile, segments=48):
    """Surface of revolution around the Y axis. profile: [(y, radius), ...]. Returns vertices, normals, faces."""
    profile = np.asarray(profile, dtype=float)
    ang = np.linspace(0, 2 * np.pi, segments + 1)
    P = np.zeros((len(profile), segments + 1, 3))
    P[..., 0] = profile[:, 1, None] * np.cos(ang)
    P[..., 1] = profile[:, 0, None]
    P[..., 2] = profile[:, 1, None] * np.sin(ang)
    N = normalize(np.cross(np.gradient(P, axis=0), np.gradient(P, axis=1)))
    flip = (N * (P * np.array([1.0, 0.0, 1.0]))).sum() < 0
    if flip:
        N = -N
    ni, nj = P.shape[:2]
    idx = np.arange(ni * nj).reshape(ni, nj)
    a, b, c, d = idx[:-1, :-1].ravel(), idx[1:, :-1].ravel(), idx[1:, 1:].ravel(), idx[:-1, 1:].ravel()
    f = np.concatenate([np.stack([a, b, c], 1), np.stack([a, c, d], 1)])
    if not flip:
        f = f[:, ::-1]
    return P.reshape(-1, 3), N.reshape(-1, 3), f


def box(center, size, yaw_deg=0.0, pitch_deg=0.0):
    """Box with hard edges, rotated around its center."""
    c, s = np.asarray(center, float), np.asarray(size, float) * 0.5
    dirs = [((1, 0, 0), (0, 1, 0), (0, 0, 1)), ((-1, 0, 0), (0, 0, 1), (0, 1, 0)), ((0, 1, 0), (0, 0, 1), (1, 0, 0)),
            ((0, -1, 0), (1, 0, 0), (0, 0, 1)), ((0, 0, 1), (1, 0, 0), (0, 1, 0)), ((0, 0, -1), (0, 1, 0), (1, 0, 0))]
    V, N, F = [], [], []
    for k, (n, u, v) in enumerate(dirs):
        n, u, v = np.array(n, float), np.array(u, float), np.array(v, float)
        V += [(n + a * u + b * v) * s for a, b in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
        N += [n] * 4
        F += [[4 * k, 4 * k + 2, 4 * k + 1], [4 * k, 4 * k + 3, 4 * k + 2]]
    y, p = math.radians(yaw_deg), math.radians(pitch_deg)
    Ry = np.array([[math.cos(p), 0, math.sin(p)], [0, 1, 0], [-math.sin(p), 0, math.cos(p)]])
    Rz = np.array([[math.cos(y), -math.sin(y), 0], [math.sin(y), math.cos(y), 0], [0, 0, 1]])
    return transform((np.array(V), np.array(N), np.array(F)), Rz @ Ry, c)


def ellipsoid(center, radii, rings=14, segments=20, R=None):
    th = np.linspace(0, np.pi, rings + 1)[:, None]
    ph = np.linspace(0, 2 * np.pi, segments + 1)[None, :]
    unit = np.stack([np.sin(th) * np.cos(ph), np.sin(th) * np.sin(ph), np.cos(th) * np.ones_like(ph)], -1)
    radii = np.asarray(radii, float)
    P, N = unit * radii, normalize(unit / radii)
    idx = np.arange(P.shape[0] * P.shape[1]).reshape(P.shape[:2])
    a, b, c, d = idx[:-1, :-1].ravel(), idx[1:, :-1].ravel(), idx[1:, 1:].ravel(), idx[:-1, 1:].ravel()
    f = np.concatenate([np.stack([a, c, b], 1), np.stack([a, d, c], 1)])
    return transform((P.reshape(-1, 3), N.reshape(-1, 3), f), R, center)


def tube(points, radius, sides=8):
    """Tube along a polyline, closed with flat caps hidden by the surface it sits on."""
    pts = np.asarray(points, float)
    tangents = normalize(np.gradient(pts, axis=0))
    up = np.array([0.0, 0.0, 1.0])
    frames = []
    side = normalize(np.cross(tangents[0], up if abs(tangents[0] @ up) < 0.9 else np.array([0.0, 1.0, 0.0])))
    for t in tangents:
        side = normalize(side - t * (side @ t))
        frames.append((side, np.cross(t, side)))
    ang = np.linspace(0, 2 * np.pi, sides + 1)
    V = np.array([[p + radius * (math.cos(a) * s + math.sin(a) * u) for a in ang] for p, (s, u) in zip(pts, frames)])
    N = np.array([[math.cos(a) * s + math.sin(a) * u for a in ang] for s, u in frames])
    ni, nj = V.shape[:2]
    idx = np.arange(ni * nj).reshape(ni, nj)
    a, b, c, d = idx[:-1, :-1].ravel(), idx[1:, :-1].ravel(), idx[1:, 1:].ravel(), idx[:-1, 1:].ravel()
    f = np.concatenate([np.stack([a, b, c], 1), np.stack([a, c, d], 1)])
    # Orientation test on the first quad: the normal must oppose the math cross of a front face.
    probe = np.cross(V[1, 0] - V[0, 0], V[1, 1] - V[0, 0])
    if probe @ N[0, 0] > 0:
        f = f[:, ::-1]
    return V.reshape(-1, 3), N.reshape(-1, 3), f


def segment_distance(a, b, p0, p1):
    """Distance from points (a, b) to the segment p0-p1 and the closest point coordinates."""
    seg = np.asarray(p1, float) - np.asarray(p0, float)
    t = np.clip(((a - p0[0]) * seg[0] + (b - p0[1]) * seg[1]) / max(float(seg @ seg), 1e-12), 0, 1)
    qa, qb = p0[0] + t * seg[0], p0[1] + t * seg[1]
    return np.hypot(a - qa, b - qb), qa, qb


def closed_edges(poly):
    pts = [np.asarray(p, float) for p in poly]
    return list(zip(pts, pts[1:] + pts[:1]))


def plane_axes(plane):
    return {"xy": (0, 1), "xz": (0, 2), "yz": (1, 2)}[plane]


def polygon_field(poly, plane, pts):
    """Signed distance to a polygon in a design plane: negative inside."""
    a_i, b_i = plane_axes(plane)
    a, b = pts[..., a_i], pts[..., b_i]
    dist = np.full(a.shape, np.inf)
    for p0, p1 in closed_edges(poly):
        dist = np.minimum(dist, segment_distance(a, b, p0, p1)[0])
    inside = PolyPath(np.asarray(poly, float)).contains_points(np.stack([a.ravel(), b.ravel()], 1)).reshape(a.shape)
    return np.where(inside, -dist, dist)


def polyline_field(points, plane, pts):
    """Distance to an open polyline in a design plane."""
    a_i, b_i = plane_axes(plane)
    a, b = pts[..., a_i], pts[..., b_i]
    line = np.asarray(points, float)
    dist = np.full(a.shape, np.inf)
    for p0, p1 in zip(line[:-1], line[1:]):
        dist = np.minimum(dist, segment_distance(a, b, p0, p1)[0])
    return dist


def grid_soup(P, N, j_offset):
    """Triangles of a grid as arrays: positions (n, 3, 3), normals (n, 3, 3), section index per triangle (n)."""
    ni, nj = P.shape[:2]
    idx = np.arange(ni * nj).reshape(ni, nj)
    a, b = idx[:-1, :-1].ravel(), idx[1:, :-1].ravel()
    c, d = idx[1:, 1:].ravel(), idx[:-1, 1:].ravel()
    # Front faces: seen from outside, the order is (origin, +section, +station), as Unreal expects.
    faces = np.concatenate([np.stack([a, c, b], 1), np.stack([a, d, c], 1)])
    jj = (np.arange(nj - 1)[None, :] * np.ones((ni - 1, 1), dtype=int)).ravel() + j_offset
    v, n = P.reshape(-1, 3), N.reshape(-1, 3)
    return v[faces], n[faces], np.concatenate([jj, jj])


def clip_soup(T, Tn, J, M, field, material, cond, only=None):
    """Gives `material` to the part of the surface where field < 0 and cond holds, cutting triangles along field = 0.

    A material of -1 removes that part. `only` restricts the change to triangles of those materials.
    """
    centers = T.mean(axis=1)
    eligible = cond(centers[:, 0], J)
    if only is not None:
        eligible = eligible & np.isin(M, only)
    ids = np.nonzero(eligible)[0]
    if len(ids) == 0:
        return T, Tn, J, M
    f = field(T[ids])
    below = f < 0
    count = below.sum(axis=1)
    M = M.copy()
    M[ids[count == 3]] = material

    crossing = (count == 1) | (count == 2)
    if crossing.any():
        rows = ids[crossing]
        fc, bc, cc = f[crossing], below[crossing], count[crossing]
        # The lone vertex is the one on its own side of the curve.
        lone_inside = cc == 1
        lone = np.argmax(bc == lone_inside[:, None], axis=1)
        order = (lone[:, None] + np.arange(3)) % 3
        take = lambda arr: np.take_along_axis(arr, order[..., None] if arr.ndim == 3 else order, axis=1)
        tp, tn, ff = take(T[rows]), take(Tn[rows]), take(fc)
        t_ab = (ff[:, 0] / (ff[:, 0] - ff[:, 1]))[:, None]
        t_ac = (ff[:, 0] / (ff[:, 0] - ff[:, 2]))[:, None]
        pd, pe = tp[:, 0] + t_ab * (tp[:, 1] - tp[:, 0]), tp[:, 0] + t_ac * (tp[:, 2] - tp[:, 0])
        nd, ne = normalize(tn[:, 0] + t_ab * (tn[:, 1] - tn[:, 0])), normalize(tn[:, 0] + t_ac * (tn[:, 2] - tn[:, 0]))
        old = M[rows]
        m_lone = np.where(lone_inside, material, old)
        m_rest = np.where(lone_inside, old, material)
        new_t = np.concatenate([np.stack([tp[:, 0], pd, pe], 1), np.stack([pd, tp[:, 1], tp[:, 2]], 1), np.stack([pd, tp[:, 2], pe], 1)])
        new_n = np.concatenate([np.stack([tn[:, 0], nd, ne], 1), np.stack([nd, tn[:, 1], tn[:, 2]], 1), np.stack([nd, tn[:, 2], ne], 1)])
        new_j = np.concatenate([J[rows], J[rows], J[rows]])
        new_m = np.concatenate([m_lone, m_rest, m_rest])
        keep = np.ones(len(T), dtype=bool)
        keep[rows] = False
        T = np.concatenate([T[keep], new_t])
        Tn = np.concatenate([Tn[keep], new_n])
        J = np.concatenate([J[keep], new_j])
        M = np.concatenate([M[keep], new_m])
    if material < 0:
        keep = M >= 0
        T, Tn, J, M = T[keep], Tn[keep], J[keep], M[keep]
    return T, Tn, J, M


def add_faces_soup(mesh, name, verts, norms, faces):
    mesh.add(name, verts, norms, faces)
    flip = np.array([1.0, -1.0, 1.0])
    mesh.add(name, verts * flip, norms * flip, faces[:, ::-1])


def loft(design, c):
    L = design["Length"]
    quality = design.get("Quality", 1.0)
    n_st = max(40, int(design.get("Stations", 440) * quality))
    t = np.linspace(0, 1, n_st)
    # Stations clustered at the nose and the tail, where curvature is highest.
    x = -L / 2 + L * (0.75 * t + 0.25 * (0.5 - 0.5 * np.cos(np.pi * t)))
    x[0], x[-1] = -L / 2, L / 2

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
        ctrl = np.array([
            (0.0, zt),
            (0.62 * wr, zt - 0.32 * crown),
            (wr, zt - crown),
            (wb, zb + 0.004),
            (wm - 0.012, zb - 0.075),
            (wm, zs),
            (wm - 0.035, zbot + 0.11),
            (wm - 0.075, zbot + 0.012),
            (0.0, zbot),
        ])
        P[i, :, 1] = PchipInterpolator(knots, ctrl[:, 0])(params)
        P[i, :, 2] = PchipInterpolator(knots, ctrl[:, 1])(params)
        P[i, :, 0] = xi
    return P, np.cumsum([0] + counts)


def build_body(design):
    """Lofted body shell plus every attached detail. Returns (Mesh, arches)."""
    keys = ("Top", "Belt", "HalfWidth", "RoofHalfWidth", "BeltHalfWidth", "Bottom", "SideZ")
    c = {k: curve(design["Curves"][k]) for k in keys}
    P, seg = loft(design, c)
    n_st, n_sec = P.shape[:2]
    j_cabin0, j_side0, j_side1 = seg[2], seg[4], seg[6]
    jv = np.arange(n_sec)[None, :] * np.ones((n_st, 1), dtype=int)

    # --- Sculpting on the flanks ------------------------------------------------------------------
    for dent in design.get("Dents", []):
        d = np.exp(-(((P[..., 0] - dent["X"]) / dent["SX"]) ** 2) - (((P[..., 2] - dent["Z"]) / dent["SZ"]) ** 2))
        P[..., 1] -= dent["Depth"] * d * np.clip(P[..., 1] / max(float(c["HalfWidth"](dent["X"])), 0.1), 0, 1) ** 3

    # --- Wheel arches: flare, then snap the rim of the opening onto an exact circle ------------------
    arches = []
    for ax, radius in ((design["FrontAxleX"], design["FrontWheelRadius"]), (design["RearAxleX"], design["RearWheelRadius"])):
        R = radius + design.get("ArchGap", 0.05)
        arches.append((ax, radius, R))
        d = np.hypot(P[..., 0] - ax, P[..., 2] - radius)
        side = jv >= j_side0 - 8
        P[..., 1] += design.get("ArchFlare", 0.022) * np.exp(-(((d - R) / 0.10) ** 2)) * (d > R - 0.02) * side

    regions = dict(design["Regions"])
    half_len = design["Length"] / 2
    (front_x, _front_r, front_R), (rear_x, _rear_r, rear_R) = arches
    skirt_top = float(c["Bottom"](0.0)) + design.get("SkirtHeight", 0.075)
    regions["Skirt"] = [[[rear_x + rear_R + 0.015, 0.0], [rear_x + rear_R + 0.015, skirt_top], [front_x - front_R - 0.015, skirt_top], [front_x - front_R - 0.015, 0.0]]]
    idx = {m: k for k, m in enumerate(MATERIALS)}

    def region_filter(name, plane, x, j):
        if plane == "yz":
            # Lamps and intakes live on the nose or the tail only, never on the flanks.
            return (x > 0.7 * half_len) if name in ("Grille", "LowerIntake") else (x < -0.7 * half_len)
        if name == "SideWindow":
            return j < j_side0 + 2
        if name in ("SideIntake", "Skirt"):
            return j >= j_side0 - 4
        if name == "HeadLight":
            return (x > 0) & (j < j_side0)
        return j < j_side0

    # --- Real depth: a smooth recess computed from the exact distance to each outline -----------------
    depth = np.zeros((n_st, n_sec))
    falloff = design.get("RecessFalloff", 0.012)
    for name, _material, plane, recess in REGIONS:
        if recess <= 0:
            continue
        where = region_filter(name, plane, P[..., 0], jv)
        for poly in regions.get(name, []):
            f = polygon_field(poly, plane, P)
            depth = np.maximum(depth, recess * np.clip(-f / falloff, 0, 1) * where)
    gap = design.get("GapWidth", 0.008)
    for line in design.get("GapLines", []):
        cond = (jv >= j_cabin0) if line["Plane"] == "xz" else (jv < j_side0)
        f = polyline_field(line["Points"], line["Plane"], P) - gap
        depth = np.maximum(depth, 0.007 * np.clip(-f / gap, 0, 1) * cond)
    depth = gaussian_filter(depth, sigma=design.get("RecessSmoothing", 0.9))
    P = P - grid_normals(P) * depth[..., None]

    # --- Triangle soup, with hard creases along the character lines -----------------------------------
    creases = sorted({0, n_sec - 1, int(j_side0), int(j_side1)} | {int(seg[k]) for k in design.get("ExtraCreaseSegments", [])})
    soups = []
    for j0, j1 in zip(creases[:-1], creases[1:]):
        strip = P[:, j0:j1 + 1]
        N = grid_normals(strip)
        if j0 == 0:
            # Exactly on the mirror plane: no sideways normal, otherwise a seam shows along the centerline.
            N[:, 0, 1] = 0.0
            N = normalize(N)
        if j1 == n_sec - 1:
            N[:, -1, 1] = 0.0
            N = normalize(N)
        soups.append(grid_soup(strip, N, j0))
    T = np.concatenate([q[0] for q in soups])
    Tn = np.concatenate([q[1] for q in soups])
    J = np.concatenate([q[2] for q in soups])
    M = np.full(len(T), idx["Paint"], dtype=np.int32)
    M[J >= j_side1] = idx["Trim"]  # The floor is dark.

    # --- Outlines: triangles are cut exactly along the curves, at any angle ----------------------------
    def apply(field, material, cond, only=None):
        nonlocal T, Tn, J, M
        T, Tn, J, M = clip_soup(T, Tn, J, M, field, material, cond, only)

    paint = (idx["Paint"],)
    band = design.get("WindowTrimWidth", 0.018)
    for poly in regions.get("SideWindow", []):
        field = lambda pts, poly=poly: np.abs(polygon_field(poly, "xz", pts)) - band
        apply(field, idx[design.get("WindowTrim", "Trim")], lambda x, j: j < j_side0 + 2, paint)

    for line in design.get("GapLines", []):
        cond = (lambda x, j: j >= j_cabin0) if line["Plane"] == "xz" else (lambda x, j: j < j_side0)
        field = lambda pts, line=line: polyline_field(line["Points"], line["Plane"], pts) - gap
        apply(field, idx["Gap"], cond, paint)

    for name, material, plane, _recess in REGIONS:
        cond = lambda x, j, name=name, plane=plane: region_filter(name, plane, x, j)
        for poly in regions.get(name, []):
            field = lambda pts, poly=poly, plane=plane: polygon_field(poly, plane, pts)
            apply(field, idx[material], cond)

    # Wheel openings: a material of -1 removes the triangles.
    for ax, radius, R in arches:
        field = lambda pts, ax=ax, radius=radius, R=R: np.hypot(pts[..., 0] - ax, pts[..., 2] - radius) - R
        apply(field, -1, lambda x, j: j >= j_side0 - 8)

    mesh = Mesh()
    for m, name in enumerate(MATERIALS):
        sel = M == m
        if sel.any():
            count = int(sel.sum())
            faces = np.arange(count * 3).reshape(-1, 3)
            add_faces_soup(mesh, name, T[sel].reshape(-1, 3), Tn[sel].reshape(-1, 3), faces)

    build_details(mesh, design, c, arches, P, seg)
    return mesh, arches


def surface_points(P, plane, samples, where, lift):
    """Projects 2D plane coordinates onto the body surface, slightly outside it. Returns 3D points."""
    a_i, b_i = plane_axes(plane)
    candidates = P[where]
    _, nearest = cKDTree(candidates[:, [a_i, b_i]]).query(samples)
    out = candidates[nearest].copy()
    out[:, a_i], out[:, b_i] = samples[:, 0], samples[:, 1]
    # The nearest grid vertex jumps from sample to sample: smooth the third coordinate so a tube runs straight.
    c_i = 3 - a_i - b_i
    if len(out) > 8:
        out[:, c_i] = gaussian_filter1d(out[:, c_i], sigma=4.0, mode="nearest")
    outward = {"xy": np.array([0.0, 0.0, 1.0]), "yz": np.array([-1.0, 0.0, 0.0]), "xz": np.array([0.0, 1.0, 0.0])}[plane]
    return out + outward * lift


def resample(points, spacing=0.012):
    pts = np.asarray(points, float)
    chord = np.r_[0, np.cumsum(np.linalg.norm(np.diff(pts, axis=0), axis=1))]
    t = np.linspace(0, chord[-1], max(4, int(chord[-1] / spacing)))
    if len(pts) > 2:
        return np.stack([PchipInterpolator(chord, pts[:, k])(t) for k in range(pts.shape[1])], 1)
    return np.stack([np.interp(t, chord, pts[:, k]) for k in range(pts.shape[1])], 1)


def build_details(mesh, design, c, arches, P, seg):
    quality = design.get("Quality", 1.0)
    jv = np.arange(P.shape[1])[None, :] * np.ones((P.shape[0], 1), dtype=int)

    # Before anything else is painted: only the shell of the body may get a lining.
    if design.get("Cabin") and quality >= 1:
        build_headliner(mesh, design, c)

    # Wheel wells: dark drum behind each opening.
    for ax, radius, R in arches:
        outer = float(c["HalfWidth"](ax)) - 0.015
        v, n, f = lathe([(outer, R + 0.01), (outer - 0.42, R + 0.01), (outer - 0.42, 0.02)], 56)
        add_mirrored(mesh, "Gap", (v + np.array([ax, 0.0, radius]), -n, f[:, ::-1]))

    # LED signatures: real tubes that sit on the lamps.
    upper = (P[..., 0] > 0) & (jv < seg[4])
    rear = (P[..., 0] < -0.7 * design["Length"] / 2) & (jv > seg[1])
    for line in design.get("DRL", []):
        pts = surface_points(P, "xy", resample(line), upper, 0.004)
        add_mirrored(mesh, "DRL", tube(pts, design.get("DRLRadius", 0.0065)))
    for line in design.get("TailLED", []):
        pts = surface_points(P, "yz", resample(line), rear, 0.004)
        add_mirrored(mesh, "TailLED", tube(pts, design.get("TailLEDRadius", 0.0055)))
    for p in design.get("Projectors", []):
        pts = surface_points(P, "xy", np.array([[p["X"], p["Y"]]]), upper, -0.012)
        add_mirrored(mesh, "Metal", ellipsoid(pts[0], (0.032, 0.032, 0.026), 10, 14))
        add_mirrored(mesh, "DRL", ellipsoid(pts[0] + [0, 0, 0.014], (0.014, 0.014, 0.012), 6, 10))

    # Calipers on the body: they do not spin with the wheel.
    for ax, radius, name in ((design["FrontAxleX"], design["FrontWheelRadius"], "Front"), (design["RearAxleX"], design["RearWheelRadius"], "Rear")):
        y = design[name + "TrackHalf"] - design[name + "WheelWidth"] * 0.18
        r = radius * 0.5
        add_mirrored(mesh, "Caliper", box((ax - r * 0.86, y, radius + r * 0.1), (0.075, 0.05, 0.16), pitch_deg=8))

    # Door handles flush with the flank.
    for h in design.get("Handles", []):
        y = float(c["HalfWidth"](h["X"])) - 0.006
        add_mirrored(mesh, design.get("HandleMaterial", "Trim"), ellipsoid((h["X"], y, h["Z"]), (0.07, 0.012, 0.012), 8, 12))

    # Mirrors: stalk and aerodynamic shell with a glass face.
    m = design.get("Mirror")
    if m:
        add_mirrored(mesh, "Paint", ellipsoid((m["X"], m["Y"], m["Z"]), (0.06, 0.085, 0.042)))
        add_mirrored(mesh, "Trim", box((m["X"] + 0.02, m["Y"] - 0.08, m["Z"] - 0.02), (0.04, 0.11, 0.018)))
        add_mirrored(mesh, "Metal", ellipsoid((m["X"] - 0.042, m["Y"], m["Z"]), (0.018, 0.07, 0.034), 8, 12))

    # Exhausts: axis along X.
    for e in design.get("Exhausts", []):
        v, n, f = lathe([(-0.22, 0.042), (0.0, 0.047), (0.006, 0.042), (-0.05, 0.034), (-0.05, 0.0)], 28)
        vv = np.stack([-v[:, 1] + e["X"], v[:, 0] + e["Y"], v[:, 2] + e["Z"]], 1)
        nn = np.stack([-n[:, 1], n[:, 0], n[:, 2]], 1)
        add_mirrored(mesh, "Metal", (vv, nn, f[:, ::-1]))

    split = design.get("Splitter")
    if split:
        add_mirrored(mesh, "Trim", box((split["X"], split["HalfWidth"] / 2, split["Z"]), (split["Depth"], split["HalfWidth"], 0.022)))
    wing = design.get("Wing")
    if wing:
        add_mirrored(mesh, "Trim", box((wing["X"], wing["HalfSpan"] / 2, wing["Z"]), (wing["Chord"], wing["HalfSpan"], 0.02), pitch_deg=wing.get("Pitch", -8)))
        add_mirrored(mesh, "Trim", box((wing["X"] + 0.04, wing["PostY"], wing["Z"] - wing["PostH"] / 2), (0.1, 0.018, wing["PostH"])))
        add_mirrored(mesh, "Trim", box((wing["X"], wing["HalfSpan"], wing["Z"] - 0.03), (wing["Chord"] + 0.05, 0.012, 0.1)))

    for w in design.get("Wipers", []) if quality >= 1 else []:
        add_mirrored(mesh, "Trim", tube(resample(np.asarray(w, float), 0.03), 0.005, 6))

    cab = design.get("Cabin")
    if cab and quality >= 1:
        build_interior(mesh, cab, c["Top"])
        close_cabin(mesh, design, c, cab)


def build_headliner(mesh, design, c):
    """From the cockpit the roof and pillars would show the back of the paint: line them with the interior material."""
    v, n, f = mesh.arrays()["Paint"]
    front = design["Regions"]["Windshield"][0][0][0]  # Lower edge of the windshield.
    rear = min(point[0] for poly in design["Regions"]["RearGlass"] for point in poly)
    center = v[f].mean(axis=1)
    keep = (center[:, 0] > rear) & (center[:, 0] < front) & (center[:, 2] > c["Belt"](center[:, 0]) - 0.02)
    corners = f[keep][:, ::-1].reshape(-1)
    faces = np.arange(len(corners)).reshape(-1, 3)
    mesh.add("Interior", v[corners] - n[corners] * 0.015, -n[corners], faces)


def close_cabin(mesh, design, c, cab):
    """The shell is one-sided: from inside it is invisible, so the cabin needs its own walls up to the glass."""
    z = cab["FloorZ"]
    base_x = design["Regions"]["Windshield"][0][0][0]  # Lower edge of the windshield.
    dash = cab["DashX"]
    sx = cab["SeatX"]
    rear = sx - 0.66
    # Top of the dash: from the driver's knees to the base of the windshield, just under the beltline.
    top = float(c["Belt"](base_x)) - 0.012
    half = float(c["BeltHalfWidth"](base_x)) - 0.03
    length = base_x + 0.04 - (dash - 0.18)
    add_mirrored(mesh, "Interior", box((dash - 0.18 + length / 2, half / 2, top - 0.02), (length, half, 0.04)))
    add_mirrored(mesh, "Trim", box((dash - 0.17, half / 2, (z + top) / 2), (0.04, half, top - z)))
    # Door cards from the floor to the beltline, sloping with it.
    for x0, x1 in ((rear, sx + 0.3), (sx + 0.3, base_x)):
        mid = (x0 + x1) / 2
        belt = float(c["Belt"](mid)) - 0.012
        y = float(c["BeltHalfWidth"](mid)) - 0.035
        add_mirrored(mesh, "Interior", box((mid, y, (z + belt) / 2), (x1 - x0, 0.03, belt - z)))
    # Parcel shelf and back wall up to the rear glass.
    belt = float(c["Belt"](rear)) - 0.012
    half = float(c["BeltHalfWidth"](rear)) - 0.04
    add_mirrored(mesh, "Interior", box((rear, half / 2, (z + belt) / 2), (0.03, half, belt - z)))


def build_interior(mesh, cab, roof):
    """Seats, dash, console, door cards and steering wheel. Seen through the glass and from the cockpit camera."""
    z, sx, sy, dash = cab["FloorZ"], cab["SeatX"], cab["SeatY"], cab["DashX"]
    pitch = math.radians(-16)
    Rback = np.array([[math.cos(pitch), 0, math.sin(pitch)], [0, 1, 0], [-math.sin(pitch), 0, math.cos(pitch)]])
    # Sport seats: cushion, bolsters, back and integrated headrest.
    add_mirrored(mesh, "Interior", ellipsoid((sx, sy, z + 0.2), (0.27, 0.235, 0.075)))
    # Low roofs get lower seats: the headrest always stays clear of the roof line behind it.
    head_top = float(roof(sx - 0.5)) - 0.05
    head_z = min(z + 0.86, head_top - 0.115)
    back_z = min(z + 0.5, head_z - 0.36)
    add_mirrored(mesh, "Interior", ellipsoid((sx - 0.32, sy, back_z), (0.085, 0.235, 0.33), R=Rback))
    add_mirrored(mesh, "Interior", ellipsoid((sx - 0.42, sy, head_z), (0.07, 0.125, 0.115), R=Rback))
    for side in (-1, 1):
        add_mirrored(mesh, "Trim", ellipsoid((sx - 0.3, sy + side * 0.2, z + 0.48), (0.07, 0.04, 0.24), 8, 12, R=Rback))
        add_mirrored(mesh, "Trim", ellipsoid((sx + 0.04, sy + side * 0.2, z + 0.22), (0.21, 0.04, 0.06), 8, 12))
    # Dash: soft top with an instrument binnacle and a center screen.
    add_mirrored(mesh, "Interior", ellipsoid((dash, 0.36, z + 0.5), (0.33, 0.4, 0.11)))
    add_mirrored(mesh, "Trim", box((dash + 0.02, 0.28, z + 0.34), (0.42, 0.56, 0.22), pitch_deg=10))
    mesh.add("Trim", *box((dash - 0.24, -sy, z + 0.565), (0.12, 0.3, 0.075), pitch_deg=-12))
    mesh.add("Screen", *box((dash - 0.3, -sy, z + 0.565), (0.012, 0.25, 0.058), pitch_deg=-12))
    mesh.add("Screen", *box((dash - 0.23, 0.0, z + 0.5), (0.012, 0.2, 0.13), pitch_deg=-20))
    # Tub, center console, door cards and rear bulkhead.
    add_mirrored(mesh, "Interior", box((cab["TubX"], 0.36, z + 0.04), (cab["TubLength"], 0.72, 0.08)))
    mesh.add("Trim", *box((sx + 0.1, 0.0, z + 0.2), (0.95, 0.2, 0.26)))
    mesh.add("Metal", *ellipsoid((sx + 0.3, 0.0, z + 0.36), (0.035, 0.03, 0.05), 8, 10))
    add_mirrored(mesh, "Interior", box((sx + 0.05, cab.get("DoorY", 0.72), z + 0.34), (1.2, 0.04, 0.5)))
    add_mirrored(mesh, "Interior", box((sx - 0.62, 0.36, z + 0.32), (0.08, 0.72, 0.58)))
    # Steering wheel, axis tilted towards the driver.
    tilt = math.radians(24)
    axis = np.array([-math.cos(tilt), 0.0, math.sin(tilt)])
    across = np.array([1.0, 0.0, 0.0]) * math.sin(tilt) + np.array([0.0, 0.0, 1.0]) * math.cos(tilt)
    R = np.stack([across, axis, np.cross(across, axis)], 1)  # Lathe axis (+Y) goes to the wheel axis.
    center = (dash - 0.42, -sy, z + 0.46)
    rim = [(-0.016, 0.165), (0.0, 0.181), (0.016, 0.165), (0.0, 0.149), (-0.016, 0.165)]
    mesh.add("Trim", *transform(lathe(rim, 36), R, center))
    mesh.add("Trim", *transform(lathe([(0.03, 0.0), (0.03, 0.05), (-0.02, 0.06), (-0.02, 0.0)], 18), R, center))
    for ang in (0.0, 2.3, -2.3):
        spoke = box((0.0, 0.0, 0.1), (0.02, 0.016, 0.1))
        ca, sa = math.cos(ang), math.sin(ang)
        Rs = np.array([[ca, 0, sa], [0, 1, 0], [-sa, 0, ca]])
        mesh.add("Trim", *transform(transform(spoke, Rs), R, center))


def build_wheel(radius, width, spokes=10, rim_style="split", segments=72):
    """One wheel: origin at the hub, +Y is the outer face. Separate mesh so the game can spin it."""
    mesh = Mesh()
    rim_r = radius * 0.74
    w = width / 2
    tire = [(-w * 0.82, rim_r * 0.99), (-w, rim_r * 1.06), (-w, radius * 0.93), (-w * 0.85, radius * 0.985), (0.0, radius), (w * 0.85, radius * 0.985), (w, radius * 0.93), (w, rim_r * 1.06), (w * 0.82, rim_r * 0.99)]
    mesh.add("Tire", *lathe(tire, segments))
    barrel = [(w * 0.9, rim_r * 1.0), (w * 0.84, rim_r * 0.95), (w * 0.2, rim_r * 0.9), (-w * 0.8, rim_r * 0.88), (-w * 0.95, rim_r * 0.97)]
    v, n, f = lathe(barrel, segments)
    mesh.add("Rim", v, -n, f[:, ::-1])
    # Polished lip catches the light.
    mesh.add("Metal", *lathe([(w * 0.84, rim_r * 0.95), (w * 0.93, rim_r * 0.97), (w * 0.92, rim_r * 1.02)], segments))
    # Brake disc behind the spokes.
    mesh.add("Brake", *lathe([(-w * 0.2, 0.0), (-w * 0.2, rim_r * 0.84), (w * 0.05, rim_r * 0.84), (w * 0.05, 0.0)], max(24, segments // 2)))
    hub = [(w * 0.66, 0.0), (w * 0.66, rim_r * 0.09), (w * 0.62, rim_r * 0.2), (w * 0.5, rim_r * 0.24), (w * 0.1, rim_r * 0.2)]
    mesh.add("Rim", *lathe(hub, 32))
    # Lug nuts.
    if segments >= 40:
        for k in range(5):
            a = 2 * math.pi * k / 5 + 0.3
            nut = lathe([(w * 0.56, 0.0), (w * 0.7, 0.0), (w * 0.7, 0.012), (w * 0.56, 0.014)], 8)
            mesh.add("Metal", *transform(nut, None, (rim_r * 0.17 * math.cos(a), 0.0, rim_r * 0.17 * math.sin(a))))
    pairs = spokes // 2 if rim_style == "split" else spokes
    for k in range(pairs):
        a0 = 2 * math.pi * k / pairs
        for off in ((-0.075, 0.075) if rim_style == "split" else (0.0,)):
            inner, outer = rim_r * 0.2, rim_r * 0.97
            a, b = a0 + off * 0.35, a0 + off * 1.15
            p0 = np.array([inner * math.cos(a), w * 0.58, inner * math.sin(a)])
            p1 = np.array([outer * math.cos(b), w * 0.8, outer * math.sin(b)])
            u = normalize(p1 - p0)
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
        wheel = build_wheel(radius, design[name + "WheelWidth"], design.get("Spokes", 10), design.get("RimStyle", "split"), segments)
        for mat, (v, n, f) in wheel.arrays().items():
            add_mirrored(mesh, mat, (v + np.array([ax, design[name + "TrackHalf"], radius], dtype=np.float32), n, f))


# --- Preview ----------------------------------------------------------------------------------------

PREVIEW_COLORS = {
    "Paint": (205, 40, 28), "Glass": (28, 36, 44), "Trim": (24, 24, 26), "Gap": (6, 6, 6), "HeadLight": (150, 160, 175), "DRL": (255, 255, 255),
    "TailLight": (90, 8, 10), "TailLED": (255, 40, 30), "Metal": (190, 190, 195), "Interior": (40, 34, 30), "Screen": (70, 120, 170),
    "Tire": (20, 20, 20), "Rim": (175, 178, 185), "Brake": (90, 90, 95), "Caliper": (200, 30, 25),
}


def render(mesh, eye, target, size=(800, 460), fov=26.0):
    """Small rasterizer with a sky gradient reflection; good enough to judge shape."""
    W, H = size
    eye, target = np.asarray(eye, float), np.asarray(target, float)
    f = normalize(target - eye)
    r = normalize(np.cross(f, [0, 0, 1.0]))
    u = np.cross(r, f)
    focal = 0.5 * H / math.tan(math.radians(fov) / 2)
    grad = np.linspace(1, 0, H)[:, None, None]
    color_buf = np.tile(grad * np.array([120, 130, 145]) + (1 - grad) * np.array([62, 60, 58]), (1, W, 1)).astype(np.float32)
    z_buf = np.full((H, W), 1e9)
    light = normalize(np.array([0.45, 0.35, 0.85]))
    for name, (V, Nv, F) in mesh.arrays().items():
        base = np.array(PREVIEW_COLORS[name], dtype=np.float32)
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
        if name in ("DRL", "TailLED", "Screen"):
            shade = np.tile(base, (len(shade), 1))
        min_x = np.floor(sx.min(1)).astype(int).clip(0, W - 1)
        max_x = np.ceil(sx.max(1)).astype(int).clip(0, W - 1)
        min_y = np.floor(sy.min(1)).astype(int).clip(0, H - 1)
        max_y = np.ceil(sy.max(1)).astype(int).clip(0, H - 1)
        small = front & ((max_x - min_x) <= 3) & ((max_y - min_y) <= 3)
        if small.any():
            order = np.nonzero(small)[0]
            depth_t = cz[order].mean(1)
            sort = np.argsort(-depth_t)
            order, depth_t = order[sort], depth_t[sort]
            for ox, oy in ((0, 0), (1, 0), (0, 1), (1, 1), (2, 1), (1, 2)):
                px = np.clip(min_x[order] + ox, 0, W - 1)
                py = np.clip(min_y[order] + oy, 0, H - 1)
                ok = (px <= max_x[order]) & (py <= max_y[order]) & (depth_t < z_buf[py, px])
                color_buf[py[ok], px[ok]] = shade[order[ok]]
                np.minimum.at(z_buf, (py[ok], px[ok]), depth_t[ok])
        for t in np.nonzero(front & ~small)[0]:
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
            if mask.any():
                depth = w0 * cz[t, 0] + w1 * cz[t, 1] + w2 * cz[t, 2]
                region = z_buf[y0:y1 + 1, x0:x1 + 1]
                mask &= depth < region
                region[mask] = depth[mask]
                color_buf[y0:y1 + 1, x0:x1 + 1][mask] = shade[t]
    return Image.fromarray(np.clip(color_buf, 0, 255).astype(np.uint8))


def preview_sheet(mesh, path, design):
    L = design["Length"]
    views = [((L * 1.05, -L * 1.25, 1.25), (0.05, 0, 0.5)), ((-L * 1.1, -L * 1.2, 1.35), (0, 0, 0.5))]
    tiles = [render(mesh, eye, tgt) for eye, tgt in views]
    sheet = Image.new("RGB", (1600, 460))
    for k, tile in enumerate(tiles):
        sheet.paste(tile, (k * 800, 0))
    sheet.save(path)


# --- Export -----------------------------------------------------------------------------------------

def write_glb(mesh, path, include=None, exclude=()):
    """Single-mesh glTF: one primitive per material, so Unreal gets one static mesh with named slots."""
    arrays = {k: v for k, v in mesh.arrays().items() if (include is None or k in include) and k not in exclude}
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
        # UVs in meters, so tiling materials (flakes, grain) have a real-world scale on any car.
        uv = np.stack([V[:, 0] + 0.6 * V[:, 1], V[:, 2] + 0.6 * V[:, 1]], 1).astype(np.float32)
        indices = F.astype(np.uint32)
        vp = add_view(gltf_v.tobytes(), 34962)
        accessors.append({"bufferView": vp, "componentType": 5126, "count": len(gltf_v), "type": "VEC3", "min": gltf_v.min(0).tolist(), "max": gltf_v.max(0).tolist()})
        pos = len(accessors) - 1
        accessors.append({"bufferView": add_view(gltf_n.tobytes(), 34962), "componentType": 5126, "count": len(gltf_n), "type": "VEC3"})
        nrm = len(accessors) - 1
        accessors.append({"bufferView": add_view(uv.tobytes(), 34962), "componentType": 5126, "count": len(uv), "type": "VEC2"})
        tex = len(accessors) - 1
        accessors.append({"bufferView": add_view(indices.tobytes(), 34963), "componentType": 5125, "count": int(indices.size), "type": "SCALAR"})
        idx = len(accessors) - 1
        materials.append({"name": name, "pbrMetallicRoughness": {"baseColorFactor": [c / 255 for c in PREVIEW_COLORS[name]] + [1.0]}})
        primitives.append({"attributes": {"POSITION": pos, "NORMAL": nrm, "TEXCOORD_0": tex}, "indices": idx, "material": k})

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
        # Nanite cannot draw translucent materials: an opaque Nanite body plus a separate glass mesh.
        write_glb(mesh, out / f"SM_{car_id}_Body.glb", exclude=("Glass",))
        write_glb(mesh, out / f"SM_{car_id}_Glass.glb", include=("Glass",))
        # Light variant for traffic: coarser body, wheels baked in (traffic wheels do not need to spin up close).
        low_design = dict(design, Quality=0.3)
        low, _ = build_body(low_design)
        place_wheels(low, low_design, segments=20)
        write_glb(low, out / f"SM_{car_id}_Traffic.glb")
        write_glb(build_wheel(design["RearWheelRadius"], design["RearWheelWidth"], design.get("Spokes", 10), design.get("RimStyle", "split")), out / f"SM_{car_id}_Wheel.glb")
        print("written", car_id, "body:", mesh.triangle_count(), "traffic:", low.triangle_count())


if __name__ == "__main__":
    main()
