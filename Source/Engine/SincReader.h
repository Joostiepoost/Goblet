// SincReader.h — reads a sample at any fractional position.
//
//   y(p) = sum_k x[k] h(p - k) / sum_k h(p - k)
//   h(t) = c * g(c * t),   c = min(1, 1/r)
//
// r is the read ratio (source samples per output sample). Faster than x1
// (r > 1) the kernel is stretched by 1/c: the cutoff drops to 1/r of the
// file's Nyquist and the number of taps grows to N * r, so the stopband
// attenuation is the same at every speed. The factor c cancels in the
// normalisation, so only g(c * |t|) is computed.
//
// Up to x1 the weights come from four neighbouring polyphase rows (contiguous
// memory, vectorised); faster, from the compact Hermite table. Sums run in
// double precision with four partial sums. The reader keeps no state
// between calls: its output depends only on the position and the source.

#pragma once

#include "SincKernel.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace goblet
{

class SincReader
{
public:
    // maxRatio: the largest read ratio that will be used (speed x file rate /
    // device rate). Allocates the weight buffers; call off the audio thread.
    void prepare (double maxRatio, int maxHalfWidth = 384)
    {
        const double hw = (double) maxHalfWidth * std::max (1.0, maxRatio);
        weights.assign ((size_t) (2.0 * hw) + 16, 0.0);
    }

    // Reads numChannels values at position p (in source samples) into out.
    // Samples outside [0, length) count as 0.
    void read (const SincKernel& kernel, const float* const* data, int numChannels,
               int64_t length, double p, double ratio, double* out) noexcept
    {
        if (ratio <= 1.0) readPolyphase (kernel, data, numChannels, length, p, out);
        else              readStretched (kernel, data, numChannels, length, p, 1.0 / ratio, out);
    }

private:
    static double dot (const double* w, const float* x, int n) noexcept
    {
        double a0 = 0.0, a1 = 0.0, a2 = 0.0, a3 = 0.0;
        int j = 0;
        for (; j + 4 <= n; j += 4)
        {
            a0 += w[j]     * (double) x[j];
            a1 += w[j + 1] * (double) x[j + 1];
            a2 += w[j + 2] * (double) x[j + 2];
            a3 += w[j + 3] * (double) x[j + 3];
        }
        for (; j < n; ++j) a0 += w[j] * (double) x[j];
        return (a0 + a1) + (a2 + a3);
    }

    // Both channels in one pass: each weight is loaded once.
    static void dot2 (const double* w, const float* x0, const float* x1, int n, double& r0, double& r1) noexcept
    {
        double a0 = 0.0, a1 = 0.0, b0 = 0.0, b1 = 0.0;
        int j = 0;
        for (; j + 2 <= n; j += 2)
        {
            a0 += w[j]     * (double) x0[j];
            b0 += w[j]     * (double) x1[j];
            a1 += w[j + 1] * (double) x0[j + 1];
            b1 += w[j + 1] * (double) x1[j + 1];
        }
        for (; j < n; ++j) { a0 += w[j] * (double) x0[j]; b0 += w[j] * (double) x1[j]; }
        r0 = a0 + a1;
        r1 = b0 + b1;
    }

    void readPolyphase (const SincKernel& kernel, const float* const* data, int numChannels,
                        int64_t length, double p, double* out) noexcept
    {
        const int K = kernel.halfWidth();
        const int L = kernel.phases();
        const double fl = std::floor (p);
        const auto n = (int64_t) fl;
        const double f = p - fl;

        // Taps at or before p: |t| = f + m, sample n - m.  After p: |t| = (1 - f) + m, sample n + 1 + m.
        // Each side: phase position x = |t| * L between rows i and i+1, combined from
        // rows i-1 .. i+2 with 4-point Lagrange weights.
        auto lagrange = [] (double t, double* c) noexcept
        {
            c[0] = -t * (t - 1.0) * (t - 2.0) / 6.0;
            c[1] = (t + 1.0) * (t - 1.0) * (t - 2.0) * 0.5;
            c[2] = -(t + 1.0) * t * (t - 2.0) * 0.5;
            c[3] = (t + 1.0) * t * (t - 1.0) / 6.0;
        };

        const double xp = f * (double) L;
        const int ip = std::min ((int) xp, L - 1);
        double cp[4];
        lagrange (xp - (double) ip, cp);

        const double xn = (1.0 - f) * (double) L;
        const int in = std::min ((int) xn, L);
        double cn[4];
        lagrange (xn - (double) in, cn);

        // Weights in tap order: w[j] belongs to sample n - K + 1 + j, j = 0 .. 2K-1.
        double* w = weights.data();
        {
            const double* r0 = kernel.row (ip - 1);
            const double* r1 = kernel.row (ip);
            const double* r2 = kernel.row (ip + 1);
            const double* r3 = kernel.row (ip + 2);
            double* wPos = w + (K - 1);            // wPos[-m] = weight of sample n - m
            for (int m = 0; m < K; ++m)
                wPos[-m] = cp[0] * r0[m] + cp[1] * r1[m] + cp[2] * r2[m] + cp[3] * r3[m];
        }
        {
            const double* r0 = kernel.row (in - 1);
            const double* r1 = kernel.row (in);
            const double* r2 = kernel.row (in + 1);
            const double* r3 = kernel.row (in + 2);
            double* wNeg = w + K;                  // wNeg[m] = weight of sample n + 1 + m
            for (int m = 0; m < K; ++m)
                wNeg[m] = cn[0] * r0[m] + cn[1] * r1[m] + cn[2] * r2[m] + cn[3] * r3[m];
        }

        const double sum = cp[0] * kernel.rowSum (ip - 1) + cp[1] * kernel.rowSum (ip) + cp[2] * kernel.rowSum (ip + 1) + cp[3] * kernel.rowSum (ip + 2)
                         + cn[0] * kernel.rowSum (in - 1) + cn[1] * kernel.rowSum (in) + cn[2] * kernel.rowSum (in + 1) + cn[3] * kernel.rowSum (in + 2);
        const double norm = sum > 0.0 ? 1.0 / sum : 0.0;

        // Overlap with the source.
        const int64_t kLo = n - K + 1;
        const int jStart = kLo < 0 ? (int) (-kLo) : 0;
        const int jEnd = (int) std::min ((int64_t) (2 * K), length - kLo);
        finish (w, jStart, jEnd, kLo, data, numChannels, norm, out);
    }

    static void finish (const double* w, int jStart, int jEnd, int64_t kLo, const float* const* data,
                        int numChannels, double norm, double* out) noexcept
    {
        if (jEnd <= jStart)
        {
            for (int ch = 0; ch < numChannels; ++ch) out[ch] = 0.0;
            return;
        }

        if (numChannels == 2)
        {
            double r0, r1;
            dot2 (w + jStart, data[0] + (kLo + jStart), data[1] + (kLo + jStart), jEnd - jStart, r0, r1);
            out[0] = r0 * norm;
            out[1] = r1 * norm;
            return;
        }

        for (int ch = 0; ch < numChannels; ++ch)
            out[ch] = dot (w + jStart, data[ch] + (kLo + jStart), jEnd - jStart) * norm;
    }

    void readStretched (const SincKernel& kernel, const float* const* data, int numChannels,
                        int64_t length, double p, double c, double* out) noexcept
    {
        const double halfWidth = (double) kernel.halfWidth() / c;

        int64_t kLo = (int64_t) std::floor (p - halfWidth) + 1;
        int64_t kHi = (int64_t) std::ceil (p + halfWidth) - 1;
        int count = (int) (kHi - kLo + 1);

        if (count > (int) weights.size())   // ratio beyond what prepare() allowed: clip the kernel
        {
            const int excess = (count - (int) weights.size()) / 2 + 1;
            kLo += excess; kHi -= excess;
            count = (int) (kHi - kLo + 1);
        }

        double* w = weights.data();
        const double* h = kernel.hermiteTable();
        const int limit = kernel.hermiteLimit();
        const double scale = c * (double) SincKernel::hermitePoints;

        // Hermite evaluation at table position x (>= 0). Positions at or past the
        // end land on the table's zero padding, so no test is needed per tap.
        auto weight = [h, limit] (double x) noexcept
        {
            const int i = std::min ((int) x, limit);
            const double t = x - (double) i;
            const double* e = h + 2 * (size_t) i;
            const double t2 = t * t;
            const double a = t2 * (3.0 - 2.0 * t);          // h01
            const double b = t2 * (t - 1.0);                // h11
            return e[0] + a * (e[2] - e[0]) + (t - 2.0 * t2 + t2 * t) * e[1] + b * e[3];
        };

        const auto n = (int64_t) std::floor (p);
        const int split = (int) (n - kLo + 1);   // taps with k <= n

        // Taps at or before p: x = (p - k) * scale, falling by 'scale' per tap.
        double x = (p - (double) kLo) * scale;
        double s0 = 0.0, s1 = 0.0;
        int j = 0;
        for (; j + 2 <= split; j += 2)
        {
            const double v0 = weight (x), v1 = weight (x - scale);
            w[j] = v0; w[j + 1] = v1;
            s0 += v0; s1 += v1;
            x -= 2.0 * scale;
        }
        for (; j < split; ++j) { const double v = weight (x); w[j] = v; s0 += v; x -= scale; }

        // Taps after p: x = (k - p) * scale, rising.
        x = ((double) (kLo + split) - p) * scale;
        for (; j + 2 <= count; j += 2)
        {
            const double v0 = weight (x), v1 = weight (x + scale);
            w[j] = v0; w[j + 1] = v1;
            s0 += v0; s1 += v1;
            x += 2.0 * scale;
        }
        for (; j < count; ++j) { const double v = weight (x); w[j] = v; s0 += v; x += scale; }

        const double sum = s0 + s1;
        const double norm = sum > 0.0 ? 1.0 / sum : 0.0;
        const int jStart = kLo < 0 ? (int) (-kLo) : 0;
        const int jEnd = kHi >= length ? (int) (length - kLo) : count;
        finish (w, jStart, jEnd, kLo, data, numChannels, norm, out);
    }

    std::vector<double> weights;
};

} // namespace goblet
