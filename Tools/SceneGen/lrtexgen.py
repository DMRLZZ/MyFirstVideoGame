"""Generates the tiling textures of the world from code: no external assets, no licences to track.

    python Tools/SceneGen/lrtexgen.py --out Saved/SceneGen/Textures

Every map is periodic by construction (filtered noise with wrapped borders), so it tiles without seams.
Writes PNG files; Tools/Editor/create_scene_materials.py imports them into Unreal.

Output, for each surface: T_<name>_D (color, sRGB), T_<name>_N (normal, DirectX) and T_<name>_R (roughness).
T_Macro_M is a large mask that breaks the repetition: R = mottling, G = cracks, B = rectangular repairs.
"""
import argparse
import pathlib

import numpy as np
from PIL import Image
from scipy.ndimage import gaussian_filter


def white(n, seed):
    return np.random.default_rng(seed).standard_normal((n, n))


def normalized(a):
    a = a - a.min()
    return a / max(a.max(), 1e-9)


def blur(a, sigma):
    return gaussian_filter(a, sigma=sigma, mode="wrap")


def fractal(n, beta, seed):
    """Periodic noise with a 1/f^beta spectrum: large soft shapes with finer detail on top. Range [0, 1]."""
    fx = np.fft.fftfreq(n)[:, None]
    fy = np.fft.fftfreq(n)[None, :]
    f = np.hypot(fx, fy)
    f[0, 0] = 1.0
    spectrum = np.fft.fft2(white(n, seed)) / f ** beta
    spectrum[0, 0] = 0.0
    return normalized(np.fft.ifft2(spectrum).real)


def streaks(n, angle_deg, length, width, seed):
    """Periodic noise stretched along one direction: grass blades, weather stains."""
    fx = np.fft.fftfreq(n)[:, None]
    fy = np.fft.fftfreq(n)[None, :]
    a = np.radians(angle_deg)
    along = fx * np.cos(a) + fy * np.sin(a)
    across = -fx * np.sin(a) + fy * np.cos(a)
    envelope = np.exp(-((along * length) ** 2) - ((across * width) ** 2))
    return normalized(np.fft.ifft2(np.fft.fft2(white(n, seed)) * envelope).real)


def normal_map(height, strength):
    dx = (np.roll(height, -1, axis=1) - np.roll(height, 1, axis=1)) * strength
    dy = (np.roll(height, -1, axis=0) - np.roll(height, 1, axis=0)) * strength
    n = np.stack([-dx, dy, np.ones_like(height)], axis=-1)  # DirectX: green points down the image.
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    return n * 0.5 + 0.5


def lerp(a, b, t):
    t = t[..., None] if a.ndim == 3 and np.ndim(t) == 2 else t
    return a * (1.0 - t) + b * t


