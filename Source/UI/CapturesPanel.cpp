// CapturesPanel.cpp — see CapturesPanel.h.

#include "CapturesPanel.h"
#include "TimeFormat.h"
#include "../Engine/CaptureRecorder.h"
#include "../Engine/Gesture.h"

namespace goblet
{

//==============================================================================
class CapturesPanel::Row : public juce::Component
{
public:
    explicit Row (CapturesPanel& o) : owner (o) {}

    void set (int index, bool isSelected)
    {
        row = index;
        selected = isSelected;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        if (! juce::isPositiveAndBelow (row, (int) owner.entries.size())) return;
        const auto& e = owner.entries[(size_t) row];
        const bool playing = owner.previewFile == e.file && owner.preview.isPlaying();

        g.fillAll (selected ? juce::Colour (0xff2f4f6f) : (row % 2 == 0 ? juce::Colour (0xff23272d) : juce::Colour (0xff262a31)));
        auto r = getLocalBounds().reduced (8, 3);

        // The fold triangle of a take with stems; stems are indented.
        auto fold = r.removeFromLeft (18);
        if (e.isStem)
            r.removeFromLeft (10);
        else if (! e.stems.isEmpty())
        {
            juce::Path p;
            const float cx = (float) fold.getCentreX(), cy = (float) fold.getCentreY();
            if (e.expanded) p.addTriangle (cx - 4.5f, cy - 2.5f, cx + 4.5f, cy - 2.5f, cx, cy + 3.5f);
            else            p.addTriangle (cx - 2.5f, cy - 4.5f, cx - 2.5f, cy + 4.5f, cx + 3.5f, cy);
            g.setColour (juce::Colours::white.withAlpha (0.6f));
            g.fillPath (p);
        }

        auto nameArea = r.removeFromLeft (juce::jmax (200, r.getWidth() / 2));
        g.setColour (playing ? juce::Colour (0xff6cc3ff) : juce::Colours::white.withAlpha (e.isStem ? 0.75f : 0.92f));
        g.setFont (juce::FontOptions (e.isStem ? 12.0f : 12.5f, e.isStem ? juce::Font::plain : juce::Font::bold));
        g.drawText ((playing ? juce::String::fromUTF8 ("\xe2\x96\xb6 ") : juce::String()) + e.file.getFileNameWithoutExtension(),
                    nameArea, juce::Justification::centredLeft, true);

        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.setFont (juce::FontOptions (11.5f));
        juce::String info = formatTime (e.seconds);
        if (e.rate > 0.0) info << "   " << juce::String (e.rate / 1000.0, 1) << " kHz";
        if (e.bits > 0) info << " " << e.bits << (e.bits == 32 ? "-bit float" : "-bit");
        if (! e.isStem)
        {
            info << "   " << e.file.getLastModificationTime().formatted ("%d/%m %H:%M");
            if (! e.stems.isEmpty()) info << juce::String::fromUTF8 ("   \xe2\x80\xa2 sum + ") << e.stems.size() << (e.stems.size() == 1 ? " stem" : " stems");
            if (e.hasSnapshot) info << juce::String::fromUTF8 ("   \xe2\x80\xa2 snapshot");
            if (e.hasGesture) info << juce::String::fromUTF8 ("   \xe2\x80\xa2 gesture");
        }
        g.drawText (info, r, juce::Justification::centredLeft, true);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragStarted = false;
        if (e.x < 26 && juce::isPositiveAndBelow (row, (int) owner.entries.size()) && ! owner.entries[(size_t) row].stems.isEmpty())
        {
            owner.toggleExpanded (row);
            return;
        }
        owner.list.selectRowsBasedOnModifierKeys (row, e.mods, false);
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override   { if (e.x >= 26) owner.togglePreview (row); }

    // Drag out of the app: into Reaper, the Finder, another program.
    // Shift-drag on a take: the sum and all its stems.
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragStarted || e.getDistanceFromDragStart() < 6) return;
        if (! juce::isPositiveAndBelow (row, (int) owner.entries.size())) return;
        dragStarted = true;
        const auto& entry = owner.entries[(size_t) row];
        juce::StringArray files { entry.file.getFullPathName() };
        if (e.mods.isShiftDown())
            for (auto& s : entry.stems) files.add (s.getFullPathName());
        juce::DragAndDropContainer::performExternalDragDropOfFiles (files, false, this);
    }

private:
    CapturesPanel& owner;
    int row = -1;
    bool selected = false, dragStarted = false;
};

