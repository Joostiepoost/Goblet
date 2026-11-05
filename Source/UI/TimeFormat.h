// TimeFormat.h — m:ss.mmm text for times, and speed text.

#pragma once

#include <JuceHeader.h>
#include <cmath>

namespace goblet
{

inline juce::String formatTime (double seconds)
{
    if (seconds < 0.0) seconds = 0.0;
    const auto totalMs = (juce::int64) std::llround (seconds * 1000.0);
    const auto minutes = totalMs / 60000;
    const auto secs = (totalMs / 1000) % 60;
    const auto ms = totalMs % 1000;
    return juce::String (minutes) + ":" + juce::String (secs).paddedLeft ('0', 2)
         + "." + juce::String (ms).paddedLeft ('0', 3);
}

// Accepts "1:23.456", "83.456" or "83". Returns -1 when the text is not a time.
inline double parseTime (const juce::String& text)
{
    const auto t = text.trim();
    if (t.isEmpty()) return -1.0;

    if (t.containsChar (':'))
    {
        const auto m = t.upToFirstOccurrenceOf (":", false, false).trim();
        const auto s = t.fromFirstOccurrenceOf (":", false, false).trim();
        if (! m.containsOnly ("0123456789") || ! s.containsOnly ("0123456789.")) return -1.0;
        return m.getDoubleValue() * 60.0 + s.getDoubleValue();
    }

    if (! t.containsOnly ("0123456789.")) return -1.0;
    return t.getDoubleValue();
}

// Speed in octaves as "x0.750" or as "-4.98 st".
inline juce::String formatSpeed (double octaves, bool semitones)
{
    if (semitones)
    {
        const double st = octaves * 12.0;
        return (st >= 0.0 ? "+" : "") + juce::String (st, 2) + " st";
    }
    return juce::String::fromUTF8 ("\xc3\x97") + juce::String (std::exp2 (octaves), 3);
}

} // namespace goblet
