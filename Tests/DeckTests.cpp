// DeckTests.cpp — checks Goblet's deck player and measures its CPU cost.
//
//   GobletTests [--out <folder>] [--quick]
//
// 1. Passthrough: at equal file and device rates the deck must output the
//    file's samples unchanged (after the 3 ms start ramp).
// 2. The playhead stays inside the fragment, with reverse switched while
//    playing, at a rate ratio other than 1.
// 3. Loop seam and reverse switch: the largest sample-to-sample step,
//    relative to the steepest step of the sine itself (1.00 = no click).
// 4. Start, stop and play-once: the same measure for the declick ramps.
// 5. Play all: two decks started together stay sample-aligned.
//    Mute and solo: who is heard, and the 5 ms ramps don't click.
// 5c. Plugin latency: decks with different plugin latencies stay aligned in
//    the sum; bypassing a plugin neither clicks nor shifts the timing.
// 6. CPU: time to produce 1 s of stereo output per deck, per preset, at the
//    rate ratios that occur in practice.

#include <JuceHeader.h>
#include "../Source/Engine/SincKernel.h"
#include "../Source/Engine/SourceFile.h"
#include "../Source/Engine/DeckPlayer.h"
#include "../Source/Engine/Mixer.h"

#include <chrono>
#include <cmath>
#include <iostream>

using namespace goblet;

namespace
{
constexpr double pi = 3.14159265358979323846;

// A stand-in plugin: delays the signal by `latency` samples, reports that
// latency, and multiplies by `gain`.
class DelayPlugin : public juce::AudioProcessor
{
public:
    DelayPlugin (int latencySamples, float g)
        : AudioProcessor (BusesProperties().withInput ("In", juce::AudioChannelSet::stereo()).withOutput ("Out", juce::AudioChannelSet::stereo())),
          latency (latencySamples), gain (g)
    {
        setLatencySamples (latency);
    }

    const juce::String getName() const override             { return "Test delay"; }
    void prepareToPlay (double, int) override               { line.assign (2 * (size_t) (latency + 1), 0.0f); pos = 0; }
    void releaseResources() override                        {}
    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        const int len = latency + 1;
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                line[(size_t) (ch * len + pos)] = b.getSample (ch, i);
                b.setSample (ch, i, gain * line[(size_t) (ch * len + (pos + 1) % len)]);
            }
            pos = (pos + 1) % len;
        }
    }
    double getTailLengthSeconds() const override            { return 0.0; }
    bool acceptsMidi() const override                       { return false; }
    bool producesMidi() const override                      { return false; }
    juce::AudioProcessorEditor* createEditor() override     { return nullptr; }
    bool hasEditor() const override                         { return false; }
    int getNumPrograms() override                           { return 1; }
    int getCurrentProgram() override                        { return 0; }
    void setCurrentProgram (int) override                   {}
    const juce::String getProgramName (int) override        { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override  {}
    void setStateInformation (const void*, int) override    {}

private:
    int latency;
    float gain;
    std::vector<float> line;
    int pos = 0;
};

std::shared_ptr<const SourceFile> sineFile (double rate, double seconds, double freq, double amp, double phase = 0.0)
{
    juce::AudioBuffer<float> b (2, (int) (rate * seconds));
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const float v = (float) (amp * std::sin (2.0 * pi * freq * (double) i / rate + phase));
        b.setSample (0, i, v);
        b.setSample (1, i, v);
    }
    return SourceFile::fromBuffer (std::move (b), rate, "sine");
}

std::shared_ptr<const SourceFile> noiseFile (double rate, double seconds, int seed)
{
    juce::AudioBuffer<float> b (2, (int) (rate * seconds));
    juce::Random rnd (seed);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (ch, i, rnd.nextFloat() - 0.5f);
    return SourceFile::fromBuffer (std::move (b), rate, "noise");
}

