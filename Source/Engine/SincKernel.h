// SincKernel.h — the interpolation filter of Goblet's file players.
//
// A Kaiser-windowed sinc, tabulated once per quality preset. The reader
// (SincReader.h) evaluates it at any fractional position, stretched by the
// speed so the cutoff follows the playback speed.
//
// Design (see "Playback engine" in the proposal):
//   prototype  g(u) = rho * sinc(rho * u) * w(u / K),   |u| < K
//   window     w(x) = I0(beta * sqrt(1 - x^2)) / I0(beta)
//   rho        = 1 - delta / 2      (cutoff, as a fraction of Nyquist)
//   beta       = 0.1102 * (A - 8.7)
//   delta      = (A - 8) / (2.285 * pi * N)   (Kaiser's length formula, solved
//                                              for the transition width)
// where N = 2K is the number of taps at speed <= x1 and A the stopband
// attenuation in dB. The stopband starts exactly at Nyquist, so images and
// aliases are always attenuated by at least A.
//
// Two tables, laid out for the two ways the reader uses the kernel, both
// small enough to stay in the processor's cache:
//   * polyphase (speed <= x1): row phi holds g(m + phi/L) for m = 0..K-1, with
//     L = 256 phases per zero crossing. One output sample reads four
//     neighbouring rows from start to end and combines them with cubic
//     (4-point Lagrange) weights: error below 1.1e-10 (-199 dB).
//   * Hermite (speed > x1, kernel stretched): g and its slope at 64 points
//     per zero crossing, interpolated with cubic Hermite polynomials (error
//     below 3e-9, -170 dB).

#pragma once

#include <vector>

namespace goblet
{

enum class Quality { High = 0, VeryHigh, Max, Render };
constexpr int numQualities = 4;

struct KernelSpec
{
    const char* name;
    int taps;             // N at speed <= x1
    double stopbandDb;    // A
    int phases;           // L: polyphase phases per zero crossing
};

const KernelSpec& kernelSpec (Quality q);

class SincKernel
{
public:
    explicit SincKernel (const KernelSpec& spec);

    int halfWidth() const noexcept          { return K; }      // K, in input samples at c = 1
    int phases() const noexcept             { return L; }
    double cutoff() const noexcept          { return rho; }    // fraction of Nyquist (-6 dB point)
    double transitionWidth() const noexcept { return delta; }  // fraction of Nyquist
    double passbandEdge() const noexcept    { return 1.0 - delta; }
    double beta() const noexcept            { return kaiserBeta; }
    const KernelSpec& spec() const noexcept { return kernelSpecRef; }

    // Polyphase rows: row(phi)[m] = g(|m + phi / L|), phi = -1 .. L+2; rowSum(phi) = sum over m.
    const double* row (int phi) const noexcept { return poly.data() + (size_t) (phi + 1) * (size_t) K; }
    double rowSum (int phi) const noexcept     { return sums[(size_t) (phi + 1)]; }

    // Hermite table: pairs (g, slope * step) at u = i / hermitePoints.
    static constexpr int hermitePoints = 64;
    const double* hermiteTable() const noexcept { return hermite.data(); }
    int hermiteLimit() const noexcept           { return K * hermitePoints; }

    // g(u) for u >= 0, from the Hermite table; 0 for u >= K.
    double evaluate (double u) const noexcept
    {
        const double x = u * (double) hermitePoints;
        const int i = (int) x;
        if (i >= K * hermitePoints) return 0.0;
        const double t = x - (double) i;
        const double* h = hermite.data() + 2 * (size_t) i;
        const double t2 = t * t, t3 = t2 * t;
        return (2.0 * t3 - 3.0 * t2 + 1.0) * h[0] + (t3 - 2.0 * t2 + t) * h[1]
             + (3.0 * t2 - 2.0 * t3) * h[2] + (t3 - t2) * h[3];
    }

    // The exact prototype, for building tables and for tests.
    double exact (double u) const noexcept;

    static double besselI0 (double x) noexcept;

private:
    const KernelSpec& kernelSpecRef;
    int K, L;
    double rho, delta, kaiserBeta, i0beta;
    std::vector<double> poly, sums, hermite;
};

// The four kernels, built once at start-up (call buildAll on the message
// thread before audio starts; all four take about a second).
namespace KernelBank
{
    void buildAll();
    const SincKernel& get (Quality q);
}

} // namespace goblet