//==============================================================================
CapturesPanel::CapturesPanel (juce::AudioTransportSource& previewTransport) : preview (previewTransport)
{
    formats.registerBasicFormats();

    titleLabel.setText ("Captures", juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    addAndMakeVisible (titleLabel);

    recordingLabel.setFont (juce::FontOptions (12.5f, juce::Font::bold));
    recordingLabel.setColour (juce::Label::textColourId, juce::Colour (0xffe0483c));
    addAndMakeVisible (recordingLabel);

    folderLabel.setFont (juce::FontOptions (11.5f));
    folderLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.45f));
    folderLabel.setMinimumHorizontalScale (0.6f);
    folderLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (folderLabel);

    hintLabel.setText ("Rec (R) captures the sum and a stem per deck. Double-click to listen; drag into Reaper "
                       "(Shift-drag a take: sum and stems). The triangle shows a take's stems.",
                       juce::dontSendNotification);
    hintLabel.setFont (juce::FontOptions (11.5f));
    hintLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.45f));
    hintLabel.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (hintLabel);

    list.setRowHeight (28);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff1e2126));
    list.setMultipleSelectionEnabled (false);
    addAndMakeVisible (list);

    playButton.onClick = [this] { togglePreview (list.getSelectedRow()); };
    renameButton.onClick = [this] { renameSelected(); };
    revealButton.onClick = [this] { if (auto* e = selectedEntry()) e->file.revealToUser(); };
    revealButton.setTooltip ("Show in Finder");
    deleteButton.onClick = [this] { deleteSelected(); };
    deleteButton.setTooltip ("Move the take (sum, stems, snapshot and gesture) to the bin");
    restoreButton.onClick = [this]
    {
        if (auto* e = selectedEntry())
            if (e->hasSnapshot && onRestoreSnapshot)
                onRestoreSnapshot (snapshotFor (e->file));
    };
    restoreButton.setTooltip ("Go back to the decks, plugins, trims, faders and mappings that produced this take");
    folderButton.onClick = [this] { folder.createDirectory(); folder.startAsProcess(); };
    rerenderButton.onClick = [this]
    {
        if (auto* e = selectedEntry())
            if (e->hasGesture && e->hasSnapshot && onRerender)
                onRerender (e->file);
    };
    rerenderButton.setTooltip ("Play the take again offline, at Render quality, from its snapshot and its gesture (every fader, trim, "
                               "mute, start and stop, and the plugin parameters your MIDI mappings moved). The result is a new take "
                               "with its own stems. Click again while it runs to cancel.");

    for (auto* b : { &playButton, &renameButton, &revealButton, &deleteButton, &restoreButton, &folderButton, &rerenderButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }

    updateButtons();
    startTimer (500);
}

CapturesPanel::~CapturesPanel()
{
    stopPreview();
}

void CapturesPanel::setFolder (const juce::File& f)
{
    if (f == folder) return;
    folder = f;
    folderLabel.setText (folder.getFullPathName(), juce::dontSendNotification);
    folderLabel.setTooltip (folder.getFullPathName());
    lastListing = "-";            // differs from any listing, also an empty one
    refresh();
}

void CapturesPanel::setRecording (const juce::File& file, double seconds, bool inTail)
{
    juce::String t;
    if (file == juce::File() && rendering) return;     // the render's progress is shown
    if (file != juce::File())
        t << juce::String::fromUTF8 ("\xe2\x97\x8f ") << (inTail ? "Tail of " : "Recording ") << file.getFileNameWithoutExtension()
          << "  " << formatTime (seconds);
    if (recordingLabel.getText() != t) recordingLabel.setText (t, juce::dontSendNotification);
}

void CapturesPanel::setRendering (const juce::String& name, double progress)
{
    rendering = name.isNotEmpty();
    if (rendering)
        recordingLabel.setText ("Re-rendering " + name + ": " + juce::String (juce::roundToInt (progress * 100.0)) + "%", juce::dontSendNotification);
    else if (recordingLabel.getText().startsWith ("Re-rendering"))
        recordingLabel.setText ({}, juce::dontSendNotification);
    updateButtons();
}

CapturesPanel::Entry CapturesPanel::readEntry (const juce::File& f, bool isStem)
{
    Entry e;
    e.file = f;
    e.isStem = isStem;
    if (std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (f)); r != nullptr)
    {
        e.rate = r->sampleRate;
        e.bits = (int) r->bitsPerSample;
        e.seconds = r->sampleRate > 0.0 ? (double) r->lengthInSamples / r->sampleRate : 0.0;
    }
    return e;
}

