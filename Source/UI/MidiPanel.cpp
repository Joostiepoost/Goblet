// MidiPanel.cpp — see MidiPanel.h.

#include "MidiPanel.h"

namespace goblet
{

namespace
{
    const juce::Colour rowColour (0xff23272d), rowAltColour (0xff262a31), activeColour (0xff27415a), accent (0xff6cc3ff);

    /** A value field: click to type, or drag up/down to change the value. */
    class DragField : public juce::Label
    {
    public:
        std::function<double()> getValue;               // 0..1
        std::function<void (double)> setValue;          // 0..1, while dragging
        double step = 1.0 / 127.0;                      // per pixel of drag (Shift: finer)

        void mouseDown (const juce::MouseEvent& e) override
        {
            dragged = false;
            startValue = getValue ? getValue() : 0.0;
            juce::Label::mouseDown (e);
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (! setValue || std::abs (e.getDistanceFromDragStartY()) < 3) return;
            dragged = true;
            const double perPixel = e.mods.isShiftDown() ? step * 0.1 : step;
            setValue (juce::jlimit (0.0, 1.0, startValue - e.getDistanceFromDragStartY() * perPixel));
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (dragged) { dragged = false; return; }      // a drag is not a click to edit
            juce::Label::mouseUp (e);
        }

    private:
        double startValue = 0.0;
        bool dragged = false;
    };

    void styleField (juce::Label& l)
    {
        l.setEditable (true, true, false);
        l.setJustificationType (juce::Justification::centred);
        l.setColour (juce::Label::backgroundColourId, juce::Colour (0xff15171b));
        l.setColour (juce::Label::outlineColourId, juce::Colours::white.withAlpha (0.12f));
        l.setFont (juce::FontOptions (13.0f));
    }

