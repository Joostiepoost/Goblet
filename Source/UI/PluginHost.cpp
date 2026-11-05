// PluginHost.cpp

#include "PluginHost.h"

namespace goblet
{

static const juce::Colour accent (0xff4a9eff);

PluginHost::PluginHost (Mixer& m, juce::ApplicationProperties& p)
    : mixer (m), properties (p)
{
    juce::addDefaultFormatsToManager (formats);   // AU and VST3 on macOS
}

void PluginHost::loadPluginList()
{
    // The plugin list from the last scan or, the first time, Effet's (or Trajet's).
    if (auto xml = properties.getUserSettings()->getXmlValue ("knownPlugins"))
        knownPlugins.recreateFromXml (*xml);
    if (knownPlugins.getNumTypes() == 0)
        importPluginList();
    knownPlugins.addChangeListener (this);
}

PluginHost::~PluginHost()
{
    *alive = false;
    knownPlugins.removeChangeListener (this);
    closeAllEditors();
    scannerWindow = nullptr;
}

//==============================================================================
juce::String PluginHost::slotName (int chain, int slot)
{
    const juce::String n (slot + 1);
    if (chain == Mixer::sumChain) return "Sum, slot " + n;
    const bool pre = chain < Mixer::numDecks;
    const auto deck = juce::String::charToString ((juce::juce_wchar) ('A' + (pre ? chain : chain - Mixer::numDecks)));
    return "Deck " + deck + (pre ? ", before the fader, slot " : ", after the fader, slot ") + n;
}

PluginHost::SlotInfo PluginHost::describe (int chain, int slot) const
{
    SlotInfo info;
    auto& c = mixer.chain (chain);
    if (auto* p = c.get (slot))
    {
        info.name = p->getName();
        info.bypassed = c.isBypassed (slot);
        const double rate = c.getSampleRate();
        info.latencyMs = rate > 0.0 ? 1000.0 * (double) p->getLatencySamples() / rate : 0.0;
    }
    if (loading[chain][slot].isNotEmpty())
    {
        info.name = loading[chain][slot];
        info.loading = true;
    }
    return info;
}

//==============================================================================
juce::File PluginHost::deadMansPedalFile() const
{
    return properties.getUserSettings()->getFile().getSiblingFile ("RecentlyCrashedPluginsList");
}

void PluginHost::importPluginList()
{
    // Effet and Trajet keep their scans in their own settings files; reuse one so the first start needs no scan.
    for (auto* app : { "Effet", "Trajet" })
    {
        juce::PropertiesFile::Options o;
        o.applicationName = app;
        o.filenameSuffix = "settings";
        o.folderName = app;
        o.osxLibrarySubFolder = "Application Support";
        juce::PropertiesFile other (o);
        if (auto xml = other.getXmlValue ("knownPlugins"))
        {
            knownPlugins.recreateFromXml (*xml);
            if (knownPlugins.getNumTypes() > 0)
            {
                properties.getUserSettings()->setValue ("knownPlugins", xml.get());
                status ("Plugin list taken from " + juce::String (app) + " (" + juce::String (knownPlugins.getNumTypes()) + " plugins). Plugins... scans again.");
                return;
            }
        }
    }
}

void PluginHost::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (auto xml = knownPlugins.createXml())
    {
        properties.getUserSettings()->setValue ("knownPlugins", xml.get());
        properties.saveIfNeeded();
    }
}

void PluginHost::showScanner()
{
    if (scannerWindow != nullptr)
    {
        scannerWindow->toFront (true);
        return;
    }

    auto* list = new juce::PluginListComponent (formats, knownPlugins, deadMansPedalFile(),
                                                properties.getUserSettings(), true);
    list->setSize (760, 520);

    struct ScanWindow : juce::DocumentWindow
    {
        ScanWindow (juce::Component* content, std::function<void()> close)
            : DocumentWindow ("Plugins", juce::Colour (0xff2a2e33), DocumentWindow::closeButton), onClose (std::move (close))
        {
            setUsingNativeTitleBar (true);
            setContentOwned (content, true);
            setResizable (true, false);
            centreWithSize (content->getWidth(), content->getHeight());
            setVisible (true);
        }
        void closeButtonPressed() override { if (onClose) onClose(); }
        std::function<void()> onClose;
    };

    std::weak_ptr<bool> stillAlive = alive;
    scannerWindow = std::make_unique<ScanWindow> (list, [this, stillAlive]
    {
        juce::MessageManager::callAsync ([this, stillAlive] { if (! stillAlive.expired()) scannerWindow = nullptr; });
    });
}