void CapturesPanel::refresh()
{
    auto files = folder.findChildFiles (juce::File::findFiles, false, "*.wav");
    juce::String listing;
    for (auto& f : files)
    {
        listing << f.getFileName() << f.getLastModificationTime().toMilliseconds() << f.getSize() << ";";
        for (auto& s : CaptureRecorder::stemsFolder (f).findChildFiles (juce::File::findFiles, false, "*.wav"))
            listing << s.getFileName() << s.getSize() << ",";
    }
    if (listing == lastListing) return;
    lastListing = listing;

    takes.clear();
    for (auto& f : files)
    {
        auto e = readEntry (f, false);
        e.hasSnapshot = snapshotFor (f).existsAsFile();
        e.hasGesture = Gesture::fileFor (f).existsAsFile();
        auto stems = CaptureRecorder::stemsFolder (f).findChildFiles (juce::File::findFiles, false, "*.wav");
        stems.sort();
        e.stems = stems;
        takes.push_back (e);
    }
    std::sort (takes.begin(), takes.end(), [] (const Entry& a, const Entry& b)
               { return a.file.getLastModificationTime() > b.file.getLastModificationTime(); });
    rebuild();
}

void CapturesPanel::rebuild()
{
    const auto keep = selectedEntry() != nullptr ? selectedEntry()->file : juce::File();
    entries.clear();
    for (auto& t : takes)
    {
        auto e = t;
        e.expanded = expandedTakes.contains (t.file.getFullPathName());
        entries.push_back (e);
        if (e.expanded)
            for (auto& s : t.stems)
                entries.push_back (readEntry (s, true));
    }

    list.updateContent();
    list.deselectAllRows();
    for (int i = 0; i < (int) entries.size(); ++i)
        if (entries[(size_t) i].file == keep) list.selectRow (i);
    list.repaint();
    updateButtons();
}

void CapturesPanel::toggleExpanded (int row)
{
    const auto path = entries[(size_t) row].file.getFullPathName();
    if (expandedTakes.contains (path)) expandedTakes.removeString (path);
    else                               expandedTakes.add (path);
    rebuild();
}

juce::Component* CapturesPanel::refreshComponentForRow (int row, bool selected, juce::Component* existing)
{
    auto* r = dynamic_cast<Row*> (existing);
    if (r == nullptr)
    {
        delete existing;
        r = new Row (*this);
    }
    r->set (row, selected);
    return r;
}

const CapturesPanel::Entry* CapturesPanel::selectedEntry() const
{
    const int row = list.getSelectedRow();
    return juce::isPositiveAndBelow (row, (int) entries.size()) ? &entries[(size_t) row] : nullptr;
}

void CapturesPanel::updateButtons()
{
    const auto* e = selectedEntry();
    for (auto* b : { &playButton, &revealButton })
        b->setEnabled (e != nullptr);
    renameButton.setEnabled (e != nullptr && ! e->isStem);
    deleteButton.setEnabled (e != nullptr && ! e->isStem);
    restoreButton.setEnabled (e != nullptr && e->hasSnapshot);
    rerenderButton.setEnabled (rendering || (e != nullptr && e->hasSnapshot && e->hasGesture));
    rerenderButton.setButtonText (rendering ? "Cancel render" : "Re-render");
    playButton.setButtonText (e != nullptr && previewFile == e->file && preview.isPlaying() ? "Stop" : "Play");
}

void CapturesPanel::timerCallback()
{
    refresh();
    // The preview reached its end.
    if (previewSource != nullptr && ! preview.isPlaying() && preview.hasStreamFinished())
        stopPreview();
    updateButtons();
    list.repaint();
}

void CapturesPanel::stopPreview()
{
    preview.stop();
    preview.setSource (nullptr);
    previewSource = nullptr;
    previewFile = juce::File();
}

