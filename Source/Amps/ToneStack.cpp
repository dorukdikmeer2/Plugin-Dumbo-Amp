#include "ToneStack.h"

namespace { constexpr double kPi = 3.14159265358979323846; }

//==============================================================================
void Biquad::setPeaking(double fc, double gainDB, double Q, double sr)
{
    fc = std::min(fc, 0.45 * sr);
    const double A  = std::pow(10.0, gainDB / 40.0);
    const double w0 = 2.0 * kPi * fc / sr;
    const double alpha = std::sin(w0) / (2.0 * Q);
    const double c = std::cos(w0);
    const double a0inv = 1.0 / (1.0 + alpha / A);
    b0 = (1.0 + alpha * A) * a0inv;
    b1 = -2.0 * c * a0inv;
    b2 = (1.0 - alpha * A) * a0inv;
    a1 = -2.0 * c * a0inv;
    a2 = (1.0 - alpha / A) * a0inv;
}

void Biquad::setHighShelf(double fc, double gainDB, double sr)
{
    fc = std::min(fc, 0.45 * sr);
    const double A  = std::pow(10.0, gainDB / 40.0);
    const double w0 = 2.0 * kPi * fc / sr;
    const double cw = std::cos(w0), sw = std::sin(w0);
    const double alpha = sw / 2.0 * std::sqrt(2.0);          // S = 1
    const double sq = 2.0 * std::sqrt(A) * alpha;
    const double a0 = (A + 1) - (A - 1) * cw + sq;
    b0 = A * ((A + 1) + (A - 1) * cw + sq) / a0;
    b1 = -2.0 * A * ((A - 1) + (A + 1) * cw) / a0;
    b2 = A * ((A + 1) + (A - 1) * cw - sq) / a0;
    a1 = 2.0 * ((A - 1) - (A + 1) * cw) / a0;
    a2 = ((A + 1) - (A - 1) * cw - sq) / a0;
}

//==============================================================================
void SixBandEQ::prepare(double sampleRate)
{
    sr = sampleRate;
    for (int i = 0; i < NUM_BANDS; ++i) filters[i].setPeaking(BAND_FREQS[i], gains[i] * MAX_GAIN_DB, BAND_Q, sr);
    reset();
}

void SixBandEQ::reset() { for (auto& f : filters) f.reset(); }

void SixBandEQ::setBandGain(int band, double n)
{
    if (band < 0 || band >= NUM_BANDS) return;
    n = std::min(std::max(n, -1.0), 1.0);
    if (std::abs(n - gains[band]) < 1e-5) return;
    gains[band] = n;
    filters[band].setPeaking(BAND_FREQS[band], n * MAX_GAIN_DB, BAND_Q, sr);
}

//==============================================================================
void TMBToneStack::prepare(double sampleRate) { sr = sampleRate; update(); reset(); }
void TMBToneStack::reset() { s1 = s2 = s3 = 0.0; }

void TMBToneStack::setControls(double b, double m, double t)
{
    b = std::min(std::max(b, 0.0), 1.0);
    m = std::min(std::max(m, 0.0), 1.0);
    t = std::min(std::max(t, 0.0), 1.0);
    if (std::abs(b - bass) < 1e-5 && std::abs(m - mid) < 1e-5 && std::abs(t - treble) < 1e-5) return;
    bass = b; mid = m; treble = t;
    update();
}

