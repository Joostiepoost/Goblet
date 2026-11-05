// MidiMappings.cpp

#include "MidiMappings.h"

namespace goblet
{

static juce::String dbText (float db)
{
    if (db <= -99.0f) return juce::String::fromUTF8 ("\xe2\x88\x92\xe2\x88\x9e dB");
    juce::String t = (db > 0.05f ? "+" : "") + juce::String (db, 1);
    return t.replace ("-", juce::String::fromUTF8 ("\xe2\x88\x92")) + " dB";
}

static bool typedNumber (const juce::String& text, double& out)
{
    const auto t = text.trim().replaceCharacter (',', '.').replace (juce::String::fromUTF8 ("\xe2\x88\x92"), "-");
    if (t.containsIgnoreCase ("inf") || t.contains (juce::String::fromUTF8 ("\xe2\x88\x9e"))) { out = -100.0; return true; }
    if (! t.containsAnyOf ("0123456789")) return false;
    out = t.retainCharacters ("0123456789.-+").getDoubleValue();
    return true;
}

MidiMappings::MidiMappings (Mixer& m) : mixer (m)
{
    tree.addListener (this);
}

MidiMappings::~MidiMappings()
{
    tree.removeListener (this);
}

juce::ValueTree MidiMappings::add()
{
    if (tree.getNumChildren() >= MidiEngine::maxMappings) return {};

    // A sensible start: the next CC after the last mapping's, and the next deck's fader.
    int cc = 16;
    bool used[Mixer::numDecks] = {};
    for (const auto& m : tree)
    {
        cc = juce::jmax (cc, (int) m.getProperty (IDs::cc, 0) + 1);
        const auto t = targetOf (m);
        if (t.kind == MidiTarget::Kind::fader) used[t.deck] = true;
    }

    juce::ValueTree m (IDs::MAPPING);
    m.setProperty (IDs::enabled, true, nullptr);
    m.setProperty (IDs::channel, 0, nullptr);
    m.setProperty (IDs::cc, juce::jmin (cc, 127), nullptr);
    m.setProperty (IDs::fine, false, nullptr);
    m.setProperty (IDs::inMin, 0.0, nullptr);
    m.setProperty (IDs::inMax, 1.0, nullptr);
    m.setProperty (IDs::outMin, 0.0, nullptr);
    m.setProperty (IDs::outMax, 1.0, nullptr);
    m.setProperty (IDs::curve, 0, nullptr);
    m.setProperty (IDs::smoothMs, 20.0, nullptr);
    m.setProperty (IDs::pickup, false, nullptr);

    for (int d = 0; d < Mixer::numDecks; ++d)
        if (! used[d])
        {
            MidiTarget t;
            t.kind = MidiTarget::Kind::fader;
            t.deck = d;
            setTarget (m, t, "Fader " + juce::String::charToString ((juce::juce_wchar) ('A' + d)));
            break;
        }

    tree.appendChild (m, nullptr);
    return m;
}

MidiTarget MidiMappings::targetOf (const juce::ValueTree& m)
{
    MidiTarget t;
    t.kind = (MidiTarget::Kind) juce::jlimit (0, 3, (int) m.getProperty (IDs::kind, 0));
    t.deck = juce::jlimit (0, Mixer::numDecks - 1, (int) m.getProperty (IDs::deck, 0));
    t.chain = juce::jlimit (0, Mixer::numChains - 1, (int) m.getProperty (IDs::chain, 0));
    t.slot = juce::jlimit (0, slotsPerChain - 1, (int) m.getProperty (IDs::slot, 0));
    t.index = (int) m.getProperty (IDs::index, -1);
    return t;
}

void MidiMappings::setTarget (juce::ValueTree& m, const MidiTarget& t, const juce::String& name)
{
    m.setProperty (IDs::kind, (int) t.kind, nullptr);
    m.setProperty (IDs::deck, t.deck, nullptr);
    m.setProperty (IDs::chain, t.chain, nullptr);
    m.setProperty (IDs::slot, t.slot, nullptr);
    m.setProperty (IDs::index, t.index, nullptr);
    m.setProperty (IDs::name, name, nullptr);
}

juce::AudioProcessorParameter* MidiMappings::parameterFor (const juce::ValueTree& m) const
{
    const auto t = targetOf (m);
    if (t.kind != MidiTarget::Kind::plugin) return nullptr;
    if (auto* p = mixer.chain (t.chain).get (t.slot))
    {
        const auto& params = p->getParameters();
        if (juce::isPositiveAndBelow (t.index, params.size()))
            return params.getUnchecked (t.index);
    }
    return nullptr;
}

juce::String MidiMappings::valueText (const juce::ValueTree& m, float v) const
{
    switch (targetOf (m).kind)
    {
        case MidiTarget::Kind::fader: return dbText (Mixer::faderPositionToDb (v));
        case MidiTarget::Kind::trim:  return dbText (Mixer::normalToTrimDb (v));
        case MidiTarget::Kind::plugin:
            if (auto* p = parameterFor (m))
            {
                auto text = p->getText (v, 24);
                if (p->getLabel().isNotEmpty() && ! text.endsWith (p->getLabel())) text << " " << p->getLabel();
                return text;
            }
            break;
        case MidiTarget::Kind::none:
        default: break;
    }
    return juce::String (juce::roundToInt (v * 100.0f)) + " %";
}

bool MidiMappings::valueFromText (const juce::ValueTree& m, const juce::String& text, float& out) const
{
    const auto t = text.trim();
    if (t.isEmpty()) return false;
    double number = 0.0;

    switch (targetOf (m).kind)
    {
        case MidiTarget::Kind::fader:
            if (! typedNumber (t, number)) return false;
            out = Mixer::faderDbToPosition ((float) juce::jlimit (-100.0, 12.0, number));
            return true;

        case MidiTarget::Kind::trim:
            if (! typedNumber (t, number)) return false;
            out = Mixer::trimDbToNormal ((float) number);
            return true;

        case MidiTarget::Kind::plugin:
            if (auto* p = parameterFor (m))
            {
                // The plugin's own reading of the text, if it reads back as what was typed;
                // otherwise a plain number is a share of the range ("50" = halfway).
                const float v = juce::jlimit (0.0f, 1.0f, p->getValueForText (t));
                const auto back = p->getText (v, 32).retainCharacters ("0123456789.-");
                const auto typed = t.retainCharacters ("0123456789.-");
                if (typed.isEmpty() || std::abs (back.getDoubleValue() - typed.getDoubleValue()) < 0.051 * std::abs (typed.getDoubleValue()) + 1.0e-3)
                {
                    out = v;
                    return true;
                }
            }
            break;

        case MidiTarget::Kind::none:
        default: break;
    }

    if (! typedNumber (t, number)) return false;
    out = juce::jlimit (0.0f, 1.0f, (float) number / 100.0f);
    return true;
}

void MidiMappings::restore (const juce::XmlElement* xml)
{
    juce::ValueTree loaded;
    if (xml != nullptr) loaded = juce::ValueTree::fromXml (*xml);
    tree.removeAllChildren (nullptr);
    if (loaded.isValid() && loaded.hasType (IDs::MAPPINGS))
        for (int i = 0; i < loaded.getNumChildren() && i < MidiEngine::maxMappings; ++i)
            tree.appendChild (loaded.getChild (i).createCopy(), nullptr);
    push();
}

void MidiMappings::push()
{
    std::vector<MidiMapping> list;
    int row = 0;
    for (const auto& m : tree)
    {
        if (row >= MidiEngine::maxMappings) break;
        MidiMapping e;
        e.row = row++;
        e.enabled = m.getProperty (IDs::enabled, true);
        e.channel = m.getProperty (IDs::channel, 0);
        e.fine = m.getProperty (IDs::fine, false);
        e.cc = juce::jlimit (0, e.fine ? 31 : 127, (int) m.getProperty (IDs::cc, 1));
        e.inLow = (float) (double) m.getProperty (IDs::inMin, 0.0);
        e.inHigh = (float) (double) m.getProperty (IDs::inMax, 1.0);
        e.outLow = (float) (double) m.getProperty (IDs::outMin, 0.0);
        e.outHigh = (float) (double) m.getProperty (IDs::outMax, 1.0);
        e.curve = (MidiMapping::Curve) juce::jlimit (0, 2, (int) m.getProperty (IDs::curve, 0));
        e.smoothMs = (float) (double) m.getProperty (IDs::smoothMs, 20.0);
        e.pickup = m.getProperty (IDs::pickup, false);
        e.target = targetOf (m);
        list.push_back (e);
    }
    mixer.getMidi().setMappings (list);
    if (onChanged) onChanged();
}

} // namespace goblet
