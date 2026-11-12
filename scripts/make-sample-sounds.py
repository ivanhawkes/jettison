#!/usr/bin/env python3
# Rebuilds the sample sounds in assets/audio, synthesised here so that nothing in them is anyone
# else's work: the four sounds the sample game plays for its events (8c), a station's hum and a
# music loop. The effects are written as WAV straight into assets/audio; the music is written as WAV
# to the current directory and encoded with the reference FLAC encoder, which devenv does not carry:
#
#   nix shell nixpkgs#flac
#   python3 scripts/make-sample-sounds.py
#   flac --best --force --silent -o assets/audio/music/drift.flac drift.wav
#
# The effects are mono, as a positioned sound must be for miniaudio to place it, and the music is
# stereo. Everything is 48 kHz and 16-bit. The seed is fixed: the same script makes the same sounds.
import math, os, random, struct, wave

RATE = 48000
random.seed(11)
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'assets', 'audio')

def write(path, channels, frames):
    """frames: a list of floats (mono) or of (left, right) pairs, in [-1, 1]."""
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    data = bytearray()
    for frame in frames:
        for sample in (frame if channels == 2 else (frame,)):
            data += struct.pack('<h', int(round(max(-1.0, min(1.0, sample)) * 32767)))
    with wave.open(path, 'wb') as out:
        out.setnchannels(channels)
        out.setsampwidth(2)
        out.setframerate(RATE)
        out.writeframes(bytes(data))

def normalise(samples, peak):
    loudest = max(abs(s) for s in samples) or 1.0
    return [s * peak / loudest for s in samples]

def fade_ends(samples, seconds=0.004):
    """A few milliseconds of fade at each end, so a sound starts and stops without a click."""
    n = int(seconds * RATE)
    for i in range(min(n, len(samples))):
        samples[i] *= i / n
        samples[-1 - i] *= i / n
    return samples

def lowpass(samples, cutoff):
    """One-pole low-pass, enough to turn white noise into a hiss or a rumble."""
    a = 1.0 - math.exp(-2.0 * math.pi * cutoff / RATE)
    out, y = [], 0.0
    for s in samples:
        y += a * (s - y)
        out.append(y)
    return out

def pluck(frequency, seconds, decay, partials=((1, 1.0), (2, 0.35), (3, 0.12))):
    """A struck tone: a few harmonics under an exponential decay."""
    n = int(seconds * RATE)
    return [sum(level * math.sin(2 * math.pi * frequency * k * i / RATE) for k, level in partials)
            * math.exp(-i / (decay * RATE)) for i in range(n)]

def mix_into(target, source, at_seconds, gain=1.0):
    start = int(at_seconds * RATE)
    for i, s in enumerate(source):
        if start + i < len(target):
            target[start + i] += s * gain

# A filled trade: two notes rising a fifth, bright and short, on the UI bus.
filled = [0.0] * int(0.45 * RATE)
mix_into(filled, pluck(659.26, 0.3, 0.08), 0.0)
mix_into(filled, pluck(987.77, 0.35, 0.10), 0.09)
write(os.path.join(ROOT, 'ui', 'trade-filled.wav'), 1, fade_ends(normalise(filled, 0.7)))

# A refused trade: two low notes falling a semitone, buzzier (odd harmonics) and duller.
odd = ((1, 1.0), (3, 0.33), (5, 0.2), (7, 0.14))
refused = [0.0] * int(0.4 * RATE)
mix_into(refused, pluck(233.08, 0.16, 0.06, odd), 0.0)
mix_into(refused, pluck(220.00, 0.24, 0.08, odd), 0.14)
write(os.path.join(ROOT, 'ui', 'trade-refused.wav'), 1,
      fade_ends(normalise(lowpass(refused, 2400), 0.6)))

def clang(seconds, base):
    """A struck metal plate: inharmonic partials, each ringing for its own time."""
    n = int(seconds * RATE)
    modes = ((1.0, 1.0, 0.35), (2.76, 0.5, 0.22), (5.40, 0.3, 0.12), (8.93, 0.15, 0.07))
    return [sum(level * math.sin(2 * math.pi * base * ratio * i / RATE) * math.exp(-i / (ring * RATE))
                for ratio, level, ring in modes) for i in range(n)]

def noise(seconds):
    return [random.uniform(-1.0, 1.0) for _ in range(int(seconds * RATE))]

# Docking: the clamps close. A falling thump, a burst of rumble, and the clamp ringing.
docked = [0.0] * int(1.1 * RATE)
thump = [math.sin(2 * math.pi * (70 - 30 * i / RATE) * i / RATE) * math.exp(-i / (0.12 * RATE))
         for i in range(int(0.5 * RATE))]
mix_into(docked, thump, 0.0, 1.0)
rumble = [s * math.exp(-i / (0.08 * RATE)) for i, s in enumerate(lowpass(noise(0.4), 300))]
mix_into(docked, normalise(rumble, 0.8), 0.0)
mix_into(docked, clang(1.0, 410.0), 0.02, 0.45)
mix_into(docked, clang(0.8, 433.0), 0.11, 0.3)
write(os.path.join(ROOT, 'effects', 'docked.wav'), 1, fade_ends(normalise(docked, 0.8)))

