// SourceStrip.cpp

#include "SourceStrip.h"
#include "TimeFormat.h"

namespace goblet
{

static const juce::Colour accent (0xff4a9eff);

SourceStrip::SourceStrip()
{
    setRepaintsOnMouseActivity (false);
}

void SourceStrip::setSource (std::shared_ptr<const SourceFile> file)
{
    source = std::move (file);
    inPoint = 0;
    outPoint = source != nullptr ? source->length() : 0;
    repaint();
}

void SourceStrip::setFragment (int64_t in, int64_t out)
{
    if (in != inPoint || out != outPoint)
    {
        inPoint = in; outPoint = out;
        repaint();
    }
}

void SourceStrip::setSourcePosition (double pos)
{
    if (std::abs (pos - playPos) > 0.5)
    {
        const float oldX = sampleToX (playPos), newX = sampleToX (pos);
        playPos = pos;
        auto w = waveArea();
        repaint (juce::Rectangle<float> (juce::jmin (oldX, newX) - 4.0f, (float) w.getY(),
                                         std::abs (newX - oldX) + 8.0f, (float) w.getHeight()).getSmallestIntegerContainer());
    }
}

juce::Rectangle<int> SourceStrip::waveArea() const
{
    return getLocalBounds().reduced (8, 6);
}

float SourceStrip::sampleToX (double sample) const
{
    auto w = waveArea();
    if (source == nullptr || source->length() <= 0) return (float) w.getX();
    return (float) w.getX() + (float) (sample / (double) source->length()) * (float) w.getWidth();
}

int64_t SourceStrip::xToSample (float x) const
{
    auto w = waveArea();
    if (source == nullptr || w.getWidth() <= 0) return 0;
    const double f = juce::jlimit (0.0, 1.0, (double) (x - (float) w.getX()) / (double) w.getWidth());
    return (int64_t) std::llround (f * (double) source->length());
}

void SourceStrip::paint (juce::Graphics& g)
{
    auto w = waveArea();
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);

    if (source == nullptr)
    {
        g.setFont (juce::FontOptions (13.0f));
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        g.drawFittedText ("Drop a WAV or AIFF file here, or click Open...", w, juce::Justification::centred, 2);
        return;
    }

    // Fragment shading
    const float xIn = sampleToX ((double) inPoint), xOut = sampleToX ((double) outPoint);
    g.setColour (accent.withAlpha (0.10f));
    g.fillRect (juce::Rectangle<float> (xIn, (float) w.getY(), xOut - xIn, (float) w.getHeight()));

    // Waveform: one min/max bar per pixel.
    const float mid = (float) w.getCentreY(), half = 0.5f * (float) w.getHeight();
    const auto& lo = source->peakMin;
    const auto& hi = source->peakMax;
    const int blocks = (int) lo.size();

    for (int px = 0; px < w.getWidth(); ++px)
    {
        const int b0 = (int) ((double) px / w.getWidth() * blocks);
        const int b1 = juce::jmax (b0 + 1, (int) ((double) (px + 1) / w.getWidth() * blocks));
        float mn = 0.0f, mx = 0.0f;
        for (int b = b0; b < b1 && b < blocks; ++b) { mn = juce::jmin (mn, lo[(size_t) b]); mx = juce::jmax (mx, hi[(size_t) b]); }
        const float x = (float) (w.getX() + px);
        const bool inside = x >= xIn && x <= xOut;
        g.setColour (inside ? accent.withAlpha (0.9f) : juce::Colours::white.withAlpha (0.28f));
        g.drawVerticalLine ((int) x, mid - mx * half, mid - mn * half + 1.0f);
    }

    // Flags
    g.setColour (accent);
    g.fillRect (juce::Rectangle<float> (xIn - 1.0f, (float) w.getY(), 2.0f, (float) w.getHeight()));
    g.fillRect (juce::Rectangle<float> (xOut - 1.0f, (float) w.getY(), 2.0f, (float) w.getHeight()));

    g.setFont (juce::FontOptions (12.0f));
    g.setColour (juce::Colours::white);
    const double sr = source->sampleRate;
    // A short fragment: the labels go outside the flags so they don't overlap.
    const bool narrow = xOut - xIn < 220.0f;
    g.drawText ("in " + formatTime ((double) inPoint / sr),
                narrow ? juce::Rectangle<float> (xIn - 124.0f, (float) w.getY(), 120.0f, 14.0f) : juce::Rectangle<float> (xIn + 4.0f, (float) w.getY(), 120.0f, 14.0f),
                narrow ? juce::Justification::right : juce::Justification::left);
    g.drawText ("out " + formatTime ((double) outPoint / sr),
                narrow ? juce::Rectangle<float> (xOut + 4.0f, (float) w.getY(), 120.0f, 14.0f) : juce::Rectangle<float> (xOut - 124.0f, (float) w.getY(), 120.0f, 14.0f),
                narrow ? juce::Justification::left : juce::Justification::right);

    // Source playhead
    if (playPos >= 0.0)
    {
        const float x = sampleToX (playPos);
        const float dashes[] = { 3.0f, 3.0f };
        g.setColour (juce::Colours::white);
        g.drawDashedLine (juce::Line<float> (x, (float) w.getY() + 16.0f, x, (float) w.getBottom()), dashes, 2, 1.2f);
    }
}

void SourceStrip::mouseMove (const juce::MouseEvent& e)
{
    if (source == nullptr) { setMouseCursor (juce::MouseCursor::NormalCursor); return; }
    const float x = (float) e.x;
    const bool nearFlag = std::abs (x - sampleToX ((double) inPoint)) < 6.0f || std::abs (x - sampleToX ((double) outPoint)) < 6.0f;
    setMouseCursor (nearFlag ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::IBeamCursor);
}

void SourceStrip::mouseDown (const juce::MouseEvent& e)
{
    if (source == nullptr || ! waveArea().expanded (8, 6).contains (e.getPosition())) { drag = Drag::none; return; }

    const float x = (float) e.x;
    const float dIn = std::abs (x - sampleToX ((double) inPoint));
    const float dOut = std::abs (x - sampleToX ((double) outPoint));

    if (dIn < 6.0f && dIn <= dOut)       drag = Drag::inFlag;
    else if (dOut < 6.0f)                drag = Drag::outFlag;
    else { drag = Drag::newRange; dragAnchor = xToSample (x); }
}

void SourceStrip::mouseDrag (const juce::MouseEvent& e)
{
    if (drag == Drag::none || source == nullptr) return;

    const int64_t s = xToSample ((float) e.x);
    const int64_t minLen = (int64_t) (0.010 * source->sampleRate);   // at least 10 ms
    int64_t in = inPoint, out = outPoint;

    if (drag == Drag::inFlag)        in = juce::jmin (s, out - minLen);
    else if (drag == Drag::outFlag)  out = juce::jmax (s, in + minLen);
    else
    {
        in = juce::jmin (dragAnchor, s);
        out = juce::jmax (dragAnchor, s);
        if (out - in < minLen) return;
    }

    in = juce::jmax ((int64_t) 0, in);
    out = juce::jmin (source->length(), out);
    setFragment (in, out);
    if (onFragmentChanged) onFragmentChanged (in, out);
}

void SourceStrip::mouseUp (const juce::MouseEvent&)
{
    if (drag != Drag::none && onFragmentDragEnded) onFragmentDragEnded();
    drag = Drag::none;
}

} // namespace goblet
