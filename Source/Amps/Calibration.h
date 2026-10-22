#pragma once
//==============================================================================
// Level make-up tables (dB, per GAIN knob position 0..10) measured by tests/calibrate.cpp so that the
// loudness curve of every mode/channel follows its voicing target.  Generated: run tests/calibrate
// after changing a model, never edit by hand (the plugin only reads these tables).
//==============================================================================
namespace calib {
// AMP-001, rows = SW1 position 0..3
inline double a1Comp[4][11] = {
    { 11.99, 11.99, 9.51, 6.84, 4.02, 1.06, -1.98, -4.94, -7.63, -9.79, -10.70 },
    { -10.54, -10.54, -11.28, -11.97, -12.59, -13.09, -13.40, -13.38, -12.94, -12.14, -11.08 },
    { -10.30, -10.30, -10.48, -11.59, -13.32, -14.56, -14.94, -14.14, -12.40, -11.61, -11.69 },
    { -15.60, -15.60, -14.66, -14.03, -13.64, -13.63, -13.70, -13.95, -12.52, -11.82, -11.68 } };
// AMP-002, channel A / channel B
inline double a2CompA[11] = { 30.08, 30.08, 28.27, 26.46, 24.64, 22.82, 21.00, 19.20, 17.40, 14.14, 10.76 };
inline double a2CompB[11] = { -19.27, -19.27, -19.49, -19.67, -19.81, -19.85, -19.76, -19.88, -20.75, -21.52, -22.18 };
}
