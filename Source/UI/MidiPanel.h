// MidiPanel.h — the MIDI mappings window (adapted from Effet).
//
// One row per mapping (up to 16): the controller (channel, CC, 14-bit), the
// target (a fader, a trim, or a plugin parameter), the part of the
// controller's travel used, the target range, a curve, a smoothing time and
// the takeover (Jump or Pickup). Nothing is assigned by moving a control: the
// CC is chosen in its menu. Beside the table, every controller that sends
// shows a bar, so you can see which number each Touché axis uses.

#pragma once

#include <JuceHeader.h>
#include "MidiMappings.h"
#include "PluginHost.h"

namespace goblet
{

class MidiPanel : public juce::Component,
                  private juce::Timer,
                  private juce::ValueTree::Listener
{
public:
    MidiPanel (MidiMappings&, Mixer&);
    ~MidiPanel() override;

    /** The input device menu: "" = all inputs, "none", or a device identifier. */
    std::function<void (const juce::String& choice)> onDeviceChosen;
    std::function<juce::String()> getDeviceChoice;

    void refreshDevices();
    void chooseTarget (juce::Component& button, juce::ValueTree mapping);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Row;

    void rebuildRows();
    void timerCallback() override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override            { rebuildPending = true; }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override     { rebuildPending = true; }
    void valueTreeRedirected (juce::ValueTree&) override                              { rebuildPending = true; }

    MidiMappings& mappings;
    Mixer& mixer;
    const CcTable& table;

    juce::ComboBox deviceBox;
    juce::StringArray deviceIds;
    juce::TextButton addButton { "+ Mapping" };
    juce::Label countLabel, footerLabel, headerLabels[10];
    juce::OwnedArray<Row> rows;
    juce::Rectangle<int> meterArea, tableArea;
    juce::Viewport viewport;
    juce::Component rowHolder;
    bool rebuildPending = true;
    int slowCounter = 0;

    // Incoming controllers (any channel): when each last changed.
    uint32_t lastSeq[128] {};
    double lastSeen[128] {};
    int lastValue[128] {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiPanel)
};

/** The window around the panel. */
class MidiWindow : public juce::DocumentWindow
{
public:
    MidiWindow (std::unique_ptr<MidiPanel> panel, std::function<void()> onCloseRequested)
        : DocumentWindow ("MIDI mappings", juce::Colour (0xff1e2126), DocumentWindow::closeButton),
          onClose (std::move (onCloseRequested))
    {
        setUsingNativeTitleBar (true);
        setContentOwned (panel.release(), true);
        setResizable (true, false);
        setResizeLimits (1300, 300, 2600, 1400);
        centreWithSize (1520, 520);
        setVisible (true);
    }

    void closeButtonPressed() override { if (onClose) onClose(); }

private:
    std::function<void()> onClose;
};

} // namespace goblet
