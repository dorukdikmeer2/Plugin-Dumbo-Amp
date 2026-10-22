# Cabinets and microphones

Two cabinets (CAB A 4x12 closed back "Greenback"-style, CAB B 1x12 open back "Vintage 30"-style), each miked with
one or two microphones. Every cabinet remembers its **own** microphone setup and level.

## Signal path
```
mono in -> convolution with an impulse response per mic (rendered on the fly) -> mic level, pan, polarity
        -> sum -> CAB LEVEL -> stereo out
```
`Cab/CabIrDesigner.cpp` synthesises each impulse response from physics, `Cab/CabinetSim.cpp` runs it.

## The model of one microphone at one position
`H(f) = speaker(f) x cone directivity x mic far-field curve x polar pattern x proximity x room reflections`

1. **Speaker**: a handful of biquads per cabinet (high-pass, cone resonance, upper-mid bumps, low-pass).
2. **Position (0 = dust-cap centre ... 1 = cone edge)**: the mic is `position x 16 cm` off axis. The cone is treated as a
   circular piston with a frequency-dependent radiating radius (the cone only radiates from its centre at high
   frequencies); its directivity `|2 J1(ka sin t)/(ka sin t)|` removes treble as the mic moves out. A dust-cap resonance
   (+4.5 dB near 3.4 kHz) only exists near the centre. Measured: centre to edge changes the 3-6k vs 250-1k tilt by >= 8 dB
   and the 6-12k band by >= 8 dB on all five mics and both cabs.
3. **Distance (0 = 2 cm ... 1 = 1.2 m)**: proximity effect from the mic's gradient term
   `|a + (1-a) cos t (1 - j/(k r))|` (a = 0.5 cardioid, 0 figure-8; effective r never below 6 cm, boost capped at +16 dB),
   plus four early reflections (floor, ceiling, side wall, front wall; stronger the farther the mic is), which also give
   comb filtering. Close mic: more bass, tighter. Far mic: thinner bass, more room.
4. **Mic**: far-field response curve, polar pattern, off-axis HF loss, capsule size (see table).
5. **Two mics**: the relative arrival time between the mics is built into the impulse responses as a fractional delay
   (so moving mic 2 away produces real comb filtering when summed with mic 1). Polarity invert, level and pan per mic (pan is a balance law: it never makes the cab louder; use CAB LVL for loudness).
6. **Loudness normalisation**: every impulse response is scaled so a pink-noise guitar-band (80 Hz - 6 kHz) signal has the
   same level, measured on the *final truncated* IR (+-0.13 dB over 200 test settings). Moving a knob changes the tone, not the volume.

## The five microphones
They are **clones in the sense of a modelled character**, not measurements of real microphones. The curves were drawn
from memory of the published response charts and general descriptions, then checked by the audit for the documented
relationships. Brand names belong to their owners; this project is not affiliated with them.

| Mic | Pattern | Character in the model |
|---|---|---|
| SM57 clone | cardioid, 8 mm capsule offset | Bass roll-off, broad 3-7 kHz presence peak, steep top above 10 kHz: tight, mid-forward |
| SM58 clone | cardioid, 16 mm | Fuller low mids, softer bite (presence peak 2 dB lower), a bit more air |
| Royer 121 clone | figure-8 (strong proximity effect) | Flat, smooth, dark: gentle roll-off from 7 kHz, big low end close up |
| AKG 414 clone | cardioid, 22 mm | Extended and open: +3 dB lift at 5 kHz and a 8-14 kHz air shelf |
| MD 421 clone | cardioid, 14 mm | Very extended bass, a low-mid scoop (350-700 Hz), punchy 4-6 kHz |

## What was tested (tests/cabmic.cpp, 123 checks)
All impulse responses finite and decayed; level match +-0.13 dB; position effectiveness; proximity effect (close vs far
low end); room energy above 1 kHz grows with distance; comb ripple grows with distance; the five mics are audibly
different from each other (pairwise RMS difference >= 1.6 dB, 1.0 for SM57/SM58); documented relationships (57 brighter
than 58 and Royer, Royer more bass and darker than 414, 414 has more air above 8 kHz, 421 more bass and a low-mid scoop);
mic-pair delay accurate to a fraction of a sample; polarity inversion cancels; panning isolates a side; each cab
keeps its own settings; no clicks when mics, positions or cabs are switched (512-sample morph); switching mic 2 on
changes the level by only +1.2 ... +2.6 dB (the pair is normalised by the sum of the levels).

## CPU
Impulse response length 1024 samples (up to 52 kHz) / 2048 above. One convolution per active mic (one while knobs are
moved, thanks to a morphing IR). Linux sandbox, 48 kHz, one core: amp + 1 mic 16 %, + dragging mic knobs 19 %,
2 mics 17 %, 2 mics + dragging 26 %. IR re-rendering is limited to once per 30 ms.

## Not verified
Nobody has listened to this. The physics gets the *direction and size* of the changes right (measured), but the absolute
taste of "SM57 at position 0.3, distance 0.25" is yours to judge.
