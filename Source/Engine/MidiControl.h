// MidiControl.h — MIDI controllers driving the faders, trims and plugin
// parameters (adapted from Effet).
//
// MIDI arrives on its own thread. It only writes into a table of the latest
// value per channel and controller (CcTable); the audio thread reads that table
// at the start of every block, so no lock is ever shared between the two.
//
// A mapping (made in the MIDI window, never by "learn") says which controller
// drives which target, and how: the part of the controller's travel used, the
// target range it covers, a curve, a smoothing time, and the takeover:
//   * Jump: the target follows the controller as soon as it moves;
//   * Pickup: after the target was moved elsewhere (with the mouse, by a
//     snapshot), the controller takes over only once it passes the target's
//     current value, so nothing jumps.
// One controller may feed several mappings: with a reversed range on one of
// them, a single gesture fades one deck in as another fades out.

#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <vector>

namespace goblet
{

class Mixer;

/** The latest value of every controller, written by the MIDI thread. */
class CcTable : public juce::MidiInputCallback
{
public:
    static constexpr int anyChannel = 16;     // row holding the latest value from any channel

    struct Cell
    {
        std::atomic<int> value { -1 };
        std::atomic<uint32_t> seq { 0 };      // goes up with every message
    };

    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m) override
    {
        messages.fetch_add (1, std::memory_order_relaxed);
        if (m.isController())
            set (m.getChannel(), m.getControllerNumber(), m.getControllerValue());
    }

    /** channel 1..16. Also used by the tests. */
    void set (int channel, int cc, int value) noexcept
    {
        if (! juce::isPositiveAndBelow (cc, 128) || channel < 1 || channel > 16) return;
        for (int row : { channel - 1, (int) anyChannel })
        {
            cells[row][cc].value.store (juce::jlimit (0, 127, value), std::memory_order_relaxed);
            cells[row][cc].seq.fetch_add (1, std::memory_order_release);
        }
    }

    const Cell& cell (int channel0to16, int cc) const noexcept   { return cells[juce::jlimit (0, 16, channel0to16)][juce::jlimit (0, 127, cc)]; }
    uint32_t messageCount() const noexcept                       { return messages.load (std::memory_order_relaxed); }

private:
    Cell cells[17][128];
    std::atomic<uint32_t> messages { 0 };
};

/** What a mapping drives. */
struct MidiTarget
{
    enum class Kind { none = 0, fader, trim, plugin };
    Kind kind = Kind::none;
    int deck = 0;                    // fader, trim
    int chain = 0, slot = 0, index = -1;   // plugin parameter

    bool operator== (const MidiTarget& o) const noexcept
    {
        return kind == o.kind && deck == o.deck && chain == o.chain && slot == o.slot && index == o.index;
    }
};

/** A mapping as the audio thread uses it. */
struct MidiMapping
{
    enum class Curve { linear = 0, exponential, logarithmic };

    int row = 0;                     // its place in the MIDI window, stable while it exists
    bool enabled = true;
    int channel = 0;                 // 0 = any, 1..16
    int cc = 1;
    bool fine = false;               // 14-bit: cc (0..31) is the coarse part, cc + 32 the fine part
    float inLow = 0.0f, inHigh = 1.0f;     // the part of the controller's travel used (0..1)
    float outLow = 0.0f, outHigh = 1.0f;   // the target range (normalised 0..1)
    Curve curve = Curve::linear;
    float smoothMs = 20.0f;
    bool pickup = false;
    MidiTarget target;

    /** Controller travel 0..1 -> target value 0..1. */
    float map (float x) const noexcept
    {
        const float span = inHigh - inLow;
        float u = std::abs (span) < 1.0e-6f ? (x >= inHigh ? 1.0f : 0.0f) : (x - inLow) / span;
        u = juce::jlimit (0.0f, 1.0f, u);
        switch (curve)
        {
            case Curve::exponential: u = (std::exp2 (4.0f * u) - 1.0f) / 15.0f; break;    // slow start
            case Curve::logarithmic: u = std::log2 (1.0f + 15.0f * u) / 4.0f; break;     // fast start
            case Curve::linear:
            default: break;
        }
        return juce::jlimit (0.0f, 1.0f, outLow + (outHigh - outLow) * u);
    }

    /** The controller's position 0..1 from the table, or -1 if nothing arrived yet. */
    float read (const CcTable& table) const noexcept
    {
        const int r = channel <= 0 ? CcTable::anyChannel : channel - 1;
        const int coarse = table.cell (r, cc).value.load (std::memory_order_relaxed);
        if (coarse < 0) return -1.0f;
        if (! fine) return (float) coarse / 127.0f;
        const int lsb = juce::jmax (0, table.cell (r, cc + 32).value.load (std::memory_order_relaxed));
        return (float) ((coarse << 7) | lsb) / 16383.0f;
    }

    /** A number that changes whenever the controller (or its fine part) sends. */
    uint64_t sequence (const CcTable& table) const noexcept
    {
        const int r = channel <= 0 ? CcTable::anyChannel : channel - 1;
        uint64_t s = table.cell (r, cc).seq.load (std::memory_order_acquire);
        if (fine) s += (uint64_t) table.cell (r, cc + 32).seq.load (std::memory_order_acquire) << 32;
        return s;
    }

    /** Identifies the controller source and target, to notice a mapping that changed. */
    bool sameAs (const MidiMapping& o) const noexcept
    {
        return channel == o.channel && cc == o.cc && fine == o.fine && target == o.target;
    }
};

/** Applies the mappings, on the audio thread, at the start of each block. */
class MidiEngine
{
public:
    static constexpr int maxMappings = 16;

    MidiEngine();
    ~MidiEngine();

    //== Message thread ======================================================
    void setMappings (const std::vector<MidiMapping>& mappings);

    // For the MIDI window: has this row's controller moved recently, and where to.
    bool isActive (int row) const noexcept            { return juce::isPositiveAndBelow (row, maxMappings) && active[(size_t) row].load(); }
    float getValue (int row) const noexcept           { return juce::isPositiveAndBelow (row, maxMappings) ? live[(size_t) row].load() : 0.0f; }
    // Waiting for the controller to pass the target's value (Pickup).
    bool isWaiting (int row) const noexcept           { return juce::isPositiveAndBelow (row, maxMappings) && waiting[(size_t) row].load(); }

    //== Audio thread ========================================================
    void process (Mixer& mixer, const CcTable& table, int numSamples, double sampleRate) noexcept;

private:
    struct Set { std::vector<MidiMapping> mappings; };

    struct State
    {
        uint64_t seq = 0;
        bool seen = false;
        float lastApplied = -1.0f;    // the value this mapping last gave its target
        float previous = -1.0f;       // the mapped value at the previous move (Pickup)
        bool owns = false;            // Pickup: has taken over its target
        float current = -1.0f;        // plugin parameter smoothing
        float goal = -1.0f;
        double lastMove = -1.0e9;
        MidiMapping last;
    };

    float currentValue (Mixer&, const MidiTarget&) const noexcept;
    void apply (Mixer&, const MidiMapping&, float value) noexcept;

    std::atomic<const Set*> current { nullptr };
    std::unique_ptr<Set> owned;
    struct Retired { std::unique_ptr<Set> set; double time; };
    std::vector<Retired> retired;

    std::array<State, maxMappings> states;
    const Set* lastSet = nullptr;
    double clock = 0.0;
    std::array<std::atomic<bool>, maxMappings> active {}, waiting {};
    std::array<std::atomic<float>, maxMappings> live {};
};

} // namespace goblet
