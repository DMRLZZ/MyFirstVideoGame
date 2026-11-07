"""Composes and renders the music of the game from code: no samples and no external material, so no third-party rights.

    python Tools/MusicGen/lrmusicgen.py --out Data/Music

Three synthwave tracks for driving at sunset. Every sound is synthesized here: supersaw pads, plucked arpeggios, bass,
lead, drums and the reverb. Writes one M4A (AAC) per track through ffmpeg, which must be on the PATH. AAC because the
media player of the engine opens it directly.
"""
import argparse
import pathlib
import subprocess
import tempfile
from dataclasses import dataclass, field

import numpy as np
from scipy.signal import butter, fftconvolve, sosfilt
from scipy.io import wavfile

RATE = 44100


def midi_hz(note):
    return 440.0 * 2.0 ** ((note - 69) / 12.0)


def poly_blep(t, dt):
    out = np.zeros_like(t)
    low = t < dt
    x = t[low] / dt[low]
    out[low] = x + x - x * x - 1.0
    high = t > 1.0 - dt
    x = (t[high] - 1.0) / dt[high]
    out[high] = x * x + x + x + 1.0
    return out


def saw(hz, n, phase=0.0):
    """Band-limited sawtooth. hz is a number or an array with one value per sample."""
    dt = np.full(n, hz / RATE) if np.isscalar(hz) else hz / RATE
    t = (phase + np.cumsum(dt)) % 1.0
    return 2.0 * t - 1.0 - poly_blep(t, dt)


def pulse(hz, n, width=0.5, phase=0.0):
    a = saw(hz, n, phase)
    b = saw(hz, n, (phase + width) % 1.0)
    return 0.5 * (a - b)


def lowpass(x, cutoff, order=2):
    sos = butter(order, min(cutoff, RATE * 0.45) / (RATE * 0.5), btype="low", output="sos")
    return sosfilt(sos, x, axis=0)


def highpass(x, cutoff, order=2):
    sos = butter(order, cutoff / (RATE * 0.5), btype="high", output="sos")
    return sosfilt(sos, x, axis=0)


def bandpass(x, low, high):
    sos = butter(2, [low / (RATE * 0.5), min(high, RATE * 0.45) / (RATE * 0.5)], btype="band", output="sos")
    return sosfilt(sos, x, axis=0)


def envelope(n, attack, decay, sustain, release):
    """Attack, decay and sustain over n samples, then a release tail is part of n."""
    a = max(int(attack * RATE), 1)
    d = max(int(decay * RATE), 1)
    r = max(int(release * RATE), 1)
    held = max(n - a - d - r, 0)
    curve = np.concatenate([
        np.linspace(0.0, 1.0, a, endpoint=False),
        sustain + (1.0 - sustain) * np.exp(-5.0 * np.arange(d) / d),
        np.full(held, sustain),
        sustain * np.exp(-5.0 * np.arange(r) / r),
    ])
    if len(curve) < n:
        curve = np.concatenate([curve, np.zeros(n - len(curve))])
    return curve[:n]


def add(bus, start, sound):
    """Mixes a sound into a bus from a start time in seconds. Mono sounds go to both channels."""
    i = int(start * RATE)
    if i >= len(bus):
        return
    if sound.ndim == 1:
        sound = np.stack([sound, sound], axis=1)
    n = min(len(sound), len(bus) - i)
    bus[i:i + n] += sound[:n]


def make_reverb(seconds, seed, damping):
    rng = np.random.default_rng(seed)
    n = int(seconds * RATE)
    decay = np.exp(-6.0 * np.arange(n) / n)
    left = lowpass(rng.standard_normal(n) * decay, damping)
    right = lowpass(rng.standard_normal(n) * decay, damping)
    ir = np.stack([left, right], axis=1)
    return ir / np.sqrt((ir ** 2).sum(axis=0)).mean()


def apply_reverb(bus, ir, wet):
    tail = np.stack([fftconvolve(bus[:, c], ir[:, c])[:len(bus)] for c in range(2)], axis=1)
    return bus + wet * tail