void CapturesPanel::togglePreview (int row)
{
    if (! juce::isPositiveAndBelow (row, (int) entries.size())) return;
    const auto file = entries[(size_t) row].file;
    const bool wasThis = previewFile == file && preview.isPlaying();
    stopPreview();
    if (wasThis) { updateButtons(); return; }

    auto* reader = formats.createReaderFor (file);
    if (reader == nullptr)
    {
        if (showStatus) showStatus ("Cannot read " + file.getFileName());
        return;
    }
    const double rate = reader->sampleRate;
    previewSource = std::make_unique<juce::AudioFormatReaderSource> (reader, true);
    preview.setSource (previewSource.get(), 0, nullptr, rate);
    preview.setPosition (0.0);
    preview.start();
    previewFile = file;
    updateButtons();
}

void CapturesPanel::renameSelected()
{
    const auto* e = selectedEntry();
    if (e == nullptr || e->isStem) return;
    const auto file = e->file;

    auto* w = new juce::AlertWindow ("Rename take", "New name for " + file.getFileName() + ":", juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", file.getFileNameWithoutExtension());
    w->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<CapturesPanel> safeThis (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safeThis, w, file] (int result)
    {
        if (safeThis == nullptr || result != 1) return;
        const auto name = juce::File::createLegalFileName (w->getTextEditorContents ("name").trim());
        if (name.isEmpty()) return;
        const auto target = file.getSiblingFile (name + ".wav");
        if (target.exists()) { if (safeThis->showStatus) safeThis->showStatus (target.getFileName() + " already exists."); return; }
        safeThis->stopPreview();

        const auto oldName = file.getFileNameWithoutExtension();
        const auto snapshot = snapshotFor (file);
        const auto stems = CaptureRecorder::stemsFolder (file);
        if (file.moveFileTo (target))
        {
            if (snapshot.existsAsFile()) snapshot.moveFileTo (snapshotFor (target));
            if (Gesture::fileFor (file).existsAsFile()) Gesture::fileFor (file).moveFileTo (Gesture::fileFor (target));
            if (stems.isDirectory())
            {
                // The stems follow: "<old> A.wav" becomes "<new> A.wav", in "<new> stems".
                for (auto& s : stems.findChildFiles (juce::File::findFiles, false, "*.wav"))
                    if (s.getFileName().startsWith (oldName))
                        s.moveFileTo (s.getSiblingFile (name + s.getFileName().substring (oldName.length())));
                stems.moveFileTo (CaptureRecorder::stemsFolder (target));
            }
            safeThis->refresh();
        }
    }), true);
}

void CapturesPanel::deleteSelected()
{
    const auto* e = selectedEntry();
    if (e == nullptr || e->isStem) return;
    const auto file = e->file;
    stopPreview();
    const auto snapshot = snapshotFor (file);
    const auto stems = CaptureRecorder::stemsFolder (file);
    if (file.moveToTrash())
    {
        if (snapshot.existsAsFile()) snapshot.moveToTrash();
        if (Gesture::fileFor (file).existsAsFile()) Gesture::fileFor (file).moveToTrash();
        if (stems.isDirectory()) stems.moveToTrash();
        if (showStatus) showStatus ("Moved " + file.getFileName() + " (with its stems and snapshot) to the bin.");
    }
    refresh();
}

void CapturesPanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff23272b));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);

    if (entries.empty())
    {
        g.setColour (juce::Colours::white.withAlpha (0.4f));
        g.setFont (juce::FontOptions (12.5f));
        g.drawFittedText ("No captures yet.", list.getBounds().reduced (10), juce::Justification::centred, 2);
    }
}

void CapturesPanel::resized()
{
    auto r = getLocalBounds().reduced (14, 8);
    auto top = r.removeFromTop (22);
    titleLabel.setBounds (top.removeFromLeft (90));
    recordingLabel.setBounds (top.removeFromLeft (360));
    folderLabel.setBounds (top);
    r.removeFromTop (4);

    auto side = r.removeFromRight (330);
    r.removeFromRight (10);
    list.setBounds (r);

    auto row1 = side.removeFromTop (26);
    side.removeFromTop (4);
    auto row2 = side.removeFromTop (26);
    side.removeFromTop (4);
    hintLabel.setBounds (side);

    const int w1 = row1.getWidth() / 4;
    for (auto* b : { &playButton, &renameButton, &revealButton, &deleteButton })
        b->setBounds (row1.removeFromLeft (w1).reduced (2, 0));
    const int w2 = row2.getWidth() / 3;
    restoreButton.setBounds (row2.removeFromLeft (w2 + 20).reduced (2, 0));
    rerenderButton.setBounds (row2.removeFromLeft (w2).reduced (2, 0));
    folderButton.setBounds (row2.reduced (2, 0));
}

} // namespace goblet
