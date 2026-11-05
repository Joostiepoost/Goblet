// PluginHost.h — everything about plugins on the message thread: the scanned
// list, the slot menus, loading, plugin windows, saving and restoring.
//
// A slot is named by (chain, slot): chains 0-2 before each deck's fader, 3-5
// after it, 6 on the sum (see Mixer.h); slots 0 and 1 in each. Adapted from
// Effet's MainComponent.

#pragma once

#include <JuceHeader.h>
#include "../Engine/Mixer.h"
#include "PluginWindow.h"

namespace goblet
{

class PluginHost : private juce::ChangeListener
{
public:
    PluginHost (Mixer& mixer, juce::ApplicationProperties& properties);
    ~PluginHost() override;

    void loadPluginList();            // once the settings file is set up

    // What a slot shows: the plugin's name (empty = no plugin), loading, bypassed, latency.
    struct SlotInfo { juce::String name; bool loading = false, bypassed = false; double latencyMs = 0.0; };
    SlotInfo describe (int chain, int slot) const;
    static juce::String slotName (int chain, int slot);    // "Deck A, before the fader, slot 1"

    void showSlotMenu (int chain, int slot, juce::Component& target);
    void openEditor (int chain, int slot);
    void showScanner();

    // All chains as XML (for the settings file or a snapshot), and back.
    void save();                      // into the settings, now
    void restore();                   // from the settings, at start-up
    void writeTo (juce::PropertySet&) const;
    void restoreFrom (const juce::PropertySet&, bool removeCurrentFirst);
    void removeAll();
    bool isLoading() const noexcept   { return pendingLoads > 0; }
    juce::AudioPluginFormatManager& getFormats() noexcept   { return formats; }
    void closeAllEditors();

    std::function<void()> onChanged;                       // repaint the slot buttons
    std::function<void (const juce::String&)> onStatus;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void loadPlugin (const juce::PluginDescription&, int chain, int slot, const juce::MemoryBlock* state, bool bypass);
    void removePlugin (int chain, int slot);
    void closeEditor (int chain, int slot);
    void importPluginList();
    juce::File deadMansPedalFile() const;
    void changed()                       { if (onChanged) onChanged(); }
    void status (const juce::String& s)  { if (onStatus) onStatus (s); }

    Mixer& mixer;
    juce::ApplicationProperties& properties;
    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList knownPlugins;
    std::unique_ptr<PluginWindow> editors[Mixer::numChains][slotsPerChain];
    juce::String loading[Mixer::numChains][slotsPerChain];
    std::unique_ptr<juce::DocumentWindow> scannerWindow;
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
    int pendingLoads = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginHost)
};

// A slot on screen: "1 · Plugin name", or "1 · empty". Click an empty slot
// for the plugin menu, a filled one to open its window; right-click (or the
// small arrow) for the menu: open, replace, bypass, move, remove.
class SlotButton : public juce::Component,
                   public juce::SettableTooltipClient
{
public:
    SlotButton (PluginHost& host, int chain, int slot, bool compact = false);

    void refresh();
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    PluginHost& host;
    const int chain, slot;
    const bool compact;
    PluginHost::SlotInfo info;
};

} // namespace goblet
