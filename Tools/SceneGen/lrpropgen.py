"""Builds the roadside objects of the world from code: trees, bushes, rocks, sign gantries and overpasses.

    python Tools/SceneGen/lrpropgen.py --out Saved/PropGen --textures Saved/SceneGen/Textures

Writes one .glb per object (meters, X along the road, Y to the right, Z up) and the textures they use:
T_Leaves_D (leaf clusters with alpha) and T_Signs_D (the panels of the gantry, fictional places only).
Tools/Editor/import_generated_props.py imports the meshes and assigns the materials by slot name.
"""
import argparse
import json
import math
import pathlib
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "CarGen"))
from lrcargen import box, lathe, normalize, transform, tube  # noqa: E402  Shared primitives, same axes and winding.

# Material slots. Each one becomes a material slot of the static mesh; the import script maps them.
SLOTS = ["Bark", "Leaf", "Rock", "Concrete", "Metal", "Sign", "SignBack", "Earth"]


class Mesh:
    """Triangles grouped by material slot, with optional UVs (meters when omitted)."""

    def __init__(self):
        self.parts = {name: [] for name in SLOTS}

    def add(self, slot, part, uv=None):
        v, n, f = part
        v = np.asarray(v, np.float32)
        if uv is None:
            # Planar mapping in meters: good enough for concrete and earth, which tile.
            uv = np.stack([v[:, 0] + v[:, 1], v[:, 2] + 0.5 * v[:, 1]], 1)
        self.parts[slot].append((v, np.asarray(n, np.float32), np.asarray(f, np.uint32), np.asarray(uv, np.float32)))

    def arrays(self):
        result = {}
        for slot, parts in self.parts.items():
            if not parts:
                continue
            offset, vs, ns, fs, uvs = 0, [], [], [], []
            for v, n, f, uv in parts:
                vs.append(v)
                ns.append(n)
                fs.append(f + offset)
                uvs.append(uv)
                offset += len(v)
            result[slot] = (np.concatenate(vs), np.concatenate(ns), np.concatenate(fs), np.concatenate(uvs))
        return result

    def triangle_count(self):
        return sum(len(a[2]) for a in self.arrays().values())


def quad(center, right, up, normal, size, uv_rect):
    """A flat card: center, its in-plane axes, the normal to shade it with, (width, height) and its rectangle in the atlas."""
    w, h = size[0] / 2.0, size[1] / 2.0
    corners = np.array([center - right * w - up * h, center + right * w - up * h, center + right * w + up * h, center - right * w + up * h])
    u0, v0, u1, v1 = uv_rect
    uv = np.array([[u0, v1], [u1, v1], [u1, v0], [u0, v0]])
    normals = np.repeat(normal[None, :], 4, 0)
    return (corners, normals, np.array([[0, 1, 2], [0, 2, 3]])), uv


def orient(v, f, outward):
    """Winding of the shared primitives (see lrcargen.box): cross(b - a, c - a) points against the visible side."""
    tri = v[f]
    cross = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    wrong = (cross * outward).sum(1) > 0
    f = f.copy()
    f[wrong] = f[wrong][:, ::-1]
    return f


