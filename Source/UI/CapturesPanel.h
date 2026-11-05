// CapturesPanel.h — the captures list along the bottom of the window
// (adapted from Effet).
//
// Lists the takes in the captures folder, newest first. A take is its sum
// file; the triangle unfolds it to show its stems. Each file can be listened
// to, dragged straight into Reaper or the Finder (Shift-drag a take: the sum
// and all its stems), renamed, shown in Finder or moved to the bin; a take's
// snapshot (the session that produced it) can be restored.

#pragma once

#include <JuceHeader.h>

namespace goblet
{

class CapturesPanel : public juce::Component,
                      private juce::ListBoxModel,
                      private juce::Timer
{
public:
    explicit CapturesPanel (juce::AudioTransportSource& previewTransport);
    ~CapturesPanel() override;

    void setFolder (const juce::File& folder);
    juce::File getFolder() const                { return folder; }
    void refresh();
    void setRecording (const juce::File& file, double seconds, bool inTail);   // shown as the first row while capturing

    std::function<void (const juce::File& snapshot)> onRestoreSnapshot;
    std::function<void (const juce::String&)> showStatus;

    /** The snapshot saved beside a take. */
    static juce::File snapshotFor (const juce::File& wav)   { return wav.withFileExtension (".goblet"); }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Entry
    {
        juce::File file;
        double seconds = 0.0, rate = 0.0;
        int bits = 0;
        bool hasSnapshot = false, isStem = false, expanded = false;
        juce::Array<juce::File> stems;
    };
    class Row;

    int getNumRows() override                   { return (int) entries.size(); }
    void paintListBoxItem (int, juce::Graphics&, int, int, bool) override {}
    juce::Component* refreshComponentForRow (int row, bool selected, juce::Component* existing) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override  { togglePreview (row); }
    void selectedRowsChanged (int) override     { updateButtons(); }
    void deleteKeyPressed (int) override        { deleteSelected(); }
    void returnKeyPressed (int row) override    { togglePreview (row); }

    void timerCallback() override;
    void rebuild();
    void toggleExpanded (int row);
    void togglePreview (int row);
    void stopPreview();
    void renameSelected();
    void deleteSelected();
    void updateButtons();
    const Entry* selectedEntry() const;
    Entry readEntry (const juce::File&, bool isStem);

    juce::AudioTransportSource& preview;
    std::unique_ptr<juce::AudioFormatReaderSource> previewSource;
    juce::File previewFile;
    juce::AudioFormatManager formats;

    juce::File folder;
    std::vector<Entry> takes, entries;      // takes; the rows shown (takes, unfolded stems)
    juce::StringArray expandedTakes;
    juce::Label titleLabel, folderLabel, hintLabel, recordingLabel;
    juce::ListBox list { "captures", this };
    juce::TextButton playButton { "Play" }, renameButton { "Rename..." }, revealButton { "Show" },
                     deleteButton { "Delete" }, restoreButton { "Restore snapshot..." }, folderButton { "Open folder" };
    juce::String lastListing;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CapturesPanel)
};

} // namespace goblet