    bool typedNumber (const juce::String& text, double& out)
    {
        const auto t = text.trim().replaceCharacter (',', '.');
        if (! t.containsAnyOf ("0123456789")) return false;
        out = t.retainCharacters ("0123456789.-").getDoubleValue();
        return true;
    }
}

//==============================================================================
// Columns of a mapping row (width, gap after), shared by the rows and the header.
namespace Columns
{
    enum { enabled, channel, cc, fine, target, inLow, inHigh, outLow, outHigh, curve, smooth, takeover, meter, removeButton, count };
    constexpr int width[count] = { 26, 78, 98, 66, 230, 54, 54, 96, 96, 62, 66, 76, 70, 30 };
    constexpr int gap[count]   = { 4, 6, 6, 8, 10, 2, 10, 2, 10, 6, 6, 10, 6, 0 };
    constexpr int margin = 6;
    inline int x (int column)
    {
        int pos = margin;
        for (int i = 0; i < column; ++i) pos += width[i] + gap[i];
        return pos;
    }
    inline int total() { return x (count - 1) + width[count - 1] + margin; }
}

//==============================================================================
class MidiPanel::Row : public juce::Component,
                       private juce::ValueTree::Listener
{
public:
    Row (MidiPanel& o, juce::ValueTree mapping, int rowIndex) : owner (o), m (mapping), index (rowIndex)
    {
        enabled.setTooltip ("On or off");
        enabled.onClick = [this] { set (IDs::enabled, enabled.getToggleState()); };

        channelBox.addItem ("Any ch", 1);
        for (int c = 1; c <= 16; ++c) channelBox.addItem ("ch " + juce::String (c), c + 1);
        channelBox.onChange = [this] { set (IDs::channel, channelBox.getSelectedId() - 1); };
        channelBox.setTooltip ("MIDI channel (Any: every channel)");

        ccBox.onChange = [this] { set (IDs::cc, ccBox.getSelectedId() - 1); };
        ccBox.setTooltip ("The controller number. Watch the Incoming list to see what your controller sends.");

        fineButton.setTooltip ("14-bit: CC 0-31 sends the coarse part, CC 32-63 the fine part (16384 steps instead of 128). "
                               "For controllers that send such pairs.");
        fineButton.onClick = [this]
        {
            const bool fine = fineButton.getToggleState();
            set (IDs::fine, fine);
            if (fine && (int) m[IDs::cc] > 31) set (IDs::cc, (int) m[IDs::cc] % 32);
        };

        targetButton.onClick = [this] { owner.chooseTarget (targetButton, m); };
        targetButton.setTooltip ("What the controller drives: a deck's fader or trim, or a parameter of a loaded plugin");

        for (auto* l : std::initializer_list<juce::Label*> { &inLow, &inHigh, &outLow, &outHigh, &smooth })
            styleField (*l);
        inLow.setTooltip ("The controller value that gives the start of the range. Click to type, or drag up/down (Shift: finer).");
        inHigh.setTooltip ("The controller value that gives the end of the range. Click to type, or drag up/down (Shift: finer).");
        outLow.setTooltip ("Target value at the start of the controller's range (faders and trims in dB, plugins in their own units). "
                           "Click to type, or drag up/down (Shift: finer).");
        outHigh.setTooltip ("Target value at the end of the controller's range. Lower than the start reverses the control: "
                            "map one CC to two faders, one reversed, for a crossfade.");

        auto bindDrag = [this] (DragField& f, const juce::Identifier& id, double fallback, double step)
        {
            f.getValue = [this, id, fallback] { return (double) m.getProperty (id, fallback); };
            f.setValue = [this, id] (double v) { set (id, v); };
            f.step = step;
        };
        bindDrag (inLow, IDs::inMin, 0.0, 1.0 / 127.0);
        bindDrag (inHigh, IDs::inMax, 1.0, 1.0 / 127.0);
        bindDrag (outLow, IDs::outMin, 0.0, 1.0 / 200.0);
        bindDrag (outHigh, IDs::outMax, 1.0, 1.0 / 200.0);
        smooth.setTooltip ("Smoothing time in ms, so the steps of a 7-bit controller are not heard (faders and trims: at least 1 ms)");

        // Typed values are applied on Return or when the field loses focus; an empty or
        // unreadable entry leaves the value as it was.
        inLow.onTextChange  = [this] { double v; if (typedNumber (inLow.getText(), v))  set (IDs::inMin, juce::jlimit (0.0, 1.0, v / maxRaw())); refresh(); };
        inHigh.onTextChange = [this] { double v; if (typedNumber (inHigh.getText(), v)) set (IDs::inMax, juce::jlimit (0.0, 1.0, v / maxRaw())); refresh(); };
        outLow.onTextChange  = [this] { float v; if (owner.mappings.valueFromText (m, outLow.getText(), v))  set (IDs::outMin, (double) v); refresh(); };
        outHigh.onTextChange = [this] { float v; if (owner.mappings.valueFromText (m, outHigh.getText(), v)) set (IDs::outMax, (double) v); refresh(); };
        smooth.onTextChange = [this] { double v; if (typedNumber (smooth.getText(), v)) set (IDs::smoothMs, juce::jlimit (0.0, 2000.0, v)); refresh(); };

        curveBox.addItem ("lin", 1);
        curveBox.addItem ("exp", 2);
        curveBox.addItem ("log", 3);
        curveBox.setTooltip ("lin: even; exp: slow at first, fast at the end; log: fast at first. Faders already follow their dB law.");
        curveBox.onChange = [this] { set (IDs::curve, curveBox.getSelectedId() - 1); };

        takeoverBox.addItem ("Jump", 1);
        takeoverBox.addItem ("Pickup", 2);
        takeoverBox.setTooltip ("Jump: the target follows the controller as soon as it moves. Pickup: after the target was moved "
                                "with the mouse (or by a snapshot), the controller takes over only once it passes the target's value.");
        takeoverBox.onChange = [this] { set (IDs::pickup, takeoverBox.getSelectedId() == 2); };

        deleteButton.setTooltip ("Remove this mapping");
        deleteButton.onClick = [this]
        {
            auto parent = m.getParent();
            juce::MessageManager::callAsync ([parent, tree = m]() mutable
            {
                if (parent.isValid()) parent.removeChild (tree, nullptr);
            });
        };

        for (auto* c : std::initializer_list<juce::Component*> { &enabled, &channelBox, &ccBox, &fineButton, &targetButton, &inLow, &inHigh,
                                                                 &outLow, &outHigh, &curveBox, &smooth, &takeoverBox, &deleteButton })
            addAndMakeVisible (c);

        m.addListener (this);
        refresh();
    }

    ~Row() override { m.removeListener (this); }

    /** Shows the mapping's settings. Never touches a field being typed in or a menu
        that is open (setting a label's text would close its editor). */
    void refresh()
    {
        const bool fine = m[IDs::fine];
        enabled.setToggleState (m.getProperty (IDs::enabled, true), juce::dontSendNotification);
        if (! channelBox.isPopupActive())
            channelBox.setSelectedId ((int) m.getProperty (IDs::channel, 0) + 1, juce::dontSendNotification);

        if (! ccBox.isPopupActive())
        {
            if (ccBox.getNumItems() == 0 || fine != ccListFine)
            {
                ccListFine = fine;
                ccBox.clear (juce::dontSendNotification);
                for (int c = 0; c < (fine ? 32 : 128); ++c)
                    ccBox.addItem (fine ? "CC " + juce::String (c) + "+" + juce::String (c + 32) : "CC " + juce::String (c), c + 1);
            }
            ccBox.setSelectedId ((int) m.getProperty (IDs::cc, 1) + 1, juce::dontSendNotification);
        }
        fineButton.setToggleState (fine, juce::dontSendNotification);

        const auto name = m[IDs::name].toString();
        const auto kind = MidiMappings::targetOf (m).kind;
        const bool found = kind != MidiTarget::Kind::plugin || owner.mappings.parameterFor (m) != nullptr;
        targetButton.setButtonText (kind == MidiTarget::Kind::none || name.isEmpty() ? juce::String ("Choose target...")
                                                                                    : name + (found ? "" : "  (not found)"));
        targetButton.setColour (juce::TextButton::textColourOffId, ! found ? juce::Colours::orange : juce::Colours::white);

        auto show = [] (juce::Label& l, const juce::String& text)
        {
            if (! l.isBeingEdited() && l.getText() != text)
                l.setText (text, juce::dontSendNotification);
        };
        show (inLow, juce::String (juce::roundToInt ((double) m.getProperty (IDs::inMin, 0.0) * maxRaw())));
        show (inHigh, juce::String (juce::roundToInt ((double) m.getProperty (IDs::inMax, 1.0) * maxRaw())));
        show (outLow, owner.mappings.valueText (m, (float) (double) m.getProperty (IDs::outMin, 0.0)));
        show (outHigh, owner.mappings.valueText (m, (float) (double) m.getProperty (IDs::outMax, 1.0)));
        if (! curveBox.isPopupActive())
            curveBox.setSelectedId ((int) m.getProperty (IDs::curve, 0) + 1, juce::dontSendNotification);
        show (smooth, juce::String ((int) (double) m.getProperty (IDs::smoothMs, 20.0)) + " ms");
        if (! takeoverBox.isPopupActive())
            takeoverBox.setSelectedId ((bool) m.getProperty (IDs::pickup, false) ? 2 : 1, juce::dontSendNotification);
        repaint();
    }

    void setLive (bool isActive, float value, bool isWaiting)
    {
        if (isActive == active && waitingForPickup == isWaiting && std::abs (value - meterValue) < 1.0e-4f) return;
        active = isActive;
        meterValue = value;
        waitingForPickup = isWaiting;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (active ? activeColour : (index % 2 == 0 ? rowColour : rowAltColour));
        auto meter = juce::Rectangle<int> (Columns::x (Columns::meter), 14, Columns::width[Columns::meter], getHeight() - 28);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRoundedRectangle (meter.toFloat(), 3.0f);
        if (active || waitingForPickup)
        {
            g.setColour (waitingForPickup ? juce::Colours::orange.withAlpha (0.8f) : accent);
            g.fillRoundedRectangle (meter.toFloat().withWidth ((float) meter.getWidth() * meterValue), 3.0f);
        }
    }

    void resized() override
    {
        using namespace Columns;
        auto place = [this] (juce::Component& c, int column)
        {
            c.setBounds (Columns::x (column), 7, Columns::width[column], getHeight() - 14);
        };
        place (enabled, Columns::enabled);
        place (channelBox, channel);
        place (ccBox, cc);
        place (fineButton, fine);
        place (targetButton, target);
        place (inLow, Columns::inLow);
        place (inHigh, Columns::inHigh);
        place (outLow, Columns::outLow);
        place (outHigh, Columns::outHigh);
        place (curveBox, curve);
        place (smooth, Columns::smooth);
        place (takeoverBox, takeover);
        place (deleteButton, removeButton);
    }

    static constexpr int height = 40;

private:
    void set (const juce::Identifier& id, const juce::var& v)   { m.setProperty (id, v, nullptr); }
    double maxRaw() const                                         { return (bool) m[IDs::fine] ? 16383.0 : 127.0; }

    void valueTreePropertyChanged (juce::ValueTree& t, const juce::Identifier&) override
    {
        if (t == m) refresh();
    }

    MidiPanel& owner;
    juce::ValueTree m;
    int index;
    juce::ToggleButton enabled, fineButton { "14-bit" };
    juce::ComboBox channelBox, ccBox, curveBox, takeoverBox;
    juce::TextButton targetButton, deleteButton { juce::String::fromUTF8 ("\xc3\x97") };
    DragField inLow, inHigh, outLow, outHigh;
    juce::Label smooth;
    bool active = false, ccListFine = false, waitingForPickup = false;
    float meterValue = 0.0f;
};

//==============================================================================
MidiPanel::MidiPanel (MidiMappings& mm, Mixer& mx)
    : mappings (mm), mixer (mx), table (mx.getCcTable())
{
    deviceBox.onChange = [this]
    {
        const int i = deviceBox.getSelectedId();
        const juce::String choice = i == 1 ? juce::String() : i == 2 ? juce::String ("none") : deviceIds[i - 3];
        if (onDeviceChosen) onDeviceChosen (choice);
    };
    deviceBox.setTooltip ("Where MIDI comes from");
    addAndMakeVisible (deviceBox);

    addButton.onClick = [this] { mappings.add(); };
    addAndMakeVisible (addButton);

    countLabel.setJustificationType (juce::Justification::centredRight);
    countLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.6f));
    addAndMakeVisible (countLabel);

    footerLabel.setText ("Nothing is assigned by moving a control: choose the CC in its menu. One CC may drive several targets; "
                         "a reversed Out range (start higher than end) turns a fader down as the controller goes up, so one CC "
                         "mapped to two faders, one reversed, crossfades between two decks. Orange: a Pickup mapping waiting for "
                         "its controller to reach the target's value.", juce::dontSendNotification);
    footerLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.55f));
    footerLabel.setFont (juce::FontOptions (12.5f));
    footerLabel.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (footerLabel);

    const char* heads[] = { "", "Input", "", "", "Target", "In (CC value)", "Out (target)", "Curve", "Smooth", "Takeover" };
    for (int i = 0; i < 10; ++i)
    {
        headerLabels[i].setText (heads[i], juce::dontSendNotification);
        headerLabels[i].setFont (juce::FontOptions (12.5f, juce::Font::bold));
        headerLabels[i].setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
        addAndMakeVisible (headerLabels[i]);
    }

    viewport.setViewedComponent (&rowHolder, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    mappings.getTree().addListener (this);
    startTimerHz (25);
}

