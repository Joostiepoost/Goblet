// SourceFile.cpp

#include "SourceFile.h"

namespace goblet
{

std::shared_ptr<const SourceFile> SourceFile::load (const juce::File& file,
                                                    juce::AudioFormatManager& formats,
                                                    juce::String& error)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr)
    {
        error = "Could not read " + file.getFileName() + " (WAV and AIFF are supported).";
        return {};
    }

    const auto length = reader->lengthInSamples;
    const double maxSeconds = 30.0 * 60.0;

    if (length <= 0)
    {
        error = file.getFileName() + " is empty.";
        return {};
    }

    if ((double) length / reader->sampleRate > maxSeconds)
    {
        error = file.getFileName() + " is longer than 30 minutes; Goblet keeps the whole file in memory.";
        return {};
    }

    auto s = std::make_shared<SourceFile>();
    s->file = file;
    s->name = file.getFileName();
    s->sampleRate = reader->sampleRate;
    s->fileChannels = (int) reader->numChannels;
    s->bitDepth = (int) reader->bitsPerSample;
    s->audio.setSize (2, (int) length);

    reader->read (&s->audio, 0, (int) length, 0, true, true);

    if (reader->numChannels == 1)
        s->audio.copyFrom (1, 0, s->audio, 0, 0, (int) length);

    s->buildPeaks();
    return s;
}

std::shared_ptr<const SourceFile> SourceFile::fromBuffer (juce::AudioBuffer<float>&& buffer,
                                                          double sampleRate, const juce::String& name)
{
    auto s = std::make_shared<SourceFile>();
    s->name = name;
    s->sampleRate = sampleRate;
    s->fileChannels = buffer.getNumChannels();
    s->bitDepth = 32;

    if (buffer.getNumChannels() == 2)
        s->audio = std::move (buffer);
    else
    {
        s->audio.setSize (2, buffer.getNumSamples());
        s->audio.copyFrom (0, 0, buffer, 0, 0, buffer.getNumSamples());
        s->audio.copyFrom (1, 0, buffer, buffer.getNumChannels() > 1 ? 1 : 0, 0, buffer.getNumSamples());
    }

    s->buildPeaks();
    return s;
}

void SourceFile::buildPeaks()
{
    const int n = audio.getNumSamples();
    const int blocks = (n + peakBlock - 1) / peakBlock;
    peakMin.assign ((size_t) blocks, 0.0f);
    peakMax.assign ((size_t) blocks, 0.0f);

    for (int b = 0; b < blocks; ++b)
    {
        const int start = b * peakBlock;
        const int num = juce::jmin (peakBlock, n - start);
        float lo = 0.0f, hi = 0.0f;

        for (int ch = 0; ch < 2; ++ch)
        {
            auto r = juce::FloatVectorOperations::findMinAndMax (audio.getReadPointer (ch, start), num);
            lo = juce::jmin (lo, r.getStart());
            hi = juce::jmax (hi, r.getEnd());
        }

        peakMin[(size_t) b] = lo;
        peakMax[(size_t) b] = hi;
    }
}

} // namespace goblet