def write_glb(mesh, path):
    """Single-mesh glTF with one primitive per slot. Unreal maps glTF (x, y, z) to (x, z, y), so Y and Z are swapped here."""
    blob = bytearray()
    views, accessors, primitives, materials = [], [], [], []

    def add_view(data, target):
        offset = len(blob)
        blob.extend(data)
        while len(blob) % 4:
            blob.append(0)
        views.append({"buffer": 0, "byteOffset": offset, "byteLength": len(data), "target": target})
        return len(views) - 1

    for k, (slot, (v, n, f, uv)) in enumerate(mesh.arrays().items()):
        gv = np.stack([v[:, 0], v[:, 2], v[:, 1]], 1).astype(np.float32)
        gn = np.stack([n[:, 0], n[:, 2], n[:, 1]], 1).astype(np.float32)
        accessors.append({"bufferView": add_view(gv.tobytes(), 34962), "componentType": 5126, "count": len(gv), "type": "VEC3",
                          "min": gv.min(0).tolist(), "max": gv.max(0).tolist()})
        accessors.append({"bufferView": add_view(gn.tobytes(), 34962), "componentType": 5126, "count": len(gn), "type": "VEC3"})
        accessors.append({"bufferView": add_view(uv.astype(np.float32).tobytes(), 34962), "componentType": 5126, "count": len(uv), "type": "VEC2"})
        accessors.append({"bufferView": add_view(f.astype(np.uint32).tobytes(), 34963), "componentType": 5125, "count": int(f.size), "type": "SCALAR"})
        # A distinct color per slot: the importer merges materials that look the same, and the slots with them.
        tint = [((SLOTS.index(slot) * 37) % 255) / 255.0, ((SLOTS.index(slot) * 91) % 255) / 255.0, 0.5, 1.0]
        materials.append({"name": slot, "pbrMetallicRoughness": {"baseColorFactor": tint}})
        base = 4 * k
        primitives.append({"attributes": {"POSITION": base, "NORMAL": base + 1, "TEXCOORD_0": base + 2}, "indices": base + 3, "material": k})

    doc = {"asset": {"version": "2.0", "generator": "lrpropgen"}, "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0}],
           "meshes": [{"primitives": primitives}], "materials": materials, "accessors": accessors, "bufferViews": views,
           "buffers": [{"byteLength": len(blob)}]}
    text = json.dumps(doc).encode("utf-8")
    text += b" " * ((4 - len(text) % 4) % 4)
    with open(path, "wb") as out:
        out.write(struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(text) + 8 + len(blob)))
        out.write(struct.pack("<II", len(text), 0x4E4F534A) + text)
        out.write(struct.pack("<II", len(blob), 0x004E4942) + bytes(blob))


# --- Textures --------------------------------------------------------------------------------------


def leaf_atlas(path, size=1024, seed=5):
    """Four clusters of small leaves on twigs, one per quadrant, with alpha. The material tints them per instance."""
    rng = np.random.default_rng(seed)
    image = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    half = size // 2
    for qy in range(2):
        for qx in range(2):
            cx, cy = qx * half + half / 2, qy * half + half / 2
            # Twigs first, leaves over them.
            for _ in range(7):
                angle = rng.uniform(0, 2 * math.pi)
                length = rng.uniform(0.25, 0.42) * half
                draw.line([(cx, cy), (cx + math.cos(angle) * length, cy + math.sin(angle) * length)], fill=(70, 58, 45, 255), width=4)
            for _ in range(420):
                r = half * 0.42 * math.sqrt(rng.uniform(0, 1))
                a = rng.uniform(0, 2 * math.pi)
                x, y = cx + math.cos(a) * r, cy + math.sin(a) * r * 0.85
                length, width = rng.uniform(16, 28), rng.uniform(8, 13)
                rot = rng.uniform(0, math.pi)
                shade = rng.uniform(0.7, 1.15)
                green = (int(min(255, 150 * shade)), int(min(255, 165 * shade)), int(min(255, 110 * shade)), 255)
                points = []
                for t in np.linspace(0, 2 * math.pi, 10, endpoint=False):
                    # Lobed outline, like an oak leaf.
                    radius = 1.0 + 0.18 * math.sin(4 * t)
                    px, py = math.cos(t) * length / 2 * radius, math.sin(t) * width / 2 * radius
                    points.append((x + px * math.cos(rot) - py * math.sin(rot), y + px * math.sin(rot) + py * math.cos(rot)))
                draw.polygon(points, fill=green)
    # Light grayscale variation; the material multiplies by the tint, so the texture stays close to neutral.
    image.save(path)


