// SincKernel.cpp — building the kernel tables.

#include "SincKernel.h"

#include <cmath>
#include <memory>
#include <mutex>
#include <thread>

namespace goblet
{

static const KernelSpec specs[numQualities] =
{
    { "High",      192, 120.0, 256 },
    { "Very high", 368, 140.0, 256 },
    { "Max",       512, 150.0, 256 },
    { "Render",    768, 150.0, 256 },
};

const KernelSpec& kernelSpec (Quality q)
{
    return specs[(int) q];
}

double SincKernel::besselI0 (double x) noexcept
{
    // Power series: I0(x) = sum_k ((x/2)^k / k!)^2. For beta up to ~16 it
    // needs about 40 terms for full double precision.
    const double half = 0.5 * x;
    double term = 1.0, sum = 1.0;

    for (int k = 1; k < 500; ++k)
    {
        const double f = half / (double) k;
        term *= f * f;
        sum += term;
        if (term < sum * 1.0e-21)
            break;
    }

    return sum;
}

double SincKernel::exact (double u) const noexcept
{
    u = std::abs (u);
    if (u >= (double) K) return 0.0;
    const double pi = 3.14159265358979323846;
    const double x = u / (double) K;
    const double w = besselI0 (kaiserBeta * std::sqrt (1.0 - x * x)) / i0beta;
    const double a = pi * rho * u;
    const double sinc = a < 1.0e-12 ? 1.0 : std::sin (a) / a;
    return rho * sinc * w;
}

SincKernel::SincKernel (const KernelSpec& s)
    : kernelSpecRef (s), K (s.taps / 2), L (s.phases)
{
    const double pi = 3.14159265358979323846;
    const double A = s.stopbandDb;

    delta = (A - 8.0) / (2.285 * pi * (double) s.taps);
    rho = 1.0 - 0.5 * delta;
    kaiserBeta = A > 50.0 ? 0.1102 * (A - 8.7) : 0.5842 * std::pow (A - 21.0, 0.4) + 0.07886 * (A - 21.0);
    i0beta = besselI0 (kaiserBeta);

    // Polyphase rows phi = -1 .. L+2 (the extra rows serve the 4-point interpolation).
    const int rows = L + 4;
    poly.assign ((size_t) rows * (size_t) K, 0.0);
    sums.assign ((size_t) rows, 0.0);
    for (int phi = -1; phi <= L + 2; ++phi)
    {
        double* r = poly.data() + (size_t) (phi + 1) * (size_t) K;
        double sum = 0.0;
        for (int m = 0; m < K; ++m)
        {
            r[m] = exact ((double) m + (double) phi / (double) L);
            sum += r[m];
        }
        sums[(size_t) (phi + 1)] = sum;
    }

    // Hermite table: values and slopes (slope by a 4th-order central difference of
    // the exact function), the slope scaled to one table step.
    const int H = hermitePoints;
    const double d = 1.0e-4;
    hermite.assign (2 * ((size_t) K * (size_t) H + 2), 0.0);
    for (int i = 0; i < K * H; ++i)
    {
        const double u = (double) i / (double) H;
        const double slope = (-exact (u + 2.0 * d) + 8.0 * exact (u + d) - 8.0 * exact (u - d) + exact (u - 2.0 * d)) / (12.0 * d);
        hermite[2 * (size_t) i]     = exact (u);
        hermite[2 * (size_t) i + 1] = slope / (double) H;
    }
}

namespace KernelBank
{
    static std::unique_ptr<SincKernel> kernels[numQualities];
    static std::once_flag built;

    void buildAll()
    {
        std::call_once (built, []
        {
            std::thread threads[numQualities];
            for (int q = 0; q < numQualities; ++q)
                threads[q] = std::thread ([q] { kernels[q] = std::make_unique<SincKernel> (specs[q]); });
            for (auto& t : threads) t.join();
        });
    }

    const SincKernel& get (Quality q)
    {
        buildAll();
        return *kernels[(int) q];
    }
}

} // namespace goblet
