// MidiControl.cpp

#include "MidiControl.h"
#include "Mixer.h"

namespace goblet
{

MidiEngine::MidiEngine() = default;
MidiEngine::~MidiEngine() = default;

void MidiEngine::setMappings (const std::vector<MidiMapping>& mappings)
{
    auto set = std::make_unique<Set>();
    for (auto& m : mappings)
        if (juce::isPositiveAndBelow (m.row, maxMappings))
            set->mappings.push_back (m);

    // The audio thread reads the pointer once per block, so a set replaced more
    // than two seconds ago is no longer in use.
    const double now = juce::Time::getMillisecondCounterHiRes();
    current.store (set.get(), std::memory_order_release);
    if (owned != nullptr) retired.push_back ({ std::move (owned), now });
    owned = std::move (set);
    retired.erase (std::remove_if (retired.begin(), retired.end(),
                                   [now] (const Retired& r) { return now - r.time > 2000.0; }),
                   retired.end());
}

float MidiEngine::currentValue (Mixer& mixer, const MidiTarget& t) const noexcept
{
    switch (t.kind)
    {
        case MidiTarget::Kind::fader:  return mixer.getFaderPosition (t.deck);
        case MidiTarget::Kind::trim:   return Mixer::trimDbToNormal (mixer.getTrimDb (t.deck));
        case MidiTarget::Kind::plugin: return mixer.chain (t.chain).getParameter (t.slot, t.index);
        case MidiTarget::Kind::none:
        default: return -1.0f;
    }
}

void MidiEngine::apply (Mixer& mixer, const MidiMapping& m, float value) noexcept
{
    auto& s = states[(size_t) m.row];
    const auto& t = m.target;

    switch (t.kind)
    {
        case MidiTarget::Kind::fader:
            mixer.setFaderPosition (t.deck, value, juce::jmax (1.0f, m.smoothMs));
            s.lastApplied = value;
            break;

        case MidiTarget::Kind::trim:
            mixer.setTrimDb (t.deck, Mixer::normalToTrimDb (value), juce::jmax (1.0f, m.smoothMs));
            s.lastApplied = value;
            break;

        case MidiTarget::Kind::plugin:
            s.goal = value;
            if (m.smoothMs <= 0.0f || s.current < 0.0f)
            {
                s.current = value;
                mixer.chain (t.chain).setParameter (t.slot, t.index, value);
                s.lastApplied = value;
            }
            break;

        case MidiTarget::Kind::none:
        default: break;
    }
}

void MidiEngine::process (Mixer& mixer, const CcTable& table, int numSamples, double sampleRate) noexcept
{
    const Set* set = current.load (std::memory_order_acquire);
    const double dt = (double) numSamples / juce::jmax (1.0, sampleRate);
    clock += dt;

    if (set != lastSet)
    {
        lastSet = set;
        if (set != nullptr)
            for (auto& m : set->mappings)
            {
                auto& s = states[(size_t) m.row];
                if (! s.last.sameAs (m))
                {
                    s = State();
                    waiting[(size_t) m.row].store (false);
                }
                s.last = m;
            }
    }

    if (set == nullptr) return;

    for (auto& m : set->mappings)
    {
        const auto row = (size_t) m.row;
        auto& s = states[row];

        if (! m.enabled || m.target.kind == MidiTarget::Kind::none)
        {
            active[row].store (false);
            continue;
        }

        const uint64_t seq = m.sequence (table);

        if (! s.seen)
        {
            // A value that arrived before the mapping existed is not a move.
            s.seq = seq;
            s.seen = true;
        }
        else if (seq != s.seq)
        {
            s.seq = seq;
            const float x = m.read (table);

            if (x >= 0.0f)
            {
                const float value = m.map (x);
                live[row].store (value);
                s.lastMove = clock;
                bool takeOver = true;

                if (m.pickup)
                {
                    const float cur = currentValue (mixer, m.target);
                    if (cur >= 0.0f)
                    {
                        // Moved elsewhere since this mapping last set it: wait to pick it up again.
                        if (s.owns && s.lastApplied >= 0.0f && std::abs (cur - s.lastApplied) > 0.002f)
                            s.owns = false;

                        if (! s.owns && (std::abs (value - cur) < 0.02f
                                          || (s.previous >= 0.0f && (s.previous - cur) * (value - cur) <= 0.0f)))
                            s.owns = true;
                    }
                    else s.owns = true;

                    s.previous = value;
                    takeOver = s.owns;
                    waiting[row].store (! s.owns);
                }

                if (takeOver)
                    apply (mixer, m, value);
            }
        }

        // Plugin parameters glide towards the controller's value.
        if (m.target.kind == MidiTarget::Kind::plugin && s.goal >= 0.0f && s.current >= 0.0f && ! juce::exactlyEqual (s.current, s.goal))
        {
            const double tau = juce::jmax (0.001, (double) m.smoothMs * 0.001 / 3.0);
            const float alpha = (float) (1.0 - std::exp (-dt / tau));
            s.current += alpha * (s.goal - s.current);
            if (std::abs (s.goal - s.current) < 1.0e-4f) s.current = s.goal;
            mixer.chain (m.target.chain).setParameter (m.target.slot, m.target.index, s.current);
            s.lastApplied = s.current;
        }

        active[row].store (clock - s.lastMove < 0.5);
    }
}

} // namespace goblet