def sign_atlas(path, fonts):
    """Two green guide signs side by side, with fictional places. 2048 x 1024, each sign is a 1024 x 1024 square."""
    green = (0, 107, 63)
    white = (240, 242, 238)
    image = Image.new("RGB", (2048, 1024), green)
    draw = ImageDraw.Draw(image)
    big = ImageFont.truetype(fonts["bold"], 150)
    mid = ImageFont.truetype(fonts["bold"], 110)
    small = ImageFont.truetype(fonts["regular"], 84)
    for x0 in (0, 1024):
        draw.rounded_rectangle([x0 + 22, 22, x0 + 1002, 1002], radius=48, outline=white, width=16)
    # Left: an exit.
    draw.rounded_rectangle([60, 70, 520, 230], radius=20, fill=white)
    draw.text((290, 150), "EXIT 27", font=mid, fill=green, anchor="mm")
    draw.text((512, 470), "Harbor Blvd", font=big, fill=white, anchor="mm")
    draw.text((512, 650), "Sunset Ave", font=big, fill=white, anchor="mm")
    draw.text((512, 860), "1/2 MILE", font=small, fill=white, anchor="mm")
    # Right: the road ahead.
    draw.rounded_rectangle([1024 + 330, 70, 1024 + 694, 230], radius=20, outline=white, width=10)
    draw.text((1024 + 512, 150), "LR 9", font=mid, fill=white, anchor="mm")
    draw.text((1024 + 512, 330), "NORTH", font=mid, fill=white, anchor="mm")
    draw.text((1024 + 512, 560), "Pacific Valley", font=big, fill=white, anchor="mm")
    draw.text((1024 + 512, 760), "Golden Hills", font=big, fill=white, anchor="mm")
    image = image.filter(ImageFilter.GaussianBlur(0.6))
    image.save(path)


# --- Vegetation ------------------------------------------------------------------------------------


def grow(mesh, rng, start, direction, length, radius, depth, spec, tips):
    """Recursive branch: a tapered tube, then children that spread and lean upwards."""
    direction = direction / np.linalg.norm(direction)
    bend = np.array([0.0, 0.0, spec["lift"]]) + rng.normal(0, 0.18, 3)
    end = start + direction * length + bend * length * 0.25
    mid = (start + end) / 2 + rng.normal(0, 0.08 * length, 3)
    points = np.array([start, mid, end])
    sides = 9 if depth == 0 else 6
    v, n, f = tube(points, radius, sides)
    # Taper towards the end of the branch (the tube has sides + 1 vertices per ring; caps, if any, follow).
    ring = sides + 1
    rings = len(v) // ring
    if rings >= len(points):
        scale = np.ones(len(v))
        scale[:len(points) * ring] = np.linspace(1.0, 0.68, len(points)).repeat(ring)
        center = np.zeros_like(v)
        center[:len(points) * ring] = np.repeat(points, ring, 0)
        center[len(points) * ring:] = v[len(points) * ring:]
        v = center + (v - center) * scale[:, None]
    mesh.add("Bark", (v, n, f))
    if depth >= spec["depth"]:
        tips.append(end)
        return
    for _ in range(rng.integers(spec["children"][0], spec["children"][1] + 1)):
        spread = spec["spread"] * (0.7 + 0.6 * rng.random())
        axis = normalize(np.cross(direction, rng.normal(0, 1, 3)))
        child = direction * math.cos(spread) + axis * math.sin(spread)
        child[2] = abs(child[2]) * 0.6 + 0.25 if depth < 2 else child[2]
        grow(mesh, rng, end, child, length * spec["decay"] * (0.85 + 0.3 * rng.random()), radius * 0.62, depth + 1, spec, tips)
    if depth >= spec["depth"] - 1:
        tips.append(end)


def foliage(mesh, rng, centers, crown_center, count, card, spread, squash=1.0):
    """Leaf cards around each center. Normals point away from the crown, so the canopy shades as one soft volume."""
    quadrants = [(0.0, 0.0, 0.5, 0.5), (0.5, 0.0, 1.0, 0.5), (0.0, 0.5, 0.5, 1.0), (0.5, 0.5, 1.0, 1.0)]
    for center in centers:
        for _ in range(count):
            p = center + rng.normal(0, spread, 3) * np.array([1.0, 1.0, squash])
            right = normalize(rng.normal(0, 1, 3))
            up = normalize(np.cross(right, rng.normal(0, 1, 3)))
            outward = normalize(p - crown_center + np.array([0.0, 0.0, 0.35 * np.linalg.norm(p - crown_center)]))
            size = card * (0.75 + 0.5 * rng.random())
            part, uv = quad(p, right, up, outward, (size, size), quadrants[rng.integers(4)])
            mesh.add("Leaf", part, uv)


