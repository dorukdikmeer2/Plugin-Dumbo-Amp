#pragma once
#include <vector>
#include <cmath>
#include <cstddef>
#include <utility>

namespace cab {

//==============================================================================
// Small in-place radix-2 complex FFT (double precision).  Only used to *design*
// cabinet / microphone impulse responses (never per audio sample), so clarity
// matters more than speed.  All buffers are allocated in the constructor /
// resize(), so design calls do not allocate.
//==============================================================================
class Fft {
public:
    explicit Fft(int sizePow2 = 4096) { resize(sizePow2); }

    void resize(int n)
    {
        N = n;
        cosT.resize((size_t) N / 2);
        sinT.resize((size_t) N / 2);
        for (int i = 0; i < N / 2; ++i) {
            const double a = -2.0 * 3.14159265358979323846 * i / N;
            cosT[(size_t) i] = std::cos(a);
            sinT[(size_t) i] = std::sin(a);
        }
        rev.assign((size_t) N, 0);
        int bits = 0;
        while ((1 << bits) < N) ++bits;
        for (int i = 0; i < N; ++i) {
            int r = 0;
            for (int b = 0; b < bits; ++b) r |= ((i >> b) & 1) << (bits - 1 - b);
            rev[(size_t) i] = r;
        }
    }

    int size() const { return N; }

    // forward: X[k] = sum x[n] e^{-j 2 pi k n / N}
    void forward(double* re, double* im) const { transform(re, im, false); }
    // inverse includes the 1/N scale
    void inverse(double* re, double* im) const { transform(re, im, true); }

private:
    void transform(double* re, double* im, bool inv) const
    {
        for (int i = 0; i < N; ++i) {
            const int j = rev[(size_t) i];
            if (j > i) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
        }
        for (int len = 2; len <= N; len <<= 1) {
            const int half = len / 2, step = N / len;
            for (int i = 0; i < N; i += len) {
                for (int k = 0; k < half; ++k) {
                    const double wr = cosT[(size_t) (k * step)];
                    const double wi = inv ? -sinT[(size_t) (k * step)] : sinT[(size_t) (k * step)];
                    const int a = i + k, b = a + half;
                    const double tr = re[b] * wr - im[b] * wi;
                    const double ti = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - tr; im[b] = im[a] - ti;
                    re[a] += tr;        im[a] += ti;
                }
            }
        }
        if (inv) {
            const double s = 1.0 / N;
            for (int i = 0; i < N; ++i) { re[i] *= s; im[i] *= s; }
        }
    }

    int N = 0;
    std::vector<double> cosT, sinT;
    std::vector<int> rev;
};

} // namespace cab