void TMBToneStack::update()
{
    // pot positions: log-taper treble/bass, linear mid
    const double tp = std::pow(treble, val.trebleTaper), lp = std::pow(bass, val.bassTaper), mp = mid;
    const double R1 = val.R_TREBLE, R2 = val.R_BASS, R3 = val.R_MID, R4 = val.R_SLOPE;
    const double C1 = val.C_TREBLE, C2 = val.C_BASS, C3 = val.C_MID;
    const double t = tp, l = lp, m = mp;

    const double b1 = t*C1*R1 + m*C3*R3 + l*(C1*R2 + C2*R2) + (C1*R3 + C2*R3);
    const double b2 = t*(C1*C2*R1*R4 + C1*C3*R1*R4) - m*m*(C1*C3*R3*R3 + C2*C3*R3*R3)
                    + m*(C1*C3*R1*R3 + C1*C3*R3*R3 + C2*C3*R3*R3)
                    + l*(C1*C2*R1*R2 + C1*C2*R2*R4 + C1*C3*R2*R4)
                    + l*m*(C1*C3*R2*R3 + C2*C3*R2*R3)
                    + (C1*C2*R1*R3 + C1*C2*R3*R4 + C1*C3*R3*R4);
    const double b3 = l*m*(C1*C2*C3*R1*R2*R3 + C1*C2*C3*R2*R3*R4)
                    - m*m*(C1*C2*C3*R1*R3*R4 + C1*C2*C3*R3*R3*R4)
                    + m*(C1*C2*C3*R1*R3*R3 + C1*C2*C3*R3*R3*R4)
                    + t*C1*C2*C3*R1*R3*R4 - t*m*C1*C2*C3*R1*R3*R4
                    + t*l*C1*C2*C3*R1*R2*R4;
    const double a1 = (C1*R1 + C1*R3 + C2*R3 + C2*R4 + C3*R4) + m*C3*R3 + l*(C1*R2 + C2*R2);
    const double a2 = m*(C1*C3*R1*R3 - C2*C3*R3*R4 + C1*C3*R3*R3 + C2*C3*R3*R3)
                    + l*m*(C1*C3*R2*R3 + C2*C3*R2*R3)
                    - m*m*(C1*C3*R3*R3 + C2*C3*R3*R3)
                    + l*(C1*C2*R2*R4 + C1*C2*R1*R2 + C1*C3*R2*R4 + C2*C3*R2*R4)
                    + (C1*C2*R1*R4 + C1*C3*R1*R4 + C1*C2*R3*R4 + C1*C2*R1*R3 + C1*C3*R3*R4 + C2*C3*R3*R4);
    const double a3 = l*m*(C1*C2*C3*R1*R2*R3 + C1*C2*C3*R2*R3*R4)
                    - m*m*(C1*C2*C3*R1*R3*R4 + C1*C2*C3*R3*R3*R4)
                    + m*(C1*C2*C3*R3*R3*R4 + C1*C2*C3*R1*R3*R3 - C1*C2*C3*R1*R3*R4)
                    + l*C1*C2*C3*R1*R2*R4 + C1*C2*C3*R1*R3*R4;

    // bilinear transform  s = c (1 - z^-1)/(1 + z^-1)
    const double c = 2.0 * sr;
    const double c2 = c * c, c3 = c2 * c;
    // P_k(z^-1) = (1 - z^-1)^k (1 + z^-1)^(3-k)
    static const double P[4][4] = { { 1, 3, 3, 1 }, { 1, 1, -1, -1 }, { 1, -1, -1, 1 }, { 1, -3, 3, -1 } };
    const double Bk[4] = { 0.0, b1 * c, b2 * c2, b3 * c3 };
    const double Ak[4] = { 1.0, a1 * c, a2 * c2, a3 * c3 };
    double B[4] = {}, A[4] = {};
    for (int k = 0; k < 4; ++k)
        for (int j = 0; j < 4; ++j) { B[j] += Bk[k] * P[k][j]; A[j] += Ak[k] * P[k][j]; }
    const double inv = 1.0 / A[0];
    for (int j = 0; j < 4; ++j) { nb[j] = B[j] * inv; na[j] = A[j] * inv; }
}

//==============================================================================
void PresenceEQ::setPresence(double v)
{
    v = std::min(std::max(v, 0.0), 1.0);
    f.setHighShelf(3200.0, -3.0 + v * 9.0, sr);        // -3 .. +6 dB, 5 = +1.5 dB
}