def tree(seed, spec):
    rng = np.random.default_rng(seed)
    mesh = Mesh()
    tips = []
    grow(mesh, rng, np.zeros(3), np.array([0.0, 0.0, 1.0]), spec["trunk"], spec["radius"], 0, spec, tips)
    # The trunk goes a little into the ground, so slopes never show its base.
    mesh.add("Bark", tube(np.array([[0, 0, -0.8], [0, 0, 0.2]]), spec["radius"] * 1.15, 9))
    tips = np.array(tips)
    crown = tips.mean(0)
    foliage(mesh, rng, tips, crown, spec["cards"], spec["card"], spec["spread"] * 0.55, spec.get("squash", 0.8))
    # Fill the volume of the crown too: leaves only at the tips read as separate lumps.
    radii = np.maximum(np.abs(tips - crown).max(0) * 0.85, 0.8)
    inside = []
    while len(inside) < spec["fill"]:
        p = rng.uniform(-1.0, 1.0, 3)
        if p @ p <= 1.0:
            inside.append(crown + p * radii)
    foliage(mesh, rng, inside, crown, 1, spec["card"], 0.05, 1.0)
    return mesh


def bush(seed):
    rng = np.random.default_rng(seed)
    mesh = Mesh()
    centers = [np.array([rng.normal(0, 0.5), rng.normal(0, 0.5), 0.45 + 0.2 * rng.random()]) for _ in range(5)]
    foliage(mesh, rng, centers, np.array([0.0, 0.0, 0.2]), 9, 1.0, 0.45, 0.6)
    return mesh


def rock(seed):
    """A noisy, flattened sphere half buried in the ground."""
    rng = np.random.default_rng(seed)
    v, n, f = lathe([(0.0, -1.0), (0.55, -0.85), (0.9, -0.4), (1.0, 0.0), (0.9, 0.4), (0.55, 0.85), (0.0, 1.0)], 18)
    # Lathe axis is Y: stand it up on Z.
    v = np.stack([v[:, 0], v[:, 2], v[:, 1]], 1)
    n = np.stack([n[:, 0], n[:, 2], n[:, 1]], 1)
    bumps = 1.0 + 0.22 * np.sin(v @ rng.normal(0, 3.0, 3)) + 0.12 * np.sin(v @ rng.normal(0, 7.0, 3))
    v = v * bumps[:, None] * np.array([1.3, 1.0, 0.6])
    v[:, 2] += 0.15
    f = orient(v, f, v[f].mean(1) - np.array([0.0, 0.0, 0.15]))
    # Normals from the deformed surface: the face winding points inwards (see orient), so they are negated.
    normals = np.zeros_like(v)
    tri = v[f]
    face = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    for k in range(3):
        np.add.at(normals, f[:, k], -face)
    mesh = Mesh()
    mesh.add("Rock", (v, normalize(normals), f))
    return mesh


# --- Structures ------------------------------------------------------------------------------------


def gantry(half_span=13.5):
    """Steel sign gantry over the road: two columns, a truss and two guide signs facing the traffic (towards -X)."""
    mesh = Mesh()
    for y in (-half_span, half_span):
        mesh.add("Concrete", box((0.0, y, -0.9), (1.4, 1.4, 1.6)))
        mesh.add("Metal", box((0.0, y, 4.2), (0.45, 0.45, 9.0)))
    for z in (7.0, 8.6):
        mesh.add("Metal", box((0.35, 0.0, z), (0.3, 2 * half_span, 0.3)))
    for k in range(18):
        y0 = -half_span + k * (2 * half_span / 18)
        y1 = y0 + 2 * half_span / 18
        z0, z1 = (7.0, 8.6) if k % 2 == 0 else (8.6, 7.0)
        mesh.add("Metal", tube(np.array([[0.35, y0, z0], [0.35, y1, z1]]), 0.06, 6))
    # Panels: 6 m wide, 6 m tall would be too much; 5.6 x 5.6 keeps the square of the atlas.
    for center_y, (u0, u1) in ((-4.6, (0.0, 0.5)), (4.0, (0.5, 1.0))):
        mesh.add("SignBack", box((0.12, center_y, 7.8), (0.12, 5.7, 3.9)))
        part, uv = quad(np.array([0.05, center_y, 7.8]), np.array([0.0, 1.0, 0.0]), np.array([0.0, 0.0, 1.0]),
                        np.array([-1.0, 0.0, 0.0]), (5.6, 3.8), (u0, 0.0, u1, 1.0))
        mesh.add("Sign", part, uv)
    return mesh


