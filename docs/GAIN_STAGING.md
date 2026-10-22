# Gain staging and clipping

*Why this document exists:* the first version clipped when MASTER, CAB LEVEL or the output trim were raised.
This page explains the signal levels, what was changed, and how to re-run the proof.

## Level map (48 kHz, defaults: MASTER 5, CAB 0 dB, OUT 0 dB, normal guitar DI)

```
DI (jack)  -> IN TRIM -> [gate] -> amp model -> DC block -> cab (mic 1 + mic 2) -> CAB LEVEL -> OUT TRIM
           -> DC block -> look-ahead limiter (ceiling -3 dBFS, inter-sample aware) -> DC block -> hard clamp (never acts)
```

| Point | What happens there |
|---|---|
| Amp output | Calibrated (tests/calibrate.cpp -> Source/Amps/Calibration.h) so the *loudness* of every mode/channel follows its voicing target. Targets were lowered by ~6.5 dB against the first version, so a DI at -12 dBFS peaks at about -8 ... -25 dBFS at the output, depending on the channel and GAIN. |
| MASTER | The pot still feeds the power amp as drawn on the schematic. The real pot (x^2.5 taper) was a steep cliff for loudness, so a loudness law (`dsp_util::masterOutComp`, applied *after* the power stage) makes the level change smoothly: 0 -> mute, 1 -> -30 dB, 3 -> -11 dB, 5 -> 0 dB, 8 -> +6 dB, 10 -> +8 dB (re MASTER 5). It is a non-schematic addition. |
| CAB LEVEL, OUT TRIM | Pure gain (measured change equals the set change to within 0.1 dB). |
| Limiter | `Engine/Limiter.h`. 1 ms look-ahead, instant attack, 80 ms release, aims 0.9 dB below its -3 dBFS ceiling and also watches the INTER-SAMPLE peak (4-point interpolation between two samples). Below that level it is bit-transparent. Above it the gain is turned *down* smoothly before the peak, it never clips. The old `tanh` soft clipper is gone. |
| DC blockers | One before the limiter (so it sees the final waveform) and a very slow one after it (limiter gain changes on asymmetric waveforms can leave a few mV of DC). |
| Hard clamp | Last line of defence. The audit counts how often it has to cut by more than 0.1 dB: **0 times** in every test. |

The reported plugin latency is oversampler + limiter look-ahead (87 samples at 48 kHz).

## Revision 4: why the output is 3 dB lower and the ceiling is -3 dBFS
At the old settings a hot guitar DI reached the -1 dBFS limiter (about -1.9 dBFS peaks) and showed yellow/red on the meters, so
about 3-4 dB of OUT TRIM was needed. Now: every voice is 3 dB lower (`kAmpOutScale`, calibration targets in tests/calibrate.cpp
lowered by the same amount, so the voices keep their relative loudness), the limiter ceiling is -3 dBFS, and the limiter looks at
inter-sample peaks too (measured true peak of worst-case tones: <= -3.6 dBFS, tests/gainstage.cpp section G).
The OUT meter is green up to -8 dBFS, amber to -4 dBFS and red only above that (= the limiter is working).

Mic count / pan do not change loudness any more: the cab measures how the two mic IRs add in power (80 Hz - 2.5 kHz) and scales every
output side to the loudness of its loudest mic (tests/cabmic.cpp section 7: 50 cab x mic combinations, MIC 2 on/off,
polarity, pan: -0.9 .. +0.6 dB; hot setting: no combination clips).

## What was measured (tests/gainstage.cpp, 797 checks)

* **A.** Default headroom: 6 voicings x GAIN 1/5/10 x three inputs: peaks between -28 and -6 dBFS, 0 dB limiter action.
* **B.** MASTER sweep 0..10 (GAIN 5 and 10): monotonic, mute at 0, no step larger than 12.5 dB between neighbouring positions.
* **C/D.** CAB LEVEL and OUT TRIM -12..+12 dB: measured change = set change (+-0.1 dB until the limiter engages, at about +9 to +12 dB).
* **E.** IN TRIM -12..+24 dB: the amp compresses (Lead/Ultra change only 1-5 dB over 36 dB of trim) instead of overloading.
* **F.** Everything up at once (MASTER 10, CAB +12, OUT +12, GAIN 10, DI -3 dBFS, i.e. ~24 dB over): the limiter pulls
  19-24 dB, output peak stays at or below -3.2 dBFS, 0 clamp hits, DC < 1 mV.
* **G.** Limiter quality on a 220 Hz sine, 6 dB over the ceiling: THD 0.77 % (the old clipper: 19.8 %); 12 dB over: 1.7 % (33 %).
  Below the ceiling the output equals the delayed input exactly.
* **H.** Zipper / click test: sweeps of MASTER, CAB LEVEL, OUT TRIM, IN TRIM, GAIN, mic level / position / distance / pan
  during a held note. The worst 1 ms HF burst is 1.0-1.5x the steady-state value (no clicks).
* **I.** Idle with everything up: -140 dBFS (silence).

Run it yourself: `cd tests && make build/gainstage && build/gainstage`.

## What it cannot tell you
It is measurement, not hearing. The loudness law and the calibration targets are *my* choices; if a mode feels too
loud or too quiet relative to another, change the targets in `tests/calibrate.cpp` and run `tests/calibrate.sh`.