def ping_pong(bus, delay_seconds, feedback, wet):
    out = bus.copy()
    step = int(delay_seconds * RATE)
    gain = wet
    for tap in range(1, 7):
        offset = step * tap
        if offset >= len(bus):
            break
        channel = tap % 2
        out[offset:, channel] += gain * bus[:-offset, 0 if channel else 1]
        gain *= feedback
    return out


# --- Instruments ---------------------------------------------------------------------------------


def pad_chord(notes, seconds, cutoff, rng):
    n = int((seconds + 0.5) * RATE)
    left = np.zeros(n)
    right = np.zeros(n)
    for note in notes:
        hz = midi_hz(note)
        for voice, cents in enumerate((-16.0, -9.0, -4.0, 0.0, 4.0, 9.0, 16.0)):
            sound = saw(hz * 2.0 ** (cents / 1200.0), n, rng.random())
            pan = voice / 6.0
            left += sound * (1.0 - pan)
            right += sound * pan
    env = envelope(n, 0.18, 0.3, 0.85, 0.5)
    stereo = np.stack([left, right], axis=1) * env[:, None] / (len(notes) * 3.5)
    return lowpass(stereo, cutoff)


def pluck(note, seconds, cutoff, rng, width=0.5):
    n = int((seconds + 0.25) * RATE)
    sound = pulse(midi_hz(note), n, width, rng.random()) + 0.4 * saw(midi_hz(note) * 1.003, n, rng.random())
    bright = lowpass(sound, cutoff)
    dark = lowpass(sound, cutoff * 0.3)
    fade = np.exp(-14.0 * np.arange(n) / RATE)
    return (bright * fade + dark * (1.0 - fade)) * envelope(n, 0.002, 0.16, 0.0, 0.1) * 0.5


def bass(note, seconds, drive=1.0):
    n = int((seconds + 0.04) * RATE)
    hz = midi_hz(note)
    body = saw(hz, n) + 0.8 * saw(hz * 1.004, n, 0.3)
    bright = lowpass(body, 1500.0)
    dark = lowpass(body, 320.0)
    fade = np.exp(-16.0 * np.arange(n) / RATE)
    sub = np.sin(2.0 * np.pi * hz * np.arange(n) / RATE)
    env = envelope(n, 0.004, 0.08, 0.8, 0.03)
    return np.tanh(((bright * fade + dark * (1.0 - fade)) * 0.5 + sub * 0.8) * drive) * env * 0.5


def lead(note, seconds, glide_from=None, brightness=3200.0, rng=None):
    n = int((seconds + 0.2) * RATE)
    time = np.arange(n) / RATE
    hz = np.full(n, midi_hz(note))
    if glide_from is not None:
        hz *= (midi_hz(glide_from) / midi_hz(note)) ** np.exp(-time / 0.03)
    # Vibrato that comes in on long notes.
    hz *= 2.0 ** (np.minimum(time / 0.45, 1.0) * 0.13 * np.sin(2.0 * np.pi * 5.4 * time) / 12.0)
    phase = rng.random()
    sound = saw(hz, n, phase) + 0.7 * saw(hz * 1.006, n, phase + 0.2) + 0.5 * pulse(hz * 0.5, n, 0.3, phase)
    return lowpass(sound, brightness) * envelope(n, 0.012, 0.15, 0.7, 0.18) * 0.28


def kick():
    n = int(0.42 * RATE)
    time = np.arange(n) / RATE
    hz = 46.0 + 115.0 * np.exp(-time / 0.035)
    body = np.sin(2.0 * np.pi * np.cumsum(hz) / RATE) * np.exp(-time / 0.14)
    click = np.exp(-time / 0.0025) * 0.45
    return np.tanh((body + click) * 1.5) * 0.85


def snare(rng):
    n = int(0.36 * RATE)
    time = np.arange(n) / RATE
    noise = bandpass(rng.standard_normal(n), 900.0, 9000.0) * np.exp(-time / 0.1)
    tone = np.sin(2.0 * np.pi * 186.0 * time) * np.exp(-time / 0.06)
    return (noise * 1.1 + tone * 0.6) * 0.5