def overpass(half_length=80.0, deck_z=6.8, pier_y=15.5):
    """Concrete bridge across the road with two piers and an earth ramp at each end."""
    mesh = Mesh()
    deck_half = 46.0
    mesh.add("Concrete", box((0.0, 0.0, deck_z + 0.7), (12.0, 2 * deck_half, 1.4)))
    for x in (-5.85, 5.85):
        mesh.add("Concrete", box((x, 0.0, deck_z + 1.9), (0.3, 2 * deck_half, 1.0)))
    for y in (-pier_y, pier_y):
        mesh.add("Concrete", box((0.0, y, deck_z - 0.5), (11.0, 1.6, 1.0)))
        for x in (-4.0, 0.0, 4.0):
            mesh.add("Concrete", tube(np.array([[x, y, -3.0], [x, y, deck_z - 1.0]]), 0.55, 14))
    # Abutment walls and earth ramps down to the ground.
    for sign in (-1.0, 1.0):
        wall_y = sign * 40.0
        mesh.add("Concrete", box((0.0, wall_y, (deck_z - 3.0) / 2.0), (14.0, 1.0, deck_z + 3.0)))
        top = deck_z
        y_in, y_out = 40.0, half_length
        corners = []
        for x in (-9.0, 9.0):
            corners.append([(x, sign * y_in, top), (x, sign * y_out, -1.5)])
        a, b = corners
        # Sloped top of the ramp.
        v = np.array([a[0], b[0], b[1], a[1]], float)
        normal = normalize(np.cross(v[1] - v[0], v[3] - v[0]))
        normal = normal if normal[2] > 0 else -normal
        faces = orient(v, np.array([[0, 1, 2], [0, 2, 3]]), np.repeat(normal[None, :], 2, 0))
        mesh.add("Earth", (v, np.repeat(normal[None, :], 4, 0), faces))
        # Its two sides.
        for x in (-9.0, 9.0):
            side = np.array([(x, sign * y_in, top), (x, sign * y_out, -1.5), (x, sign * y_in, -1.5)], float)
            outward = np.array([[math.copysign(1.0, x), 0.0, 0.0]])
            mesh.add("Earth", (side, np.repeat(outward, 3, 0), orient(side, np.array([[0, 1, 2]]), outward)))
    return mesh


OAK = {"trunk": 2.4, "radius": 0.34, "depth": 3, "children": (2, 3), "spread": 0.75, "decay": 0.72, "lift": 0.25, "cards": 14, "card": 1.5, "squash": 0.7, "fill": 140}
OAK_WIDE = dict(OAK, trunk=2.0, spread=0.95, decay=0.78, lift=0.12, cards=14, card=1.6, fill=170)
EUCALYPTUS = {"trunk": 7.5, "radius": 0.28, "depth": 3, "children": (2, 3), "spread": 0.45, "decay": 0.62, "lift": 0.45, "cards": 12, "card": 1.4, "squash": 1.1, "fill": 90}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", default="Saved/PropGen")
    parser.add_argument("--textures", default="Saved/SceneGen/Textures")
    args = parser.parse_args()
    out = pathlib.Path(args.out)
    textures = pathlib.Path(args.textures)
    out.mkdir(parents=True, exist_ok=True)
    textures.mkdir(parents=True, exist_ok=True)

    windows_fonts = pathlib.Path("C:/Windows/Fonts")
    fonts = {"bold": str(windows_fonts / "bahnschrift.ttf"), "regular": str(windows_fonts / "bahnschrift.ttf")}
    leaf_atlas(textures / "T_Leaves_D.png")
    sign_atlas(textures / "T_Signs_D.png", fonts)

    props = {
        "SM_Prop_Oak_A": tree(1, OAK),
        "SM_Prop_Oak_B": tree(2, OAK_WIDE),
        "SM_Prop_Eucalyptus": tree(3, EUCALYPTUS),
        "SM_Prop_Bush_A": bush(4),
        "SM_Prop_Bush_B": bush(5),
        "SM_Prop_Rock_A": rock(6),
        "SM_Prop_Rock_B": rock(7),
        "SM_Prop_Gantry": gantry(),
        "SM_Prop_Overpass": overpass(),
    }
    for name, mesh in props.items():
        write_glb(mesh, out / f"{name}.glb")
        print(f"{name}: {mesh.triangle_count()} triangles")


if __name__ == "__main__":
    main()