MidiPanel::~MidiPanel()
{
    mappings.getTree().removeListener (this);
}

void MidiPanel::refreshDevices()
{
    const auto devices = juce::MidiInput::getAvailableDevices();
    const auto choice = getDeviceChoice ? getDeviceChoice() : juce::String();
    deviceBox.clear (juce::dontSendNotification);
    deviceIds.clear();
    deviceBox.addItem ("All MIDI inputs", 1);
    deviceBox.addItem ("No MIDI input", 2);
    int selected = choice == "none" ? 2 : 1;
    for (int i = 0; i < devices.size(); ++i)
    {
        deviceBox.addItem ("Device: " + devices[i].name, i + 3);
        deviceIds.add (devices[i].identifier);
        if (devices[i].identifier == choice) selected = i + 3;
    }
    if (choice.isNotEmpty() && choice != "none" && selected == 1)
    {
        deviceBox.addItem ("Device: (not connected)", 99);
        selected = 99;
    }
    deviceBox.setSelectedId (selected, juce::dontSendNotification);
}

void MidiPanel::chooseTarget (juce::Component& button, juce::ValueTree mapping)
{
    // Faders and trims first, then every loaded plugin with its parameters.
    juce::PopupMenu menu;
    for (int d = 0; d < Mixer::numDecks; ++d)
        menu.addItem (1 + d, "Fader " + juce::String::charToString ((juce::juce_wchar) ('A' + d)));
    menu.addSeparator();
    for (int d = 0; d < Mixer::numDecks; ++d)
        menu.addItem (11 + d, "Trim " + juce::String::charToString ((juce::juce_wchar) ('A' + d)));

    bool anyPlugin = false;
    for (int c = 0; c < Mixer::numChains; ++c)
        for (int s = 0; s < slotsPerChain; ++s)
            if (auto* p = mixer.chain (c).get (s))
            {
                if (! anyPlugin) { menu.addSeparator(); anyPlugin = true; }
                juce::PopupMenu params;
                const auto& list = p->getParameters();
                for (int i = 0; i < list.size() && i < 9999; ++i)
                    params.addItem (100000 + (c * slotsPerChain + s) * 10000 + i, list[i]->getName (64));
                if (list.isEmpty()) params.addItem (-1, "(no parameters)", false);
                menu.addSubMenu (PluginHost::slotName (c, s) + ": " + p->getName(), params);
            }

    juce::Component::SafePointer<MidiPanel> safeThis (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&button),
                        [safeThis, mapping] (int result) mutable
                        {
                            if (safeThis == nullptr || result <= 0) return;
                            MidiTarget t;
                            juce::String name;

                            if (result >= 1 && result <= Mixer::numDecks)
                            {
                                t.kind = MidiTarget::Kind::fader;
                                t.deck = result - 1;
                                name = "Fader " + juce::String::charToString ((juce::juce_wchar) ('A' + t.deck));
                            }
                            else if (result >= 11 && result < 11 + Mixer::numDecks)
                            {
                                t.kind = MidiTarget::Kind::trim;
                                t.deck = result - 11;
                                name = "Trim " + juce::String::charToString ((juce::juce_wchar) ('A' + t.deck));
                            }
                            else if (result >= 100000)
                            {
                                const int cs = (result - 100000) / 10000;
                                t.kind = MidiTarget::Kind::plugin;
                                t.chain = cs / slotsPerChain;
                                t.slot = cs % slotsPerChain;
                                t.index = (result - 100000) % 10000;
                                if (auto* p = safeThis->mixer.chain (t.chain).get (t.slot))
                                    name = p->getName() + juce::String::fromUTF8 (" \xc2\xb7 ") + p->getParameters()[t.index]->getName (64);
                            }
                            else return;

                            MidiMappings::setTarget (mapping, t, name);
                            // A new target starts with its whole range.
                            mapping.setProperty (IDs::outMin, 0.0, nullptr);
                            mapping.setProperty (IDs::outMax, 1.0, nullptr);
                        });
}