def hat(rng, seconds):
    n = int(seconds * RATE)
    time = np.arange(n) / RATE
    return highpass(rng.standard_normal(n), 7500.0) * np.exp(-time / (seconds * 0.3)) * 0.16


def crash(rng):
    n = int(2.8 * RATE)
    time = np.arange(n) / RATE
    sound = highpass(rng.standard_normal((n, 2)), 3500.0)
    return sound * np.exp(-time / 0.8)[:, None] * 0.16


def riser(rng, seconds):
    n = int(seconds * RATE)
    fade = np.linspace(0.0, 1.0, n) ** 2.5
    sound = highpass(rng.standard_normal((n, 2)), 1800.0)
    return sound * fade[:, None] * 0.13


# --- Songs ---------------------------------------------------------------------------------------


@dataclass
class Section:
    bars: int
    pad: float = 1.0
    arp: float = 0.0
    bass: float = 0.0
    drums: float = 0.0
    hats16: bool = False
    lead: float = 0.0
    lead_octave: int = 0
    cutoff: float = 2400.0
    riser: bool = False
    crash: bool = False


@dataclass
class Song:
    name: str
    bpm: float
    seed: int
    # One bar per chord: chord tones as MIDI notes in the middle register. The first one is the root.
    chords: list
    # One entry per bar of the progression: (beat, MIDI note, length in beats).
    melody: list
    arp_pattern: list
    bass_pattern: list
    sections: list = field(default_factory=list)


