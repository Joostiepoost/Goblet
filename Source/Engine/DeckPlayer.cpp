// DeckPlayer.cpp

#include "DeckPlayer.h"

#include <cmath>

namespace goblet
{

const char* crossfadeShapeName (CrossfadeShape s)
{
    switch (s)
    {
        case CrossfadeShape::equalPower: return "Equal power";
        case CrossfadeShape::linear:     return "Linear";
        case CrossfadeShape::sCurve:     return "S-curve";
        case CrossfadeShape::bezier:     return "Bezier";
    }
    return "?";
}

void crossfadeGains (CrossfadeShape shape, double tension, double g, double& gIn, double& gOut) noexcept
{
    const double halfPi = 1.57079632679489661923;

    switch (shape)
    {
        case CrossfadeShape::linear:
            gIn = g; gOut = 1.0 - g;
            break;

        case CrossfadeShape::sCurve:
        {
            const double s = g * g * (3.0 - 2.0 * g);
            gIn = s; gOut = 1.0 - s;
            break;
        }

        case CrossfadeShape::bezier:
        {
            // Cubic Bezier in amplitude with control values (0, k, 1, 1):
            // k = 0 starts slowly, k = 1 rises fast. The outgoing voice is the mirror.
            auto bez = [tension] (double x)
            {
                const double u = 1.0 - x;
                return 3.0 * u * u * x * tension + 3.0 * u * x * x + x * x * x;
            };
            gIn = bez (g); gOut = bez (1.0 - g);
            break;
        }

        case CrossfadeShape::equalPower:
        default:
            gIn = std::sin (g * halfPi); gOut = std::cos (g * halfPi);
            break;
    }
}

// Raised cosine from 0 to 1 over x = 0..1: the declick ramps.
static inline double ramp (double x) noexcept
{
    return 0.5 - 0.5 * std::cos (juce::jlimit (0.0, 1.0, x) * juce::MathConstants<double>::pi);
}

DeckPlayer::DeckPlayer()
{
    KernelBank::buildAll();
    reader.prepare (5.0);   // up to a 192 kHz file on a 44.1 kHz device (ratio 4.35)
}

//==============================================================================
void DeckPlayer::retire (std::shared_ptr<const void> old)
{
    // The audio thread reads the raw pointer once per block, so anything
    // replaced more than two seconds ago is no longer in use.
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (old != nullptr)
        retired.push_back ({ std::move (old), now });
    retired.erase (std::remove_if (retired.begin(), retired.end(),
                                   [now] (const Retired& r) { return now - r.time > 2000.0; }),
                   retired.end());
}

void DeckPlayer::setSource (std::shared_ptr<const SourceFile> file)
{
    auto old = source;
    source = std::move (file);
    sourcePtr.store (source.get(), std::memory_order_release);
    retire (old);

    if (source != nullptr)
        setFragment (0, source->length());
    else
        setFragment (0, 0);
}

void DeckPlayer::setFragment (int64_t in, int64_t out)
{
    const int64_t len = source != nullptr ? source->length() : 0;
    in = juce::jlimit ((int64_t) 0, len, in);
    out = juce::jlimit ((int64_t) 0, len, out);
    if (out < in) std::swap (in, out);
    inPoint.store (in);
    outPoint.store (out);
}

void DeckPlayer::setCrossfade (double ms, CrossfadeShape shape, double tension)
{
    fadeMs.store (juce::jlimit (0.0, 200.0, ms));
    fadeShape.store ((int) shape);
    fadeTension.store (juce::jlimit (0.0, 1.0, tension));
}

void DeckPlayer::play()
{
    uiPlaying.store (true);     // the button follows at once; the audio thread confirms
    playCounter.fetch_add (1, std::memory_order_release);
}

void DeckPlayer::stop()
{
    stopCounter.fetch_add (1, std::memory_order_release);
}

//==============================================================================
void DeckPlayer::prepare (double sampleRate, int)
{
    outputRate = sampleRate;
    declickLen = juce::jmax (1, (int) std::lround (declickMs * 0.001 * sampleRate));
    fade.remaining = 0;
    state = State::stopped;
    q = 0.0;
}

void DeckPlayer::startNow() noexcept
{
    // A file loaded just before the start is the one that plays (process() would
    // otherwise treat it as a change of file and stop).
    if (sourcePtr.load (std::memory_order_acquire) != lastSource)
    {
        lastSource = sourcePtr.load (std::memory_order_acquire);
        state = State::stopped;
        fade.remaining = 0;
        q = 0.0;
    }

    // Restarted while sounding: the old position fades out against the new start.
    if (state != State::stopped && fadeMs.load() > 0.0)
    {
        const auto* src = sourcePtr.load (std::memory_order_acquire);
        if (src != nullptr)
        {
            const int64_t a = juce::jlimit ((int64_t) 0, src->length(), inPoint.load());
            const int64_t b = juce::jlimit (a, src->length(), outPoint.load());
            fade.pos = lastReverse ? (double) (b - 1) - q : (double) a + q;
            fade.dir = lastReverse ? -1 : 1;
            fade.remaining = fade.length;
            rampIn = declickLen;    // the crossfade itself brings the new voice in
        }
        else rampIn = 0;
    }
    else
    {
        fade.remaining = 0;
        rampIn = 0;
    }

    q = 0.0;
    rampOut = 0;
    state = State::playing;
}

void DeckPlayer::stopNow() noexcept
{
    if (state == State::playing)
    {
        state = State::stopping;
        rampOut = rampOutLen = declickLen;
    }
}

void DeckPlayer::process (float* const* out, int numSamples, const SincKernel& kernel) noexcept
{
    const SourceFile* src = sourcePtr.load (std::memory_order_acquire);

    if (src != lastSource)
    {
        lastSource = src;
        q = 0.0;
        fade.remaining = 0;
        state = State::stopped;
    }

    // Commands from the message thread.
    const uint32_t pc = playCounter.load (std::memory_order_acquire);
    if (pc != lastPlay) { lastPlay = pc; startNow(); }
    const uint32_t sc = stopCounter.load (std::memory_order_acquire);
    if (sc != lastStop) { lastStop = sc; stopNow(); }

    const int64_t len = src != nullptr ? src->length() : 0;
    const int64_t a = juce::jlimit ((int64_t) 0, len, inPoint.load());
    const int64_t b = juce::jlimit (a, len, outPoint.load());
    const double fragLen = (double) (b - a);
    const double revStart = (double) (b - 1);     // reverse: position = revStart - q (the mirror of a + q)
    const bool haveAudio = src != nullptr && fragLen >= 1.0;

    if (! haveAudio)
    {
        if (state != State::stopped) state = State::stopped;
        uiPlaying.store (false);
        uiSourcePos.store (-1.0);
        return;
    }

    if (q >= fragLen)
        q = std::fmod (q, fragLen);

    const bool rev = reverse.load();
    const bool looping = loop.load();
    const double r = src->sampleRate / outputRate;          // read ratio, fixed
    const auto shape = (CrossfadeShape) fadeShape.load();
    const double tension = fadeTension.load();

    // Crossfade length in output samples; never longer than half the fragment.
    int fadeLen = (int) std::llround (fadeMs.load() * 0.001 * outputRate);
    fadeLen = juce::jmin (fadeLen, (int) (0.5 * fragLen / r));
    fade.length = juce::jmax (1, fadeLen);
    const bool useFades = fadeLen > 0;

    // Reverse switched: the old direction fades out while the mirrored one fades in.
    if (rev != lastReverse)
    {
        if (state != State::stopped && useFades)
        {
            fade.pos = lastReverse ? revStart - q : (double) a + q;
            fade.dir = lastReverse ? -1 : 1;
            fade.remaining = fade.length;
        }
        lastReverse = rev;
    }

    const float* channels[2] = { src->audio.getReadPointer (0), src->audio.getReadPointer (1) };
    const bool passthrough = juce::exactlyEqual (r, 1.0);
    const double invDeclick = 1.0 / (double) declickLen;

    for (int i = 0; i < numSamples && state != State::stopped; ++i)
    {
        const double pos = rev ? revStart - q : (double) a + q;
        double y[2];

        if (passthrough && fade.remaining == 0 && juce::exactlyEqual (pos, std::floor (pos)))
        {
            // The file's own rate on a whole sample: the samples unchanged.
            const auto k = (int64_t) pos;
            y[0] = channels[0][k];
            y[1] = channels[1][k];
        }
        else
        {
            reader.read (kernel, channels, 2, len, pos, r, y);
        }

        if (fade.remaining > 0)
        {
            double z[2];
            reader.read (kernel, channels, 2, len, fade.pos, r, z);
            const double g = 1.0 - (double) fade.remaining / (double) fade.length;
            double gIn, gOut;
            crossfadeGains (shape, tension, g, gIn, gOut);
            y[0] = gIn * y[0] + gOut * z[0];
            y[1] = gIn * y[1] + gOut * z[1];
            fade.pos += (double) fade.dir * r;
            --fade.remaining;
        }

        // Declick: fade in after a start, fade out before a stop.
        double gain = 1.0;
        if (rampIn < declickLen) { gain = ramp (((double) rampIn + 0.5) * invDeclick); ++rampIn; }

        if (state == State::stopping)
        {
            gain *= ramp (((double) rampOut - 0.5) / (double) rampOutLen);
            if (--rampOut <= 0)
            {
                state = State::stopped;
                fade.remaining = 0;
                q = 0.0;
            }
        }

        out[0][i] += (float) (gain * y[0]);
        out[1][i] += (float) (gain * y[1]);

        if (state == State::stopped)
            break;

        q += r;

        // Played once: fade out over the last few ms so the deck stops at the out point.
        if (! looping && state == State::playing && q + r * (double) declickLen >= fragLen)
        {
            state = State::stopping;
            rampOut = juce::jmax (1, (int) ((fragLen - q) / r));
            rampOut = rampOutLen = juce::jmin (rampOut, declickLen);
        }

        if (q >= fragLen)
        {
            // Loop seam: the old pass reads on past the out point and fades out,
            // the new pass fades in from the in point.
            if (useFades && looping)
            {
                fade.pos = rev ? revStart - q : (double) a + q;
                fade.dir = rev ? -1 : 1;
                fade.remaining = fade.length;
            }
            q = std::fmod (q, fragLen);
        }
    }

    uiPlaying.store (state != State::stopped);
    uiSourcePos.store (state != State::stopped ? (rev ? revStart - q : (double) a + q) : -1.0);
}

} // namespace goblet
