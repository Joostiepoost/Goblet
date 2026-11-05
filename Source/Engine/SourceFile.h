// SourceFile.h — the loaded sound file, held in memory as 32-bit float.
//
// Always two channels: a mono file is copied to both, a file with more than
// two channels keeps its first two. Also keeps a min/max overview for the
// waveform display.

#pragma once

#include <JuceHeader.h>
#include <memory>

namespace goblet
{

class SourceFile
{
public:
    static std::shared_ptr<const SourceFile> load (const juce::File& file,
                                                   juce::AudioFormatManager& formats,
                                                   juce::String& error);

    // For tests: wrap a buffer that is already in memory.
    static std::shared_ptr<const SourceFile> fromBuffer (juce::AudioBuffer<float>&& buffer,
                                                         double sampleRate,
                                                         const juce::String& name = "test");

    juce::File file;
    juce::String name;
    double sampleRate = 48000.0;
    int fileChannels = 2;
    int bitDepth = 0;
    juce::AudioBuffer<float> audio;     // 2 channels

    int64_t length() const noexcept     { return audio.getNumSamples(); }
    double seconds() const noexcept     { return (double) length() / sampleRate; }

    static constexpr int peakBlock = 128;
    std::vector<float> peakMin, peakMax;    // per block, both channels together

private:
    void buildPeaks();
};

} // namespace goblet