def smooth(edge0, edge1, x):
    t = np.clip((x - edge0) / (edge1 - edge0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def to_srgb(linear):
    linear = np.clip(linear, 0.0, 1.0)
    return np.where(linear <= 0.0031308, linear * 12.92, 1.055 * linear ** (1 / 2.4) - 0.055)


def save(path, data, srgb=False):
    data = to_srgb(data) if srgb else data
    Image.fromarray((np.clip(data, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)).save(path)


def write_surface(out, name, color, height, roughness, strength):
    save(out / f"T_{name}_D.png", color, srgb=True)
    save(out / f"T_{name}_N.png", normal_map(height, strength))
    save(out / f"T_{name}_R.png", roughness)
    print("written", name, color.shape)


def asphalt(out, n=2048):
    """Aggregate stones in a dark binder. The tile is 4 m wide: one pixel is about 2 mm."""
    stones = normalized(blur(white(n, 1), 2.2))
    fine = normalized(blur(white(n, 2), 1.0))
    coarse = fractal(n, 1.3, 3)
    exposed = smooth(0.56, 0.68, stones)  # Stone tops that stick out of the binder.
    tint = normalized(blur(white(n, 4), 3.0))
    binder = np.array([0.030, 0.030, 0.032])
    stone_a = np.array([0.150, 0.145, 0.135])
    stone_b = np.array([0.095, 0.098, 0.105])
    stone = lerp(np.broadcast_to(stone_a, (n, n, 3)), np.broadcast_to(stone_b, (n, n, 3)), tint)
    color = lerp(np.broadcast_to(binder, (n, n, 3)), stone, exposed * (0.75 + 0.25 * fine))
    color = color * (0.85 + 0.3 * coarse)[..., None]
    height = 0.7 * stones + 0.2 * fine + 0.1 * coarse
    roughness = 0.92 - 0.14 * exposed - 0.05 * fine
    write_surface(out, "Asphalt", color, height, roughness, 2.2)


def concrete(out, n=1024):
    """Cast concrete: soft blotches, small air pits and vertical weather stains. The tile is 4 m wide."""
    blotch = fractal(n, 1.4, 11)
    grain = normalized(blur(white(n, 12), 0.8))
    pits = smooth(0.74, 0.8, normalized(blur(white(n, 13), 1.4)))
    stains = streaks(n, 90.0, 90.0, 5.0, 14)  # Along U: vertical on a barrier, whose V runs along the road.
    value = 0.34 + 0.16 * (blotch - 0.5) + 0.04 * (grain - 0.5) - 0.12 * pits - 0.13 * smooth(0.55, 0.85, stains)
    color = value[..., None] * np.array([1.0, 0.975, 0.93])
    height = 0.5 * blotch + 0.2 * grain - 0.6 * pits
    write_surface(out, "Concrete", color, height, 0.86 + 0.08 * (grain - 0.5), 1.4)


def terrain(out, n=1024):
    """Dry California grass over bare earth. The tile is 8 m wide."""
    blades = sum(streaks(n, angle, 26.0, 3.0, 20 + i) for i, angle in enumerate((15, 70, 120, 165))) / 4.0
    blades = normalized(blades)
    patches = fractal(n, 1.6, 30)
    speck = normalized(blur(white(n, 31), 1.0))
    straw = np.array([0.44, 0.31, 0.13])
    pale = np.array([0.62, 0.50, 0.27])
    olive = np.array([0.17, 0.17, 0.065])
    earth = np.array([0.16, 0.105, 0.065])
    grass = lerp(np.broadcast_to(straw, (n, n, 3)), np.broadcast_to(pale, (n, n, 3)), smooth(0.45, 0.8, blades))
    grass = lerp(grass, np.broadcast_to(olive, (n, n, 3)), smooth(0.55, 0.8, 1.0 - patches) * 0.55)
    bare = smooth(0.74, 0.92, patches + 0.25 * (speck - 0.5))  # Small patches only: the macro mask paints the large ones.
    color = lerp(grass, np.broadcast_to(earth, (n, n, 3)), bare) * (0.8 + 0.4 * speck)[..., None]
    height = lerp(blades[..., None], 0.35 * patches[..., None] + 0.2 * speck[..., None], bare)[..., 0]
    write_surface(out, "Terrain", color, height, 0.93 - 0.05 * blades, 2.5)


def macro(out, n=1024):
    """Large mask, one tile every 62.5 m. A divisor of 1000 m, the wrap of the road UVs."""
    mottling = fractal(n, 1.8, 41)
    ridged = 1.0 - np.abs(2.0 * fractal(n, 1.5, 42) - 1.0)
    ridged_b = 1.0 - np.abs(2.0 * fractal(n, 1.7, 43) - 1.0)
    cracks = smooth(0.985, 0.998, ridged) * smooth(0.45, 0.7, fractal(n, 1.8, 44))
    cracks = np.maximum(cracks, 0.6 * smooth(0.992, 0.999, ridged_b))
    rng = np.random.default_rng(45)
    repairs = np.zeros((n, n))
    for _ in range(9):
        w, h = rng.integers(30, 120), rng.integers(70, 330)
        x, y = rng.integers(0, n, 2)
        rows = np.arange(y, y + h) % n
        cols = np.arange(x, x + w) % n
        repairs[np.ix_(rows, cols)] = rng.uniform(0.45, 1.0)
    save(out / "T_Macro_M.png", np.stack([mottling, cracks, blur(repairs, 0.8)], axis=-1))
    print("written Macro")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", default="Saved/SceneGen/Textures")
    args = parser.parse_args()
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    asphalt(out)
    concrete(out)
    terrain(out)
    macro(out)


if __name__ == "__main__":
    main()