# Undocking: the clamps let go with a clank, and air hisses out of the seal as the ship pulls away.
undocked = [0.0] * int(1.6 * RATE)
mix_into(undocked, clang(0.6, 520.0), 0.0, 0.5)
hiss_length = 1.4
hiss = lowpass(noise(hiss_length), 3500)
hiss = [s * min(1.0, i / (0.15 * RATE)) * math.exp(-i / (0.5 * RATE)) for i, s in enumerate(hiss)]
mix_into(undocked, normalise(hiss, 0.6), 0.08)
write(os.path.join(ROOT, 'effects', 'undocked.wav'), 1, fade_ends(normalise(undocked, 0.8)))

# Music: "Drift", a loop of slow pads over a held bass, with sparse bell notes that echo. The loop is
# rendered into a circular buffer, so what rings past the end comes in at the start and the loop
# seams without a gap. Am, Fmaj7, C, G, eight seconds each.
BAR = 8.0
LENGTH = int(4 * BAR * RATE)
left = [0.0] * LENGTH
right = [0.0] * LENGTH

def add(buffer, start, samples, gain):
    for i, s in enumerate(samples):
        buffer[(start + i) % LENGTH] += s * gain

def midi(note):
    return 440.0 * 2 ** ((note - 69) / 12)

def pad(frequency, seconds, detune):
    """Two detuned sines under a slow swell, so a chord breathes rather than starts."""
    n = int(seconds * RATE)
    attack, release = 2.5 * RATE, 3.0 * RATE
    out = []
    for i in range(n):
        envelope = min(1.0, i / attack) * min(1.0, (n - i) / release)
        t = i / RATE
        out.append(envelope * (math.sin(2 * math.pi * frequency * t)
                               + 0.6 * math.sin(2 * math.pi * frequency * (1 + detune) * t)
                               + 0.15 * math.sin(2 * math.pi * frequency * 2 * t)))
    return out

chords = ((57, (69, 72, 76)), (53, (65, 69, 72, 76)), (48, (67, 72, 76)), (55, (67, 71, 74)))
for bar, (bass, notes) in enumerate(chords):
    start = int(bar * BAR * RATE)
    # Each chord lasts a little longer than its bar, so it fades under the next one.
    for k, note in enumerate(notes):
        tone = pad(midi(note), BAR + 3.0, 0.003 + 0.001 * k)
        pan = 0.3 + 0.4 * (k / max(1, len(notes) - 1))
        add(left, start, tone, 0.07 * (1 - pan))
        add(right, start, tone, 0.07 * pan)
    low = pad(midi(bass - 12), BAR + 3.0, 0.0015)
    add(left, start, low, 0.11)
    add(right, start, low, 0.11)

# Bells: a few notes of the chord in each bar, at seeded times, each echoing twice across the field.
for bar, (_, notes) in enumerate(chords):
    for _ in range(3):
        at = bar * BAR + random.uniform(0.5, BAR - 1.0)
        note = random.choice(notes) + 12
        bell = pluck(midi(note), 2.5, 0.6, ((1, 1.0), (2.01, 0.2), (3.98, 0.05)))
        pan = random.uniform(0.2, 0.8)
        for echo, (delay, gain) in enumerate(((0.0, 0.05), (0.375, 0.025), (0.75, 0.012))):
            side = pan if echo % 2 == 0 else 1 - pan
            start = int((at + delay) * RATE)
            add(left, start, bell, gain * (1 - side))
            add(right, start, bell, gain * side)

peak = max(max(abs(s) for s in left), max(abs(s) for s in right))
write('drift.wav', 2, [(l * 0.7 / peak, r * 0.7 / peak) for l, r in zip(left, right)])

# A station's hum, looped while the ship is near: a low drone of machinery, beating slowly, over a
# rumble of air handling. Four seconds, and every partial a whole number of cycles in them, so the
# loop seams; the rumble is filtered over two copies of its noise and the second kept, so the filter
# is already running where the loop starts again. Made last, so the sounds above draw the same noise
# from the seed as they did before it was added.
HUM = 4.0
n = int(HUM * RATE)
drone = [0.0] * n
for frequency, level in ((55.0, 1.0), (55.25, 0.6), (110.0, 0.5), (165.0, 0.25), (220.5, 0.1)):
    assert abs(frequency * HUM - round(frequency * HUM)) < 1e-9
    for i in range(n):
        drone[i] += level * math.sin(2 * math.pi * frequency * i / RATE)
air = noise(HUM)
air = lowpass(air + air, 180)[n:]
hum = [d + 2.5 * a for d, a in zip(drone, normalise(air, 0.25))]
write(os.path.join(ROOT, 'effects', 'station-hum.wav'), 1, normalise(hum, 0.5))

print('Wrote the effects to', os.path.normpath(ROOT), 'and drift.wav here; encode it with flac.')
