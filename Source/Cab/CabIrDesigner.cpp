#include "Cab/CabIrDesigner.h"
#include <array>
#include <complex>

namespace cab {
namespace {
constexpr double kPi = 3.14159265358979323846;

// ---- RBJ biquad evaluated analytically (magnitude only) -----------------------------------------------
struct Biq {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    void norm(double a0, double B0, double B1, double B2, double A1, double A2) { b0 = B0 / a0; b1 = B1 / a0; b2 = B2 / a0; a1 = A1 / a0; a2 = A2 / a0; }
    static Biq lowpass(double f, double Q, double sr) {
        Biq q; f = std::min(f, 0.45 * sr);
        const double w = 2 * kPi * f / sr, c = std::cos(w), al = std::sin(w) / (2 * Q);
        q.norm(1 + al, (1 - c) / 2, 1 - c, (1 - c) / 2, -2 * c, 1 - al); return q;
    }
    static Biq highpass(double f, double Q, double sr) {
        Biq q; const double w = 2 * kPi * f / sr, c = std::cos(w), al = std::sin(w) / (2 * Q);
        q.norm(1 + al, (1 + c) / 2, -(1 + c), (1 + c) / 2, -2 * c, 1 - al); return q;
    }
    static Biq peak(double f, double dB, double Q, double sr) {
        Biq q; f = std::min(f, 0.45 * sr);
        const double A = std::pow(10.0, dB / 40.0), w = 2 * kPi * f / sr, c = std::cos(w), al = std::sin(w) / (2 * Q);
        q.norm(1 + al / A, 1 + al * A, -2 * c, 1 - al * A, -2 * c, 1 - al / A); return q;
    }
    double mag(double w) const {
        const std::complex<double> z1 = std::polar(1.0, -w), z2 = std::polar(1.0, -2 * w);
        return std::abs(b0 + b1 * z1 + b2 * z2) / std::max(1e-12, std::abs(1.0 + a1 * z1 + a2 * z2));
    }
};

// Bessel J1 (Abramowitz & Stegun 9.4.4 / 9.4.6, |error| < 5e-8)
double besselJ1(double x)
{
    const double ax = std::fabs(x);
    if (ax < 3.0) {
        const double t = (x / 3.0) * (x / 3.0);
        return x * (0.5 + t * (-0.56249985 + t * (0.21093573 + t * (-0.03954289 + t * (0.00443319 + t * (-0.00031761 + t * 0.00001109))))));
    }
    const double t = 3.0 / ax;
    const double f1 = 0.79788456 + t * (0.00000156 + t * (0.01659667 + t * (0.00017105 + t * (-0.00249511 + t * (0.00113653 - t * 0.00020033)))));
    const double th = ax - 2.35619449 + t * (0.12499612 + t * (0.00005650 + t * (-0.00637879 + t * (0.00074348 + t * (0.00079824 - t * 0.00029166)))));
    const double j = std::cos(th) * f1 / std::sqrt(ax);
    return x < 0 ? -j : j;
}

// Directivity of a circular piston: |2 J1(z) / z|, with a floor so that cone break-up scatter fills the nulls
double pistonDirectivity(double z)
{
    if (std::fabs(z) < 1e-3) return 1.0;
    const double d = std::fabs(2.0 * besselJ1(z) / z);
    return std::sqrt(d * d + 0.012);
}
} // namespace

//------------------------------------------------------------------------------------------------------
MicGeometry CabIrDesigner::geometry(int micType, double position01, double distance01)
{
    const MicSpec& m = micSpec(micType);
    const double d = 0.02 * std::pow(60.0, std::min(std::max(distance01, 0.0), 1.0));      // 2 cm .. 1.2 m from the grille
    MicGeometry g;
    g.axial  = d + m.capsule + 0.015;                                                       // + grille-to-cone depth
    g.offset = std::min(std::max(position01, 0.0), 1.0) * 0.16;                             // centre .. 2 cm beyond a 12" cone edge
    return g;
}

void CabIrDesigner::prepare(double sampleRate, int irLength)
{
    sr = sampleRate;
    len = irLength;
    N = std::max(4096, 4 * irLength);
    bins = N / 2 + 1;
    fft.resize(N);
    reA.assign((size_t) N, 0.0); imA.assign((size_t) N, 0.0);
    reB.assign((size_t) N, 0.0); imB.assign((size_t) N, 0.0);
    buildTables();
}

double CabIrDesigner::speakerDb(int cabinet, double f) const
{
    const double w = 2 * kPi * f / sr;
    std::array<Biq, 7> s;
    if (cabinet == 0) {                                       // 4x12 closed back, Greenback-like
        s = { { Biq::highpass(78.0, 0.75, sr), Biq::peak(112.0, 5.0, 1.3, sr), Biq::peak(380.0, -2.5, 0.9, sr),
                Biq::peak(1900.0, 3.0, 0.9, sr), Biq::peak(3300.0, 2.0, 2.2, sr), Biq::lowpass(5200.0, 0.75, sr), Biq::lowpass(6800.0, 0.60, sr) } };
    } else {                                                  // 1x12 open back, Vintage-30-like
        s = { { Biq::highpass(92.0, 0.70, sr), Biq::peak(105.0, 3.5, 1.6, sr), Biq::peak(520.0, -2.0, 1.0, sr),
                Biq::peak(2700.0, 5.5, 1.1, sr), Biq::peak(4600.0, 3.0, 2.0, sr), Biq::lowpass(7600.0, 0.80, sr), Biq::lowpass(9800.0, 0.60, sr) } };
    }
    double m = 1.0;
    for (auto& b : s) m *= b.mag(w);
    return 20.0 * std::log10(std::max(m, 1e-9));
}

void CabIrDesigner::buildTables()
{
    freq.assign((size_t) bins, 0.0); radius.assign((size_t) bins, 0.0); kWave.assign((size_t) bins, 0.0); hfExp.assign((size_t) bins, 0.0);
    for (int c = 0; c < 2; ++c) spk[c].assign((size_t) bins, 0.0);
    for (int t = 0; t < kMicTypeCount; ++t) mic[t].assign((size_t) bins, 0.0);
    for (int k = 0; k < bins; ++k) {
        const double f = std::max(1.0, sr * k / N);
        freq[(size_t) k] = f;
        radius[(size_t) k] = std::min(0.14, std::max(0.03, 0.14 * std::pow(300.0 / f, 0.75)));   // radiating radius (m)
        kWave[(size_t) k] = 2 * kPi * f / SPEED_OF_SOUND;
        hfExp[(size_t) k] = std::min(3.0, std::pow(f / 8000.0, 1.2));                              // how the off-axis loss scales with f
        for (int c = 0; c < 2; ++c) spk[c][(size_t) k] = speakerDb(c, f);
        for (int t = 0; t < kMicTypeCount; ++t) mic[t][(size_t) k] = micFarFieldDb(micSpec(t), f);
    }
}

//------------------------------------------------------------------------------------------------------
void CabIrDesigner::design(const Request& rq, float* ir)
{
    const MicSpec& ms = micSpec(rq.micType);
    const int mt = std::min(std::max(rq.micType, 0), kMicTypeCount - 1);
    const int cb = std::min(std::max(rq.cabinet, 0), 1);
    const MicGeometry g = geometry(mt, rq.position, rq.distance);
    const double x = g.offset;

    // dust-cap resonance: only audible near the centre of the cone
    const double capStrength = std::exp(-(x / 0.05) * (x / 0.05));

    std::vector<double>& lm = reA;            // log magnitude, then reused as scratch
    std::vector<double>& zi = imA;

    // ---- 1. magnitude on the bin grid (dB -> natural log)
    std::vector<double>& magDb = reB;         // scratch (size N, first `bins` used)
    for (int k = 0; k < bins; ++k) {
        const double f = freq[(size_t) k], a = radius[(size_t) k], kw = kWave[(size_t) k];
        const double miss = std::max(0.0, x - 0.9 * a);                    // lateral distance beyond the radiating area
        const double th = std::atan2(miss, g.axial);
        const double ct = std::cos(th), st = std::sin(th);
        const double r  = std::sqrt(g.axial * g.axial + miss * miss);
        const double re = std::sqrt(r * r + 0.06 * 0.06);                  // extended-source effective distance (a 12" cone is not a point)

        // speaker radiation toward the mic
        const double ap = std::min(a, 0.06);
        const double dirDb = 20.0 * std::log10(pistonDirectivity(kw * ap * st));

        // microphone: pattern x off-axis HF loss x near-field (proximity) boost
        const double pat = ms.alpha + (1.0 - ms.alpha) * ct;
        const std::complex<double> gradient = (1.0 - ms.alpha) * ct * std::complex<double>(1.0, -1.0 / (kw * re));
        const double prox = std::abs(ms.alpha + gradient);                 // = pat at far field, boosted at low f
        const double proxDb = std::min(16.0, 20.0 * std::log10(std::max(prox, 1e-4) / std::max(pat, 1e-4)));
        const double patDb  = 20.0 * std::log10(std::max(pat, 0.03));
        const double offDb  = -ms.offAxisDb * (1.0 - ct) * hfExp[(size_t) k];

        double db = spk[cb][(size_t) k] + mic[mt][(size_t) k] + dirDb + patDb + proxDb + offDb;
        db += 4.5 * capStrength * std::exp(-0.5 * std::pow(std::log2(f / 3400.0) / 0.7, 2.0));   // dust-cap bump
        db += 1.2 * (1.0 - capStrength) * std::exp(-0.5 * std::pow(std::log2(f / 220.0) / 0.9, 2.0)); // edge: a little low-mid bloom
        magDb[(size_t) k] = db;
    }

    // ---- 2. minimum-phase spectrum from the magnitude (real cepstrum folding)
    for (int k = 0; k < bins; ++k) lm[(size_t) k] = magDb[(size_t) k] * 0.115129254649702;   // dB -> ln
    for (int k = 1; k < N / 2; ++k) lm[(size_t) (N - k)] = lm[(size_t) k];
    std::fill(zi.begin(), zi.end(), 0.0);
    fft.inverse(lm.data(), zi.data());                                                       // cepstrum (real, even)
    for (int n = N / 2 + 1; n < N; ++n) { lm[(size_t) n] = 0.0; zi[(size_t) n] = 0.0; }
    for (int n = 1; n < N / 2; ++n) lm[(size_t) n] *= 2.0;
    std::fill(zi.begin(), zi.end(), 0.0);
    fft.forward(lm.data(), zi.data());                                                       // complex log spectrum of the min-phase filter

    // ---- 3. complex spectrum: min-phase x (1 + room reflections) x arrival delay
    struct Refl { double extraPath; double gain; double hfHz; };
    const double hs = (cb == 0) ? 0.42 : 0.32;                                               // speaker centre height (m)
    const double d0 = g.axial;
    auto pathExtra = [&](double lateral) { return std::sqrt(d0 * d0 + lateral * lateral) - d0; };
    const double pf = std::sqrt(d0 * d0 + 4 * hs * hs), pc = std::sqrt(d0 * d0 + 4 * 2.0 * 2.0), pw = std::sqrt(d0 * d0 + 3.6 * 3.6);
    const Refl refl[4] = {
        { pathExtra(2 * hs),  0.65 * d0 / pf, 5000.0 },                                      // floor
        { pathExtra(4.0),     0.45 * d0 / pc, 4000.0 },                                      // ceiling
        { pathExtra(3.6),     0.50 * d0 / pw, 3500.0 },                                      // side wall
        { 2.0 * (3.5 - d0),   0.40 * d0 / (d0 + 2.0 * (3.5 - d0)), 3000.0 } };               // front wall
    for (int k = 0; k < bins; ++k) {
        const double w = 2 * kPi * freq[(size_t) k] / sr;
        std::complex<double> Hm = std::exp(std::complex<double>(lm[(size_t) k], zi[(size_t) k]));
        std::complex<double> R(1.0, 0.0);
        for (const Refl& rf : refl) {
            const double tau = rf.extraPath / SPEED_OF_SOUND * sr;                           // samples
            const std::complex<double> lp = 1.0 / std::complex<double>(1.0, freq[(size_t) k] / rf.hfHz);
            R += rf.gain * lp * std::polar(1.0, -w * tau);
        }
        std::complex<double> H = Hm * R * std::polar(1.0, -w * rq.delaySamples);
        reB[(size_t) k] = H.real(); imB[(size_t) k] = H.imag();
    }
    reB[0] = reB[0]; imB[0] = 0.0; imB[(size_t) (N / 2)] = 0.0;
    for (int k = 1; k < N / 2; ++k) { reB[(size_t) (N - k)] = reB[(size_t) k]; imB[(size_t) (N - k)] = -imB[(size_t) k]; }

    // ---- 4. back to the time domain, truncate and fade the tail
    fft.inverse(reB.data(), imB.data());
    const int fade = len / 8;
    std::fill(imB.begin(), imB.end(), 0.0);
    for (int i = 0; i < N; ++i) {
        double v = (i < len) ? reB[(size_t) i] : 0.0;
        if (i >= len - fade && i < len) v *= 0.5 * (1.0 + std::cos(kPi * (double) (i - (len - fade)) / (double) fade));   // raised-cosine tail
        reB[(size_t) i] = v;
    }

    // ---- 5. loudness normalisation measured on the FINAL (truncated) IR: guitar-weighted, i.e. a pink spectrum
    //         (1/f power weighting on the linear bin grid) over 80 Hz .. 6 kHz -> every setting has the same level
    fft.forward(reB.data(), imB.data());
    double num = 0.0, den = 0.0;
    for (int k = 1; k < bins; ++k) {
        const double f = freq[(size_t) k];
        if (f < 80.0 || f > 6000.0) continue;
        const double w = 1.0 / f;
        num += w * (reB[(size_t) k] * reB[(size_t) k] + imB[(size_t) k] * imB[(size_t) k]);
        den += w;
    }
    const double gain = 1.0 / std::sqrt(std::max(num / std::max(den, 1e-9), 1e-12));
    // the forward FFT may be unnormalised: compare against the known time-domain energy instead of trusting a scale
    // (see Fft.h: inverse() carries the 1/N, forward() does not, so |X[k]| is the true DTFT magnitude)
    for (int i = 0; i < len; ++i) ir[i] = 0.0f;
    fft.inverse(reB.data(), imB.data());                                                      // back to the truncated IR
    for (int i = 0; i < len; ++i) ir[i] = (float) (reB[(size_t) i] * gain);
}

} // namespace cab
