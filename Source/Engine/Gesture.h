// Gesture.h — what changed the mix during a take, with its time, so the take
// can be rendered again offline (the re-render).
//
// The Mixer records, at the start of each block while capturing, every change
// it applies: faders, trims, mute, solo, loop, reverse, fragments, crossfades,
// deck starts and stops, slot bypass, and the plugin parameters that MIDI
// mappings set. The first block also records where every deck is. Times are
// output samples from the moment Rec was pressed.
//
// Not recorded: knobs turned inside a plugin's own window during the take (the
// plugin keeps those in its own state), and anything a plugin does by itself.
//
// Saved beside the take as "<take>.gesture": a short header line, then one
// event per line (plain text).

#pragma once

#include <JuceHeader.h>
#include <vector>

namespace goblet
{

struct GestureEvent
{
    enum Kind : int
    {
        deckPosition = 0,   // deck; a = state (0 stopped, 1 playing, 2 stopping); v = samples travelled into the fragment
        fader,              // deck; v = position, w = ramp ms
        trim,               // deck; v = dB, w = ramp ms
        mute, solo,         // deck; a = on
        loop, reverse,      // deck; a = on
        fragment,           // deck; a = in, b = out (source samples)
        crossfade,          // deck; v = ms, a = shape, w = tension
        start, stop,        // deck
        bypass,             // deck = chain; a = slot, b = on
        parameter,          // deck = chain; a = slot, b = index, v = value 0..1
        deckDetail,         // deck; the player's crossfade and declick state at Rec: a = fade remaining, b = fade direction,
                            //   v = fade position, w = rampIn, x = rampOut, y = rampOutLen
        rampState,          // deck; a = 0 fader, 1 trim, 2 mute/solo gain; v = current gain, w = target, x = step, b = samples left
        numKinds
    };

    int64_t time = 0;
    int kind = 0, deck = 0;
    int64_t a = 0, b = 0;
    double v = 0.0, w = 0.0, x = 0.0, y = 0.0;
};

struct Gesture
{
    double sampleRate = 48000.0;
    int64_t length = 0;            // samples, from Rec to the end of the take
    std::vector<GestureEvent> events;

    bool writeTo (const juce::File& file) const
    {
        juce::MemoryOutputStream out;
        out << "GOBLET_GESTURE 2 " << juce::String (sampleRate, 3) << " " << juce::String (length) << "\n";
        for (auto& e : events)
            out << juce::String (e.time) << " " << e.kind << " " << e.deck << " " << juce::String (e.a) << " " << juce::String (e.b)
                << " " << juce::String (e.v, 17) << " " << juce::String (e.w, 17) << " " << juce::String (e.x, 17) << " " << juce::String (e.y, 17) << "\n";
        return file.replaceWithData (out.getData(), out.getDataSize());
    }

    bool readFrom (const juce::File& file)
    {
        juce::StringArray lines;
        lines.addLines (file.loadFileAsString());
        if (lines.isEmpty()) return false;
        auto head = juce::StringArray::fromTokens (lines[0], " ", "");
        if (head.size() < 4 || head[0] != "GOBLET_GESTURE") return false;
        sampleRate = head[2].getDoubleValue();
        length = head[3].getLargeIntValue();
        events.clear();
        events.reserve ((size_t) lines.size());
        for (int i = 1; i < lines.size(); ++i)
        {
            auto t = juce::StringArray::fromTokens (lines[i], " ", "");
            if (t.size() < 7) continue;
            GestureEvent e;
            e.time = t[0].getLargeIntValue();
            e.kind = t[1].getIntValue();
            e.deck = t[2].getIntValue();
            e.a = t[3].getLargeIntValue();
            e.b = t[4].getLargeIntValue();
            e.v = t[5].getDoubleValue();
            e.w = t[6].getDoubleValue();
            if (t.size() >= 9) { e.x = t[7].getDoubleValue(); e.y = t[8].getDoubleValue(); }
            if (e.kind >= 0 && e.kind < GestureEvent::numKinds) events.push_back (e);
        }
        return sampleRate > 0.0;
    }

    static juce::File fileFor (const juce::File& takeWav)   { return takeWav.withFileExtension (".gesture"); }
};

} // namespace goblet