def render(song):
    rng = np.random.default_rng(song.seed)
    beat = 60.0 / song.bpm
    bar = 4.0 * beat
    total_bars = sum(s.bars for s in song.sections)
    n = int((total_bars * bar + 5.0) * RATE)

    pads = np.zeros((n, 2))
    arps = np.zeros((n, 2))
    basses = np.zeros((n, 2))
    drums = np.zeros((n, 2))
    snares = np.zeros((n, 2))
    leads = np.zeros((n, 2))
    fx = np.zeros((n, 2))

    kick_sound = kick()
    start_bar = 0
    for section in song.sections:
        begin = start_bar * bar
        if section.crash:
            add(fx, begin, crash(rng))
        if section.riser:
            add(fx, begin + (section.bars - 2) * bar, riser(rng, 2.0 * bar))

        for index in range(section.bars):
            # The filter opens over a section, which gives the arrangement its breath.
            progress = index / max(section.bars - 1, 1)
            step = (start_bar + index) % len(song.chords)
            chord = song.chords[step]
            when = (start_bar + index) * bar

            if section.pad > 0.0:
                sound = pad_chord(chord, bar, section.cutoff * (0.75 + 0.5 * progress), rng)
                add(pads, when, sound * section.pad)

            if section.bass > 0.0:
                root = chord[0] - 24
                for position, offset in song.bass_pattern:
                    add(basses, when + position * beat, bass(root + offset, 0.45 * beat if offset else 0.42 * beat) * section.bass)

            if section.arp > 0.0:
                for sixteenth, pick in enumerate(song.arp_pattern):
                    note = chord[pick % len(chord)] + 12 * (pick // len(chord)) + 12
                    add(arps, when + sixteenth * beat / 4.0, pluck(note, beat / 2.0, 1500.0 + 2600.0 * progress, rng) * section.arp)

            if section.drums > 0.0:
                for b in range(4):
                    add(drums, when + b * beat, kick_sound * section.drums)
                    add(drums, when + (b + 0.5) * beat, hat(rng, 0.09) * section.drums)
                    if section.hats16:
                        add(drums, when + (b + 0.25) * beat, hat(rng, 0.04) * 0.55 * section.drums)
                        add(drums, when + (b + 0.75) * beat, hat(rng, 0.04) * 0.55 * section.drums)
                add(snares, when + beat, snare(rng) * section.drums)
                add(snares, when + 3.0 * beat, snare(rng) * section.drums)
                # A fill closes every eighth bar.
                if (index + 1) % 8 == 0:
                    for hit in (3.25, 3.5, 3.75):
                        add(snares, when + hit * beat, snare(rng) * 0.6 * section.drums)

            if section.lead > 0.0:
                previous = None
                for start, note, length in song.melody[step]:
                    pitch = note + section.lead_octave * 12
                    add(leads, when + start * beat, lead(pitch, length * beat, previous, 2600.0 + 1800.0 * progress, rng) * section.lead)
                    previous = pitch
        start_bar += section.bars

    # Everything except the drums ducks on each kick: the pumping that defines the style.
    time = np.arange(n) / RATE
    position = (time / beat) % 1.0
    pump = 1.0 - 0.55 * np.exp(-position * beat / 0.11)
    has_drums = np.zeros(n)
    bar_index = 0
    for section in song.sections:
        begin = int(bar_index * bar * RATE)
        end = int((bar_index + section.bars) * bar * RATE)
        has_drums[begin:end] = 1.0 if section.drums > 0.0 else 0.0
        bar_index += section.bars
    pump = 1.0 - (1.0 - pump) * has_drums
    duck = pump[:, None]

    hall = make_reverb(2.8, song.seed + 1, 5200.0)
    room = make_reverb(1.1, song.seed + 2, 7000.0)

    leads = ping_pong(leads, 0.75 * beat, 0.42, 0.34)
    leads = apply_reverb(leads, hall, 0.2)
    arps = ping_pong(arps, 0.75 * beat, 0.5, 0.42)
    arps = apply_reverb(arps, hall, 0.12)
    pads = apply_reverb(pads, hall, 0.1)
    snares = apply_reverb(snares, room, 0.28)

    mix = (pads * 0.5 + arps * 0.34 + basses * 0.62) * duck + drums * 0.62 + snares * 0.55 + leads * 0.5 + fx * 0.9
    mix = highpass(mix, 28.0)
    mix = np.tanh(mix * 1.15)

    end = int((total_bars * bar + 3.5) * RATE)
    mix = mix[:end]
    fade = int(3.0 * RATE)
    mix[-fade:] *= np.linspace(1.0, 0.0, fade)[:, None] ** 2
    mix *= 0.89 / max(np.abs(mix).max(), 1e-6)
    return mix


def structure(cutoff):
    return [
        Section(8, pad=0.9, arp=0.55, cutoff=cutoff * 0.45, riser=True),
        Section(8, pad=0.9, arp=0.8, bass=0.8, cutoff=cutoff * 0.6, riser=True),
        Section(16, pad=1.0, arp=0.8, bass=1.0, drums=1.0, cutoff=cutoff, crash=True),
        Section(16, pad=1.0, arp=0.8, bass=1.0, drums=1.0, lead=1.0, hats16=True, cutoff=cutoff, riser=True, crash=True),
        Section(8, pad=1.0, arp=0.6, lead=0.75, cutoff=cutoff * 0.5, riser=True),
        Section(16, pad=1.05, arp=0.9, bass=1.0, drums=1.0, lead=1.0, lead_octave=1, hats16=True, cutoff=cutoff * 1.2, crash=True),
        Section(16, pad=1.0, arp=0.9, bass=1.0, drums=1.0, lead=0.9, hats16=True, cutoff=cutoff, crash=True),
        Section(8, pad=0.9, arp=0.5, cutoff=cutoff * 0.4),
    ]


ARP_UP = [0, 1, 2, 4, 2, 1, 0, 1, 2, 4, 5, 4, 2, 1, 2, 4]
ARP_WIDE = [0, 2, 1, 3, 2, 4, 3, 5, 0, 2, 1, 3, 4, 3, 2, 1]
ARP_SOFT = [0, 2, 4, 2, 1, 2, 4, 5, 0, 2, 4, 2, 1, 2, 5, 4]

EIGHTHS_OFF = [(0.0, 0), (0.5, 0), (1.0, 0), (1.5, 12), (2.0, 0), (2.5, 0), (3.0, 0), (3.5, 12)]
GALLOP = [(b + o, 0) for b in range(4) for o in (0.0, 0.25, 0.5)] + [(3.75, 12)]
ROLLING = [(0.0, 0), (0.75, 0), (1.5, 0), (2.0, 0), (2.75, 0), (3.5, 7)]


SONGS = [
    Song(
        name="Neon_Freeway", bpm=104.0, seed=11,
        chords=[[57, 60, 64], [53, 57, 60], [60, 64, 67], [55, 59, 62]],  # Am F C G
        melody=[
            [(0.0, 76, 1.5), (1.5, 74, 0.5), (2.0, 72, 1.0), (3.0, 76, 1.0)],
            [(0.0, 77, 1.5), (1.5, 76, 0.5), (2.0, 72, 2.0)],
            [(0.0, 79, 1.0), (1.0, 76, 1.0), (2.0, 74, 1.0), (3.0, 72, 1.0)],
            [(0.0, 74, 2.0), (2.0, 71, 1.0), (3.0, 74, 1.0)],
        ],
        arp_pattern=ARP_UP, bass_pattern=EIGHTHS_OFF, sections=structure(3000.0),
    ),
    Song(
        name="Last_Exit", bpm=118.0, seed=23,
        chords=[[54, 57, 61], [50, 54, 57], [59, 62, 66], [49, 52, 56]],  # F#m D Bm C#m
        melody=[
            [(0.0, 73, 0.75), (0.75, 69, 0.75), (1.5, 66, 0.5), (2.0, 69, 1.0), (3.0, 73, 1.0)],
            [(0.0, 74, 0.75), (0.75, 69, 0.75), (1.5, 66, 0.5), (2.0, 69, 2.0)],
            [(0.0, 71, 1.0), (1.0, 74, 1.0), (2.0, 78, 1.5), (3.5, 76, 0.5)],
            [(0.0, 73, 2.0), (2.0, 68, 1.0), (3.0, 76, 1.0)],
        ],
        arp_pattern=ARP_WIDE, bass_pattern=GALLOP, sections=structure(3600.0),
    ),
    Song(
        name="Golden_Hour", bpm=92.0, seed=37,
        chords=[[60, 63, 67], [56, 60, 63], [63, 67, 70], [58, 62, 65]],  # Cm Ab Eb Bb
        melody=[
            [(0.0, 75, 2.0), (2.0, 79, 1.0), (3.0, 77, 1.0)],
            [(0.0, 75, 1.5), (1.5, 72, 0.5), (2.0, 68, 2.0)],
            [(0.0, 70, 1.0), (1.0, 75, 1.0), (2.0, 79, 2.0)],
            [(0.0, 77, 1.5), (1.5, 74, 0.5), (2.0, 70, 1.0), (3.0, 74, 1.0)],
        ],
        arp_pattern=ARP_SOFT, bass_pattern=ROLLING, sections=structure(2500.0),
    ),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", default="Data/Music")
    parser.add_argument("--only", help="Render a single song by name.")
    args = parser.parse_args()
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    for song in SONGS:
        if args.only and song.name != args.only:
            continue
        mix = render(song)
        rms = 20.0 * np.log10(np.sqrt((mix ** 2).mean()))
        print(f"{song.name}: {len(mix) / RATE:.0f} s, peak {np.abs(mix).max():.2f}, RMS {rms:.1f} dB")
        with tempfile.TemporaryDirectory() as folder:
            wav = pathlib.Path(folder) / "mix.wav"
            wavfile.write(wav, RATE, (mix * 32767.0).astype(np.int16))
            subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", str(wav), "-codec:a", "aac", "-b:a", "192k",
                            "-metadata", f"title={song.name.replace('_', ' ')}", "-metadata", "artist=LEFTRIGHT",
                            str(out / f"{song.name}.m4a")], check=True)


if __name__ == "__main__":
    main()