// Runs a deck for `seconds` at `rate`, calling `atBlock` before each block
// with the time in seconds; returns the left channel.
std::vector<float> run (DeckPlayer& d, double rate, double seconds, Quality q,
                        std::function<void (double)> atBlock = {}, int block = 64)
{
    const auto& k = KernelBank::get (q);
    const int total = (int) (rate * seconds);
    std::vector<float> l ((size_t) total, 0.0f), r ((size_t) total, 0.0f);
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);
        if (atBlock) atBlock ((double) pos / rate);
        float* outs[2] = { l.data() + pos, r.data() + pos };
        d.process (outs, n, k);
    }
    return l;
}

double maxStep (const std::vector<float>& y, size_t from = 1)
{
    double m = 0.0;
    for (size_t i = std::max<size_t> (from, 1); i < y.size(); ++i)
        m = std::max (m, (double) std::abs (y[i] - y[i - 1]));
    return m;
}
} // namespace

//==============================================================================
int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::File outDir = juce::File::getCurrentWorkingDirectory().getChildFile ("Measurements");
    bool quick = false;

    for (int i = 1; i < argc; ++i)
    {
        const juce::String a (argv[i]);
        if (a == "--out" && i + 1 < argc) outDir = juce::File::getCurrentWorkingDirectory().getChildFile (argv[++i]);
        else if (a == "--quick")          quick = true;
    }

    outDir.createDirectory();
    KernelBank::buildAll();

    juce::String report;
    auto line = [&report] (const juce::String& s) { report << s << "\n"; std::cout << s << std::endl; };
    const juce::String x = juce::String::fromUTF8 ("\xc3\x97");

    line ("# Goblet deck player: measurements (step 1)");
    line ("");
    line ("Machine: " + juce::SystemStats::getCpuModel() + ", " + juce::SystemStats::getOperatingSystemName()
          + ". Date: " + juce::Time::getCurrentTime().toString (true, true) + ".");
    line ("");

    //== 1. Passthrough ========================================================
    {
        const double rate = 96000.0;
        auto src = noiseFile (rate, 1.5, 3);
        DeckPlayer d;
        d.setSource (src);
        d.prepare (rate, 64);
        d.startNow();
        auto y = run (d, rate, 1.5, Quality::Max);
        const int skip = (int) std::lround (DeckPlayer::declickMs * 0.001 * rate);
        int differ = 0;
        const float* f = src->audio.getReadPointer (0);
        for (int i = skip; i < (int) y.size(); ++i)
            if (y[(size_t) i] != f[i]) ++differ;
        line ("## Passthrough at equal rates");
        line ("");
        line ("1.5 s of white noise, file and device at 96 kHz, Max: " + juce::String (differ) + " of " + juce::String ((int) y.size() - skip)
              + " samples differ from the file after the 3 ms start ramp" + (differ == 0 ? " (bit-identical)." : "."));
        line ("");
    }

    //== 2. Playhead inside the fragment =======================================
    {
        line ("## Playhead stays inside the fragment");
        line ("");
        line ("A 252 ms fragment of a 44.1 kHz file on a 48 kHz device, 200 ms crossfade (limited to half the fragment), "
              "reverse switched on after 0.5 s and off after 3 s, 5 s of play.");
        line ("");
        const double fileRate = 44100.0, rate = 48000.0;
        auto src = noiseFile (fileRate, 2.0, 5);
        DeckPlayer d;
        d.setSource (src);
        const auto in = (int64_t) (0.448 * fileRate), out = (int64_t) (0.700 * fileRate);
        d.setFragment (in, out);
        d.setCrossfade (200.0, CrossfadeShape::equalPower, 0.5);
        d.prepare (rate, 64);
        d.startNow();
        double lo = 1.0e9, hi = -1.0e9;
        run (d, rate, 5.0, Quality::Max, [&] (double t)
        {
            d.setReverse (t >= 0.5 && t < 3.0);
            const double p = d.getSourcePosition();
            if (p >= 0.0) { lo = std::min (lo, p); hi = std::max (hi, p); }
        });
        const bool inside = lo >= (double) in - 0.5 && hi <= (double) out + 0.5;
        line ("Playhead min / max: " + juce::String (lo / fileRate, 4) + " / " + juce::String (hi / fileRate, 4) + " s; fragment "
              + juce::String ((double) in / fileRate, 3) + " - " + juce::String ((double) out / fileRate, 3) + " s: " + (inside ? "inside." : "OUTSIDE."));
        line ("");
    }

    //== 3. Loop seam and reverse switch =======================================
    const double sRate = 48000.0, freq = 1000.0, amp = 0.5;
    const double sineStep = 2.0 * std::sin (pi * freq / sRate) * amp;   // steepest step of the sine itself
    {
        line ("## Loop seam and reverse switch");
        line ("");
        line ("A 1 kHz sine at 48 kHz (amplitude 0.5), fragment 123.4 ms (not a whole number of periods), 2 s of play. "
              "**Step**: the largest sample-to-sample step, relative to the steepest step of the sine: 1.00 means no click.");
        line ("");
        line ("| Crossfade | Loop seam: step | Reverse switch: step |");
        line ("| --- | --- | --- |");

        auto src = sineFile (sRate, 1.0, freq, amp);
        struct Case { juce::String name; double ms; CrossfadeShape shape; };
        const Case cases[] = { { "none (0 ms)", 0.0, CrossfadeShape::linear },
                               { "10 ms equal power", 10.0, CrossfadeShape::equalPower },
                               { "10 ms linear", 10.0, CrossfadeShape::linear },
                               { "10 ms S-curve", 10.0, CrossfadeShape::sCurve },
                               { "10 ms Bezier 0.5", 10.0, CrossfadeShape::bezier } };
        const size_t skip = (size_t) (0.004 * sRate);

        for (auto& c : cases)
        {
            DeckPlayer d;
            d.setSource (src);
            d.setFragment ((int64_t) (0.1 * sRate), (int64_t) (0.1 * sRate) + (int64_t) (0.1234 * sRate));
            d.setCrossfade (c.ms, c.shape, 0.5);
            d.prepare (sRate, 64);
            d.startNow();
            auto seam = run (d, sRate, 2.0, Quality::Max);

            DeckPlayer e;
            e.setSource (src);
            e.setFragment ((int64_t) (0.1 * sRate), (int64_t) (0.1 * sRate) + (int64_t) (0.1234 * sRate));
            e.setCrossfade (c.ms, c.shape, 0.5);
            e.prepare (sRate, 64);
            e.startNow();
            auto rev = run (e, sRate, 0.1, Quality::Max, [&] (double t) { e.setReverse (t >= 0.0503); });

            line ("| " + c.name + " | " + juce::String (maxStep (seam, skip) / sineStep, 2) + " | " + juce::String (maxStep (rev, skip) / sineStep, 2) + " |");
        }
        line ("");
    }

    //== 4. Start, stop and play once ==========================================
    {
        line ("## Start, stop and play once");
        line ("");
        line ("The same sine, fragment starting at a peak of the waveform. Step relative to the sine's own steepest step; "
              "without the 3 ms ramps a start at a peak would jump by the full amplitude (about " + juce::String (amp / sineStep, 0) + ").");
        line ("");
        auto src = sineFile (sRate, 1.0, freq, amp, 0.5 * pi);   // starts at +amp

        DeckPlayer d;
        d.setSource (src);
        d.setFragment (0, (int64_t) (0.5 * sRate) + 7);
        d.prepare (sRate, 64);
        d.startNow();
        bool stopped = false;
        auto y = run (d, sRate, 0.3, Quality::Max, [&] (double t) { if (t >= 0.2 && ! stopped) { d.stopNow(); stopped = true; } });
        std::vector<float> withLead (y.size() + 1, 0.0f);
        std::copy (y.begin(), y.end(), withLead.begin() + 1);   // the silence before the start
        const double startStopStep = maxStep (withLead) / sineStep;
        const bool silentAfter = std::all_of (y.begin() + (long) (0.21 * sRate), y.end(), [] (float v) { return v == 0.0f; });

        DeckPlayer e;
        e.setSource (src);
        e.setFragment (0, (int64_t) (0.1 * sRate) + 7);
        e.setLoop (false);
        e.prepare (sRate, 64);
        e.startNow();
        auto once = run (e, sRate, 0.2, Quality::Max);
        const double onceStep = maxStep (once) / sineStep;
        size_t last = 0;
        for (size_t i = 0; i < once.size(); ++i) if (once[i] != 0.0f) last = i;

        line ("| Case | Step | Note |");
        line ("| --- | --- | --- |");
        line ("| Start at a peak, stop after 0.2 s | " + juce::String (startStopStep, 2) + " | " + (silentAfter ? "silent after the stop ramp" : "NOT silent after the stop") + " |");
        line ("| Loop off, 100 ms fragment | " + juce::String (onceStep, 2) + " | last sound at " + juce::String ((double) (last + 1) / sRate * 1000.0, 2)
              + " ms (out point at " + juce::String ((0.1 * sRate + 7.0) / sRate * 1000.0, 2) + " ms); " + (e.isPlaying() ? "STILL PLAYING" : "stopped") + " |");
        line ("");
    }

    //== 5. Play all ===========================================================
    {
        line ("## Play all");
        line ("");
        const double rate = 48000.0;
        auto src = noiseFile (44100.0, 1.0, 9);
        Mixer m;
        m.deck (0).setSource (src);
        m.deck (1).setSource (src);
        m.prepare (rate, 256);
        m.playAll();
        std::vector<float> l (24000, 0.0f), r (24000, 0.0f);
        for (size_t pos = 0; pos < l.size(); pos += 256)
        {
            float* outs[2] = { l.data() + pos, r.data() + pos };
            m.process (outs, 2, (int) std::min ((size_t) 256, l.size() - pos));
        }
        DeckPlayer single;
        single.setSource (src);
        single.prepare (rate, 256);
        single.startNow();
        auto y = run (single, rate, 0.5, Quality::Max, {}, 256);
        double err = 0.0;
        for (size_t i = 0; i < l.size(); ++i) err = std::max (err, (double) std::abs (l[i] - 2.0f * y[i]));
        line ("Two decks with the same 44.1 kHz file on a 48 kHz device, started with Play all: largest difference from twice one deck "
              + juce::String (err, 9) + (err < 1.0e-6 ? " (sample-aligned)." : " (NOT aligned)."));
        line ("");
    }

    //== 5b. Mute and solo ======================================================
    {
        line ("## Mute and solo");
        line ("");
        const double rate = 48000.0;
        auto src = sineFile (rate, 1.0, 1000.0, 0.25, 0.5 * pi);
        auto runMixer = [&] (Mixer& m, double seconds, std::function<void (double)> atBlock)
        {
            std::vector<float> l ((size_t) (seconds * rate), 0.0f), r (l.size(), 0.0f);
            for (size_t pos = 0; pos < l.size(); pos += 64)
            {
                if (atBlock) atBlock ((double) pos / rate);
                float* outs[2] = { l.data() + pos, r.data() + pos };
                m.process (outs, 2, (int) std::min ((size_t) 64, l.size() - pos));
            }
            return l;
        };
        auto rms = [] (const std::vector<float>& y, double rate, double from, double to)
        {
            double acc = 0.0; int n = 0;
            for (size_t i = (size_t) (from * rate); i < (size_t) (to * rate) && i < y.size(); ++i) { acc += (double) y[i] * y[i]; ++n; }
            return n > 0 ? std::sqrt (acc / n) : 0.0;
        };

        // A and B play the same sine; mute B at 0.2 s, solo A at 0.4 s, mute A at 0.6 s, clear all at 0.8 s.
        Mixer m;
        m.deck (0).setSource (src);
        m.deck (1).setSource (src);
        m.prepare (rate, 64);
        m.playAll();
        auto y = runMixer (m, 1.0, [&] (double t)
        {
            m.setMute (1, t >= 0.2 && t < 0.8);
            m.setSolo (0, t >= 0.4 && t < 0.8);
            m.setMute (0, t >= 0.6 && t < 0.8);
        });
        const double one = 0.25 / std::sqrt (2.0);
        const double step = maxStep (y, (size_t) (0.01 * rate)) / (2.0 * std::sin (pi * 1000.0 / rate) * 0.5);
        line ("Decks A and B play the same 1 kHz sine (level of one deck: " + juce::String (one, 3) + " RMS). Level heard in each phase:");
        line ("");
        line ("| Phase | Expected | Measured RMS |");
        line ("| --- | --- | --- |");
        line ("| A and B | two decks | " + juce::String (rms (y, rate, 0.05, 0.19), 3) + " |");
        line ("| B muted | one deck | " + juce::String (rms (y, rate, 0.25, 0.39), 3) + " |");
        line ("| B muted, A soloed | one deck | " + juce::String (rms (y, rate, 0.45, 0.59), 3) + " |");
        line ("| A soloed and muted, B muted | silence | " + juce::String (rms (y, rate, 0.65, 0.79), 6) + " |");
        line ("| all cleared | two decks | " + juce::String (rms (y, rate, 0.85, 0.99), 3) + " |");
        line ("");
        line ("Largest sample-to-sample step over the whole run, relative to the steepest step of the two-deck sine: "
              + juce::String (step, 2) + (step <= 1.05 ? " (no clicks)." : " (CLICK)."));
        line ("");
    }

    //== 5c. Plugin latency and bypass ========================================
    {
        line ("## Plugin latency and bypass");
        line ("");
        const double rate = 48000.0;
        const int lat = 480;    // 10 ms
        auto runMixer = [&] (Mixer& m, int total, std::function<void (int)> atBlock)
        {
            std::vector<float> l ((size_t) total, 0.0f), r (l.size(), 0.0f);
            for (int pos = 0; pos < total; pos += 64)
            {
                if (atBlock) atBlock (pos);
                float* outs[2] = { l.data() + pos, r.data() + pos };
                m.process (outs, 2, std::min (64, total - pos));
            }
            return l;
        };

        // Same file in A and B; A has a 10 ms plugin before its fader, B has none.
        auto src = noiseFile (rate, 1.0, 21);
        Mixer m;
        m.prepare (rate, 64);
        m.deck (0).setSource (src);
        m.deck (1).setSource (src);
        std::unique_ptr<juce::AudioProcessor> replaced;
        juce::String err;
        m.chain (Mixer::preChain (0)).insert (0, std::make_unique<DelayPlugin> (lat, 1.0f), replaced, err);
        m.playAll();
        const int total = 24000;
        auto y = runMixer (m, total, {});

        // Expected: both decks arrive 10 ms late, together: twice the file, delayed by `lat`.
        const float* f = src->audio.getReadPointer (0);
        const int ramp = (int) std::lround (DeckPlayer::declickMs * 0.001 * rate);
        double err1 = 0.0;
        for (int i = lat + ramp; i < total; ++i)
            err1 = std::max (err1, (double) std::abs (y[(size_t) i] - 2.0f * f[i - lat]));
        double early = 0.0;
        for (int i = 0; i < lat; ++i) early = std::max (early, (double) std::abs (y[(size_t) i]));

        line ("Decks A and B play the same noise; A has a test plugin with 10 ms latency before its fader, B none. "
              "Goblet reports a deck latency of " + juce::String (m.getDeckLatency (0)) + " samples for A and delays B by "
              + juce::String (m.getAlignedLatency() - m.getDeckLatency (1)) + " samples.");
        line ("");
        line ("- Largest difference from twice the file delayed by 10 ms: " + juce::String (err1, 9) + (err1 < 1.0e-6 ? " (aligned)." : " (NOT aligned)."));
        line ("- Largest level in the first 10 ms (should be silence): " + juce::String (early, 9) + ".");

        // Bypass while playing: a sine through a plugin that halves the level; bypass after 0.25 s, back on after 0.5 s.
        auto sine = sineFile (rate, 1.0, 1000.0, 0.5, 0.5 * pi);
        Mixer b;
        b.prepare (rate, 64);
        b.deck (0).setSource (sine);
        b.chain (Mixer::postChain (0)).insert (0, std::make_unique<DelayPlugin> (lat, 0.5f), replaced, err);
        b.playAll();
        auto z = runMixer (b, 48000, [&] (int pos)
        {
            b.chain (Mixer::postChain (0)).setBypass (0, pos >= 12000 && pos < 24000);
        });
        const double sineStep = 2.0 * std::sin (pi * 1000.0 / rate) * 0.5;
        const double bypassStep = maxStep (z, (size_t) (lat + ramp + 2)) / sineStep;
        auto peakIn = [&] (int from, int to) { double p = 0.0; for (int i = from; i < to; ++i) p = std::max (p, (double) std::abs (z[(size_t) i])); return p; };
        line ("- Bypass switched while playing (a 1 kHz sine through a test plugin that halves the level, with 10 ms latency): "
              "level " + juce::String (peakIn (6000, 11000), 3) + " with the plugin, " + juce::String (peakIn (14000, 23000), 3) + " bypassed, "
              + juce::String (peakIn (26000, 47000), 3) + " back on; largest step " + juce::String (bypassStep, 2)
              + (bypassStep <= 1.05 ? " (no clicks: the dry path is delayed by the plugin's latency, so the timing does not jump)." : " (CLICK)."));
        line ("");
    }

    //== 6. CPU ================================================================
    {
        line ("## CPU");
        line ("");
        line ("Time for one deck to produce 1 s of stereo output, as a share of one core of this machine (white noise, looped). "
              "Three decks cost about three times as much; all three run on the same audio thread.");
        line ("");

        struct Ratio { double file, device; };
        const Ratio ratios[] = { { 48000, 48000 }, { 44100, 48000 }, { 48000, 44100 }, { 44100, 96000 }, { 48000, 96000 },
                                 { 96000, 48000 }, { 96000, 44100 }, { 192000, 48000 } };

        juce::String head = "| Preset |", sep = "| --- |";
        for (auto& rt : ratios)
        {
            head << " " << juce::String (rt.file / 1000.0, 1) << " on " << juce::String (rt.device / 1000.0, 1) << " kHz (" << x << juce::String (rt.file / rt.device, 3) << ") |";
            sep << " --- |";
        }
        line (head);
        line (sep);

        for (int q = 0; q < numQualities; ++q)
        {
            juce::String row = "| " + juce::String (kernelSpec ((Quality) q).name) + " |";
            for (auto& rt : ratios)
            {
                auto src = noiseFile (rt.file, 3.0, 11);
                DeckPlayer d;
                d.setSource (src);
                d.prepare (rt.device, 512);
                d.startNow();
                const double seconds = quick ? 0.2 : 1.0;
                const auto t0 = std::chrono::steady_clock::now();
                run (d, rt.device, seconds, (Quality) q, {}, 512);
                const double took = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
                row << " " << juce::String (took / seconds * 100.0, 1) << "% |";
            }
            line (row);
        }
        line ("");
    }

    outDir.getChildFile ("report.md").replaceWithText (report);
    std::cout << "\nReport written to " << outDir.getChildFile ("report.md").getFullPathName() << std::endl;
    return 0;
}
