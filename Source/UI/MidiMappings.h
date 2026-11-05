// MidiMappings.h — the MIDI mappings as the user edits them (a ValueTree),
// saved in the settings and handed to the MidiEngine whenever they change.

#pragma once

#include <JuceHeader.h>
#include "../Engine/Mixer.h"

namespace goblet
{

namespace IDs
{
    #define GOBLET_ID(name) static const juce::Identifier name (#name);
    GOBLET_ID (MAPPINGS) GOBLET_ID (MAPPING)
    GOBLET_ID (enabled) GOBLET_ID (channel) GOBLET_ID (cc) GOBLET_ID (fine)
    GOBLET_ID (inMin) GOBLET_ID (inMax) GOBLET_ID (outMin) GOBLET_ID (outMax)
    GOBLET_ID (curve) GOBLET_ID (smoothMs) GOBLET_ID (pickup)
    GOBLET_ID (kind) GOBLET_ID (deck) GOBLET_ID (chain) GOBLET_ID (slot) GOBLET_ID (index) GOBLET_ID (name)
    #undef GOBLET_ID
}

class MidiMappings : private juce::ValueTree::Listener
{
public:
    explicit MidiMappings (Mixer& m);
    ~MidiMappings() override;

    juce::ValueTree& getTree() noexcept     { return tree; }
    juce::ValueTree add();                  // a new mapping with sensible defaults
    int size() const                        { return tree.getNumChildren(); }

    static MidiTarget targetOf (const juce::ValueTree& m);
    static void setTarget (juce::ValueTree& m, const MidiTarget&, const juce::String& name);

    // The parameter a plugin mapping drives, or nullptr if its plugin is gone.
    juce::AudioProcessorParameter* parameterFor (const juce::ValueTree& m) const;

    // Target values as text and back (fader and trim in dB, plugins in their own units).
    juce::String valueText (const juce::ValueTree& m, float normalised) const;
    bool valueFromText (const juce::ValueTree& m, const juce::String& text, float& out) const;

    std::unique_ptr<juce::XmlElement> createXml() const   { return tree.createXml(); }
    void restore (const juce::XmlElement*);

    std::function<void()> onChanged;        // save the settings

private:
    void push();
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override   { push(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override               { push(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override        { push(); }
    void valueTreeRedirected (juce::ValueTree&) override                                 { push(); }

    Mixer& mixer;
    juce::ValueTree tree { IDs::MAPPINGS };
};

} // namespace goblet