//==============================================================================
void PluginHost::showSlotMenu (int chain, int slot, juce::Component& target)
{
    auto types = knownPlugins.getTypes();
    auto& c = mixer.chain (chain);
    auto* current = dynamic_cast<juce::AudioPluginInstance*> (c.get (slot));

    juce::PopupMenu plugins;
    if (types.isEmpty())
        plugins.addItem (-1, "No plugins yet: click Plugins... and scan", false);
    else
        juce::KnownPluginList::addToMenu (plugins, types, juce::KnownPluginList::sortByManufacturer,
                                          current != nullptr ? current->getPluginDescription().createIdentifierString() : juce::String());

    juce::PopupMenu menu;
    menu.addSectionHeader (slotName (chain, slot));

    if (current != nullptr)
    {
        menu.addItem (1, "Open window");
        menu.addItem (3, "Bypass", true, c.isBypassed (slot));
        menu.addSubMenu ("Replace with", plugins);
        menu.addItem (4, slot == 0 ? "Move after slot 2" : "Move before slot 1");
        menu.addSeparator();
        menu.addItem (2, "Remove");
    }
    else
    {
        for (juce::PopupMenu::MenuItemIterator it (plugins); it.next();)
            menu.addItem (it.getItem());
    }

    std::weak_ptr<bool> stillAlive = alive;
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target),
                        [this, stillAlive, types, chain, slot] (int result)
                        {
                            if (stillAlive.expired() || result == 0) return;
                            auto& ch = mixer.chain (chain);

                            if (result == 1) { openEditor (chain, slot); return; }
                            if (result == 2) { removePlugin (chain, slot); return; }
                            if (result == 3)
                            {
                                ch.setBypass (slot, ! ch.isBypassed (slot));
                                changed();
                                save();
                                return;
                            }
                            if (result == 4)
                            {
                                closeEditor (chain, 0);
                                closeEditor (chain, 1);
                                ch.swapSlots();
                                changed();
                                save();
                                return;
                            }

                            const int index = juce::KnownPluginList::getIndexChosenByMenu (types, result);
                            if (juce::isPositiveAndBelow (index, types.size()))
                                loadPlugin (types.getReference (index), chain, slot, nullptr, false);
                        });
}

