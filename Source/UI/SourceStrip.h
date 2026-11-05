// SourceStrip.h — a deck's whole file: waveform, fragment and playhead.
//
// Copied from Effet. Drag the in or out flag to move it; drag anywhere else
// to mark a new fragment. The dashed line is where the deck's player reads.

#pragma once

#include <JuceHeader.h>
#include "../Engine/SourceFile.h"

namespace goblet
{

class SourceStrip : public juce::Component
{
public:
    SourceStrip();

    void setSource (std::shared_ptr<const SourceFile> file);
    void setFragment (int64_t inSample, int64_t outSample);
    void setSourcePosition (double samplePosition);   // -1 = none

    std::function<void (int64_t in, int64_t out)> onFragmentChanged;   // while dragging
    std::function<void()> onFragmentDragEnded;

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;


private:
    juce::Rectangle<int> waveArea() const;
    float sampleToX (double sample) const;
    int64_t xToSample (float x) const;

    std::shared_ptr<const SourceFile> source;
    int64_t inPoint = 0, outPoint = 0;
    double playPos = -1.0;

    enum class Drag { none, inFlag, outFlag, newRange };
    Drag drag = Drag::none;
    int64_t dragAnchor = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SourceStrip)
};

} // namespace goblet