void MidiPanel::rebuildRows()
{
    rebuildPending = false;
    rows.clear();
    int i = 0;
    for (auto m : mappings.getTree())
    {
        if (i >= MidiEngine::maxMappings) break;
        rowHolder.addAndMakeVisible (rows.add (new Row (*this, m, i++)));
    }
    addButton.setEnabled (mappings.size() < MidiEngine::maxMappings);
    countLabel.setText (juce::String (rows.size()) + " of " + juce::String (MidiEngine::maxMappings) + " mappings",
                        juce::dontSendNotification);
    resized();
}

void MidiPanel::timerCallback()
{
    if (rebuildPending)
        rebuildRows();

    auto& midi = mixer.getMidi();
    for (int i = 0; i < rows.size(); ++i)
        rows[i]->setLive (midi.isActive (i), midi.getValue (i), midi.isWaiting (i));

    // Rows refresh their target text when a plugin comes or goes.
    if (++slowCounter % 25 == 0)
        for (auto* r : rows) r->refresh();

    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    bool changed = false;
    for (int cc = 0; cc < 128; ++cc)
    {
        const auto& cell = table.cell (CcTable::anyChannel, cc);
        const auto seq = cell.seq.load();
        if (seq != lastSeq[cc])
        {
            lastSeq[cc] = seq;
            lastSeen[cc] = now;
            lastValue[cc] = cell.value.load();
            changed = true;
        }
        else if (lastSeen[cc] > 0.0 && now - lastSeen[cc] < 6.0)
            changed = true;     // fading
    }
    if (changed)
        repaint (meterArea);
}

void MidiPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e2126));

    g.setColour (juce::Colours::white.withAlpha (0.1f));
    g.drawRect (tableArea.expanded (1), 1);
    g.drawVerticalLine (meterArea.getX() - 1, (float) meterArea.getY(), (float) meterArea.getBottom());

    // Incoming controllers, the most recent first.
    auto r = meterArea.reduced (12, 8);
    g.setColour (juce::Colours::white.withAlpha (0.8f));
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("Incoming", r.removeFromTop (24), juce::Justification::centredLeft);

    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    std::vector<int> recent;
    for (int cc = 0; cc < 128; ++cc)
        if (lastSeen[cc] > 0.0 && now - lastSeen[cc] < 6.0)
            recent.push_back (cc);
    std::sort (recent.begin(), recent.end(), [this] (int a, int b) { return lastSeen[a] > lastSeen[b]; });

    if (recent.empty())
    {
        g.setColour (juce::Colours::white.withAlpha (0.4f));
        g.setFont (juce::FontOptions (12.5f));
        g.drawFittedText ("Move a control on your MIDI controller: its CC numbers appear here.", r.removeFromTop (60),
                          juce::Justification::topLeft, 3);
        return;
    }

    for (int cc : recent)
    {
        if (r.getHeight() < 24) break;
        auto line = r.removeFromTop (26);
        const float fade = (float) juce::jlimit (0.25, 1.0, 1.0 - (now - lastSeen[cc] - 1.0) / 5.0);
        bool mapped = false;
        for (const auto& m : mappings.getTree())
            if ((int) m[IDs::cc] == cc || ((bool) m[IDs::fine] && (int) m[IDs::cc] + 32 == cc)) mapped = true;

        g.setColour (juce::Colours::white.withAlpha (0.85f * fade));
        g.setFont (juce::FontOptions (12.5f));
        g.drawText ("CC" + juce::String (cc), line.removeFromLeft (50), juce::Justification::centredLeft);
        auto bar = line.removeFromLeft (110).reduced (0, 8).toFloat();
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRoundedRectangle (bar, 3.0f);
        g.setColour (accent.withAlpha (fade));
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * (float) lastValue[cc] / 127.0f), 3.0f);
        g.setColour (juce::Colours::white.withAlpha (0.5f * fade));
        g.drawText (mapped ? juce::String (lastValue[cc]) : juce::String (lastValue[cc]) + "  unmapped", line.withTrimmedLeft (8),
                    juce::Justification::centredLeft);
    }
}