void PluginHost::loadPlugin (const juce::PluginDescription& desc, int chain, int slot, const juce::MemoryBlock* state, bool bypass)
{
    loading[chain][slot] = desc.name;
    ++pendingLoads;
    changed();
    status ("Loading " + desc.name + "...");

    std::shared_ptr<juce::MemoryBlock> savedState;
    if (state != nullptr) savedState = std::make_shared<juce::MemoryBlock> (*state);
    std::weak_ptr<bool> stillAlive = alive;
    auto& c = mixer.chain (chain);

    formats.createPluginInstanceAsync (desc, c.getSampleRate(), c.getBlockSize(),
        [this, stillAlive, desc, chain, slot, savedState, bypass] (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
        {
            if (stillAlive.expired()) return;
            loading[chain][slot] = {};
            --pendingLoads;

            if (instance == nullptr)
            {
                status ("Could not load " + desc.name + (error.isNotEmpty() ? ": " + error : juce::String()));
                changed();
                return;
            }

            if (savedState != nullptr && savedState->getSize() > 0)
                instance->setStateInformation (savedState->getData(), (int) savedState->getSize());

            closeEditor (chain, slot);
            auto& ch = mixer.chain (chain);
            ch.setBypass (slot, bypass);
            std::unique_ptr<juce::AudioProcessor> replaced;
            juce::String err;
            if (! ch.insert (slot, std::move (instance), replaced, err))
                status (err);
            else
                status ("Loaded " + desc.name + ": " + slotName (chain, slot));

            replaced = nullptr;   // the old plugin goes here, on the message thread
            changed();
            if (savedState == nullptr) save();
        });
}

void PluginHost::openEditor (int chain, int slot)
{
    auto* p = mixer.chain (chain).get (slot);
    if (p == nullptr) return;

    auto& window = editors[chain][slot];
    if (window != nullptr) { window->toFront (true); return; }

    std::weak_ptr<bool> stillAlive = alive;
    window = std::make_unique<PluginWindow> (*p, slotName (chain, slot) + ": " + p->getName(), [this, stillAlive, chain, slot]
    {
        juce::MessageManager::callAsync ([this, stillAlive, chain, slot]
        {
            if (stillAlive.expired()) return;
            closeEditor (chain, slot);
            save();   // the plugin's settings may have changed
        });
    });
}

void PluginHost::closeEditor (int chain, int slot)
{
    editors[chain][slot] = nullptr;
}

void PluginHost::closeAllEditors()
{
    for (auto& c : editors)
        for (auto& e : c)
            e = nullptr;
}

void PluginHost::removePlugin (int chain, int slot)
{
    closeEditor (chain, slot);
    auto old = mixer.chain (chain).remove (slot);
    mixer.chain (chain).setBypass (slot, false);
    old = nullptr;
    changed();
    save();
}

//==============================================================================
void PluginHost::writeTo (juce::PropertySet& s) const
{
    juce::XmlElement xml ("PLUGINS");
    for (int c = 0; c < Mixer::numChains; ++c)
        if (mixer.chain (c).numPlugins() > 0)
        {
            auto e = mixer.chain (c).createXml ("CHAIN");
            e->setAttribute ("id", c);
            xml.addChildElement (e.release());
        }
    s.setValue ("plugins", &xml);
}

void PluginHost::save()
{
    if (pendingLoads > 0) return;    // restoring: the settings still hold the full set
    writeTo (*properties.getUserSettings());
    properties.saveIfNeeded();
}

void PluginHost::removeAll()
{
    for (int c = 0; c < Mixer::numChains; ++c)
        for (int k = 0; k < slotsPerChain; ++k)
            if (mixer.chain (c).get (k) != nullptr)
            {
                closeEditor (c, k);
                auto old = mixer.chain (c).remove (k);
                mixer.chain (c).setBypass (k, false);
                old = nullptr;
            }
    changed();
}

void PluginHost::restore()
{
    restoreFrom (*properties.getUserSettings(), false);
}

void PluginHost::restoreFrom (const juce::PropertySet& s, bool removeCurrentFirst)
{
    if (removeCurrentFirst) removeAll();

    auto xml = s.getXmlValue ("plugins");
    if (xml == nullptr) return;

    for (auto* ce : xml->getChildWithTagNameIterator ("CHAIN"))
    {
        const int c = ce->getIntAttribute ("id", -1);
        if (! juce::isPositiveAndBelow (c, Mixer::numChains)) continue;

        for (auto* e : ce->getChildWithTagNameIterator ("SLOT"))
            if (auto* d = e->getChildByName ("PLUGIN"))
            {
                juce::PluginDescription desc;
                if (! desc.loadFromXml (*d)) continue;
                const int slot = e->getIntAttribute ("index", -1);
                if (! juce::isPositiveAndBelow (slot, slotsPerChain)) continue;
                juce::MemoryBlock state;
                state.fromBase64Encoding (e->getStringAttribute ("state"));
                loadPlugin (desc, c, slot, &state, e->getBoolAttribute ("bypass", false));
            }
    }
}

//==============================================================================
SlotButton::SlotButton (PluginHost& h, int c, int s, bool isCompact)
    : host (h), chain (c), slot (s), compact (isCompact)
{
    refresh();
}

void SlotButton::refresh()
{
    info = host.describe (chain, slot);
    juce::String tip = PluginHost::slotName (chain, slot) + ": ";
    if (info.name.isEmpty()) tip << "empty. Click to load a plugin.";
    else
    {
        tip << info.name << (info.loading ? " (loading)" : "") << (info.bypassed ? " (bypassed)" : "");
        if (info.latencyMs > 0.0) tip << ", latency " << juce::String (info.latencyMs, 1) << " ms";
        tip << ". Click to open its window; right-click for more.";
    }
    setTooltip (tip);
    repaint();
}

void SlotButton::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const bool empty = info.name.isEmpty();

    g.setColour (empty ? juce::Colours::transparentBlack : juce::Colours::black.withAlpha (0.25f));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (juce::Colours::white.withAlpha (empty ? 0.2f : (info.bypassed ? 0.3f : 0.45f)));
    g.drawRoundedRectangle (r, 5.0f, 1.0f);

    auto t = getLocalBounds().reduced (compact ? 5 : 9, 0);
    auto arrow = t.removeFromRight (compact ? 8 : 12);
    g.setColour (juce::Colours::white.withAlpha (0.4f));
    juce::Path p;
    const float cx = (float) arrow.getCentreX(), cy = (float) arrow.getCentreY();
    p.addTriangle (cx - 3.5f, cy - 2.0f, cx + 3.5f, cy - 2.0f, cx, cy + 2.5f);
    g.fillPath (p);

    juce::String text = compact ? juce::String() : juce::String (slot + 1) + juce::String::fromUTF8 (" \xc2\xb7 ");
    text << (empty ? "empty" : info.name);
    if (info.loading) text << "...";

    g.setFont (juce::FontOptions (compact ? 11.5f : 12.5f));
    g.setColour (juce::Colours::white.withAlpha (empty ? 0.35f : (info.bypassed || info.loading ? 0.45f : 0.95f)));
    g.drawFittedText (text, t, compact ? juce::Justification::centred : juce::Justification::centredLeft, 1, 0.8f);

    if (info.bypassed && ! empty)
    {
        // A line through the name: the plugin runs but is not heard.
        const float y = (float) t.getCentreY();
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        g.drawHorizontalLine ((int) y, (float) t.getX(), (float) t.getRight());
    }
}

void SlotButton::mouseDown (const juce::MouseEvent& e)
{
    const bool onArrow = e.x > getWidth() - (compact ? 14 : 22);
    if (e.mods.isPopupMenu() || onArrow || info.name.isEmpty())
        host.showSlotMenu (chain, slot, *this);
    else
        host.openEditor (chain, slot);
}

} // namespace goblet
