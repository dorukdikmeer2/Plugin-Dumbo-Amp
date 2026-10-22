# DumbDock Amp - voicing notes (2026-10 rebuild, revision 2)

## What was studied and what it changed
| Source of ideas | What was taken |
|---|---|
| Soldano SLO-100 (4 gain stages, interstage attenuators with treble-peaker caps, 470 k grid stoppers, cold clipper, tone stack AFTER the distortion) | AMP-001 Lead / Ultra: X1/X2 stages behind 2-resistor attenuators with treble peaker, big grid stoppers (smooth, blocking-free clipping), cold-biased clipper cathodes, Marshall-style Bass/Mid/Treble after the gain stages |
| Peavey 5150 (small coupling caps = tight low end, LTP phase inverter, strong NFB) | small coupling caps / 470 k leaks in the cascaded stages, pre-V1b tightening that follows GAIN, LTP PI kept from the schematic |
| Victory Kraken / Mark-series | mid-focused V1b bypass in the high-gain modes, big headroom clean mode with separate drive stages |
| Digital modelling literature (aliasing, grid-current asymmetry) | 4x oversampling (measured: non-harmonic fold-back -18 dB at 1x, -30 dB at 2x, -47 dB at 4x on the Ultra mode), asymmetric grid-current clipping, band-limiting between stages |
| Laney VH100R and Roland JC-120 (clean headroom) | Dock channel A: input pad, unbypassed low-gain V1a, 100 W-class power section (high swing, almost no sag), scooped glassy stack, tight bass |
| Vox AC30 top boost / The Edge (edge of breakup) | Dock channel B: bright bypassed V1a, brit stack, hot-biased sagging power section; GAIN walks from clean through chime to a hairy edge-of-breakup |

## Cause of the old "harsh / gated / broken" gain (and the fixes)
- Single hard-driven stages with small grid stoppers charged their coupling caps (blocking distortion): the waveform
  showed pulses with long gaps. Fix: big grid stoppers + attenuators between stages, smaller leaks, per-mode coupling caps.
- Everything saturated in the power stage's sharp-knee limiter. Fix: level calibration puts the power stage back
  in its linear range, the preamp does the clipping.
- 2x oversampling left fold-back products. Fix: 4x.
- Tests now fail if a decaying note drops faster than the input (gating), if the tail of a loud burst comes back wrong
  (blocking), if the output is flat-topped, if >6 kHz energy is high, or if THD falls as GAIN rises.

## Dock reverb / Dumb reverb
Room: 8-line feedback delay network, Householder mixing, modulated lines, damping, 20 ms pre-delay, ~2 s tail.
Spring: three dispersive transmission lines (64 all-pass sections each) in a feedback loop, ~1.6 s tail with the chirp.
Both are normalised so the wet level at REVERB 10 is about 1 dB below the dry signal; the knob law is x^1.1.

## Added (non-schematic) parts, for the record
AMP-001: Bass/Mid/Treble stack (3 params), X1/X2 stages, interstage attenuators/peakers/low-passes, pad, tightening
depth, V1b bypass, fizz filter, per-mode tone stack values, power-stage drive calibration.
AMP-002: input pad, per-channel stack values, per-channel power-section settings, bleed, tightening, fizz filter.
Both: 4x oversampling, new reverbs, CAB OFF, output DC blocker.

## Revision 2 (per-channel controls, gain staging, cabinets)
* Every channel has its own GAIN / BASS / MID / TREBLE (Dumb 4 sets, Dock 2 sets). The engine follows the active channel and snaps on a change.
* Levels were lowered by ~6.5 dB and a loudness law was added after the power stage (docs/GAIN_STAGING.md); the tanh soft clipper was replaced by a look-ahead limiter.
* Dock channel B (Chime): +3.5 dB make-up after the tone stack (non schematic) drives V1b harder (about 3.5 dB more drive; the loudness
  calibration was redone). Measured at GAIN 10: 13 % THD at -26 dBFS input, 52 % at -16.5 dBFS, 79 % at -8 dBFS (tests/voicing.cpp).
* Dumb noise gate: downward expander on the DI before the amp, knob 0 = off, knob 1 -> -76 dBFS threshold ... 10 -> -34 dBFS, ratio 1:3, 6 dB soft knee,
  -55 dB floor, 1 ms open, 45 ms hold, 90 ms close. Non schematic.
* The six-band EQ of the Dumb amp is presented as six vertical sliders (+-15 dB); the DSP did not change.
* Cabinet / microphone model replaced (docs/CAB_AND_MICS.md).

## Revision 3 (non-schematic additions, for the record)
* Tone contour (Source/Engine/ToneContour.h): after the amp, low shelf 120 Hz, bell 700 Hz (Q 0.6), high shelf 3.2 kHz, +-9 dB at knob 0 / 10, exactly flat at 5.
  Measured total BASS/MID/TREBLE range per channel: 15 to 36 dB (tests/features.cpp section 5).
* Cab MIC PAN is a balance law (the louder side stays at unity, the other side is reduced); cab level is the CAB LVL knob only.
* Dumb EQ BYPASS: all six bands flat, bit-identical to zero settings, click-free (tests/features.cpp section 6).
* Dumb Crunch: lower pad, darker interstage / fizz filters; fizz above 6 kHz stays flat to GAIN 6 (-45 dB) and only rises at 9-10.
* Dock: output about +6 dB; the safety limiter now works a little more on very hot input at maximum GAIN (<= 2 dB on a -3 dBFS DI).
* Presets: user presets are .ddpreset files in the user application-data folder (DorukAudio/DumbDockAmp/Presets).

## Revision 4 (non-schematic additions, for the record)
* GAIN knob law per channel (`ModeCfg::gA/gB`, `ChannelCfg::gA/gB`): pot position seen by the circuit = gA + gB x knob. Crunch 4.4 + 0.36 k,
  Lead -0.3 + 0.95 k, Ultra 0.1 + 0.95 k, Chime 5.6 + 0.37 k; Clean and Pristine 0 + 1 k (unchanged). Lead / Ultra interstage
  attenuators raised (+2.3 / +1.4 dB per stage) for more gain.
  THD at 0.15 FS over GAIN 1..10: Crunch 1.7 -> 28 %, Chime 1.6 -> 40 %, Lead 9.8 -> 32 %, Ultra 21 -> 27 % (tests/voicing.cpp).
* Reference reading for the Crunch / Chime voicing (qualitative only, the pages give no gain figures): Archetype Plini and Rabea
  describe their Crunch as the mid-point between clean and high gain, a master-volume rock amp that stays clean at low gain and gets
  more aggressive as gain rises (Sound On Sound, Produce Like A Pro). That is the behaviour the new Crunch / Chime sweeps follow.
  I found no concrete rig or tone data for TOSKA.
* Output scale -3 dB, limiter ceiling -3 dBFS, inter-sample peak detection, mic-pair loudness law (see docs/GAIN_STAGING.md).

## Not verified
No listening test was possible while building this. Sample-rate behaviour, CPU on your machine, Windows/macOS
binaries and the real feel under your fingers are untested. All numbers in tests/ are proxies.