void MidiPanel::resized()
{
    auto r = getLocalBounds().reduced (12, 10);
    auto top = r.removeFromTop (30);
    deviceBox.setBounds (top.removeFromLeft (280));
    top.removeFromLeft (10);
    addButton.setBounds (top.removeFromLeft (110));
    countLabel.setBounds (top.removeFromRight (220));
    r.removeFromTop (10);

    footerLabel.setBounds (r.removeFromBottom (40));
    r.removeFromBottom (6);
    meterArea = r.removeFromRight (juce::jmax (220, r.getWidth() - Columns::total()));
    tableArea = r;

    auto head = r.removeFromTop (26);
    const int startColumn[] = { Columns::enabled, Columns::channel, -1, -1, Columns::target, Columns::inLow, Columns::outLow, Columns::curve, Columns::smooth, Columns::takeover };
    const int endColumn[]   = { Columns::enabled, Columns::fine,    -1, -1, Columns::target, Columns::inHigh, Columns::outHigh, Columns::curve, Columns::smooth, Columns::takeover };
    for (int i = 0; i < 10; ++i)
    {
        if (startColumn[i] < 0) { headerLabels[i].setBounds ({}); continue; }
        const int x0 = Columns::x (startColumn[i]), x1 = Columns::x (endColumn[i]) + Columns::width[endColumn[i]];
        headerLabels[i].setBounds (head.getX() + x0 - 2, head.getY(), x1 - x0 + 4, head.getHeight());
    }

    viewport.setBounds (r);
    rowHolder.setSize (juce::jmax (Columns::total(), r.getWidth() - viewport.getScrollBarThickness()), rows.size() * Row::height);
    int y = 0;
    for (auto* row : rows)
    {
        row->setBounds (0, y, rowHolder.getWidth(), Row::height);
        y += Row::height;
    }
}

} // namespace goblet
