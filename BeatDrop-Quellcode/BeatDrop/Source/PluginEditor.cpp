#include "PluginEditor.h"

namespace
{
    constexpr int kWidth = 560;
    constexpr int kHeight = 392;
    constexpr int kPad = 24;
}

BeatDropEditor::BeatDropEditor (BeatDropProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lnf);
    setSize (kWidth, kHeight);
    setResizable (false, false);
    setWantsKeyboardFocus (true);

    input.setMultiLine (false);
    input.setReturnKeyStartsNewLine (false);
    input.setFont (uiFont (15.0f));
    input.setJustification (juce::Justification::centredLeft);
    input.setIndents (14, 0);
    input.setSelectAllWhenFocused (true);
    input.setTextToShowWhenEmpty (T ("YouTube-Link einf\xc3\xbcgen oder Beat suchen"), Palette::dim);
    input.onReturnKey = [this] { submit(); };
    input.onEscapeKey = [this] { processor.clearResults(); };
    addAndMakeVisible (input);

    goButton.setIcon (IconButton::Icon::go);
    goButton.onClick = [this] { submit(); };
    addAndMakeVisible (goButton);

    waveform.onSeek = [this] (double f) { processor.seekToFraction (f); };
    waveform.onDragOut = [this] { startFileDrag(); };
    addAndMakeVisible (waveform);

    playButton.onClick = [this]
    {
        if (processor.isHostPlaying())
            return;   // Host steuert gerade

        if (processor.isUserPlaying())
            processor.stopAndRewind();
        else
            processor.togglePlay();
    };
    addAndMakeVisible (playButton);

    chip.onDragOut = [this] { startFileDrag(); };
    addAndMakeVisible (chip);

    volume.setDoubleClickReturnValue (true, 0.0);
    volumeAttachment = std::make_unique<juce::SliderParameterAttachment> (*processor.level, volume);
    addAndMakeVisible (volume);

    results.onPick = [this] (const SearchHit& hit)
    {
        input.setText (hit.title, false);
        processor.startDownload (hit.url());
    };
    addChildComponent (results);   // zuletzt hinzugefuegt = liegt oben

    startTimerHz (30);
}

BeatDropEditor::~BeatDropEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void BeatDropEditor::resized()
{
    const int w = getWidth();

    volume.setBounds (w - kPad - 96, 20, 96, 18);
    input.setBounds (kPad, 56, w - kPad * 2 - 48, 40);
    goButton.setBounds (w - kPad - 40, 56, 40, 40);
    waveform.setBounds (kPad, 126, w - kPad * 2, 150);
    playButton.setBounds (kPad, 322, 44, 44);
    chip.setBounds (w - kPad - 236, 322, 236, 44);

    layoutResults();
}

void BeatDropEditor::layoutResults()
{
    results.setBounds (kPad, 100, getWidth() - kPad * 2, results.getPreferredHeight());
}

void BeatDropEditor::paint (juce::Graphics& g)
{
    const int w = getWidth();

    g.fillAll (Palette::bg);

    // Wortmarke
    g.setColour (Palette::accent);
    g.fillEllipse ((float) kPad, 25.0f, 7.0f, 7.0f);

    g.setColour (Palette::text);
    g.setFont (uiFont (13.0f, true, 0.3f));
    g.drawText ("BEATDROP", kPad + 16, 20, 200, 18, juce::Justification::centredLeft);

    g.setColour (Palette::dim);
    g.setFont (uiFont (10.0f, true, 0.2f));
    g.drawText ("VOL", w - kPad - 96 - 36, 20, 30, 18, juce::Justification::centredRight);

    // Fortschrittslinie unter dem Eingabefeld
    if (progress > 0.0f)
    {
        g.setColour (Palette::accent);
        g.fillRoundedRectangle ((float) kPad, 98.0f, (float) (w - kPad * 2) * progress, 2.0f, 1.0f);
    }

    // Statuszeile
    g.setColour (statusColour);
    g.setFont (uiFont (12.0f));
    g.drawText (statusText, kPad, 104, w - kPad * 2, 16, juce::Justification::centredLeft);

    // Titel + Laenge
    g.setColour (Palette::text);
    g.setFont (uiFont (14.0f));
    g.drawText (titleText, kPad, 288, w - kPad * 2 - 60, 20, juce::Justification::centredLeft);

    g.setColour (Palette::dim);
    g.setFont (uiFont (12.0f));
    g.drawText (lengthText, w - kPad - 60, 288, 60, 20, juce::Justification::centredRight);

    // Zeit neben dem Play-Button
    g.setColour (Palette::dim);
    g.setFont (uiFont (13.0f));
    g.drawText (timeText, kPad + 56, 322, 120, 44, juce::Justification::centredLeft);
}

bool BeatDropEditor::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey && ! input.hasKeyboardFocus (true))
    {
        if (! processor.isHostPlaying())
            processor.togglePlay();

        return true;
    }

    return false;
}

void BeatDropEditor::submit()
{
    auto text = input.getText().trim();

    if (text.isEmpty())
    {
        // leeres Feld + Klick: Link aus der Zwischenablage nehmen
        text = juce::SystemClipboard::getTextFromClipboard().trim();
        input.setText (text, false);
    }

    if (text.isEmpty())
        return;

    if (YtDlpClient::looksLikeUrl (text))
        processor.startDownload (text);
    else
        processor.startSearch (text);
}

void BeatDropEditor::startFileDrag()
{
    if (auto b = processor.getBeat())
        if (b->file.existsAsFile())
            juce::DragAndDropContainer::performExternalDragDropOfFiles ({ b->file.getFullPathName() }, false, this);
}

void BeatDropEditor::timerCallback()
{
    bool needsRepaint = false;

    // Beat gewechselt?
    auto beat = processor.getBeat();

    if (beat != shownBeat)
    {
        shownBeat = beat;
        waveform.setBeat (beat);
        chip.setActive (beat != nullptr);
        titleText = beat != nullptr ? beat->title : juce::String();
        lengthText = beat != nullptr ? formatTime (beat->getSeconds()) : juce::String();
        needsRepaint = true;
    }

    // Suchergebnisse
    const int rv = processor.getResultsVersion();

    if (rv != shownResultsVersion)
    {
        shownResultsVersion = rv;

        auto hits = processor.getResults();
        results.setHits (hits);
        results.setVisible (! hits.isEmpty());
        layoutResults();
    }

    // Status
    const auto status = processor.getStatus();
    juce::String newText = status.message;
    auto newColour = Palette::dim;
    float newProgress = 0.0f;

    switch (status.kind)
    {
        case JobStatus::Kind::downloading:
            newProgress = status.progress;
            if (status.progress > 0.0f)
                newText << "  " << juce::String (juce::roundToInt (status.progress * 100.0f)) << " %";
            break;

        case JobStatus::Kind::failed:
            newColour = Palette::accent;
            break;

        case JobStatus::Kind::ready:
            newText = {};
            break;

        case JobStatus::Kind::idle:
        case JobStatus::Kind::searching:
        case JobStatus::Kind::loading:
            break;
    }

    if (newText != statusText || newColour != statusColour || std::abs (newProgress - progress) > 0.002f)
    {
        statusText = newText;
        statusColour = newColour;
        progress = newProgress;
        needsRepaint = true;
    }

    // Position + Play-Status
    const double duration = beat != nullptr ? beat->getSeconds() : 0.0;
    const double pos = juce::jlimit (0.0, juce::jmax (0.0, duration), processor.getPositionSeconds());

    waveform.setPosition (duration > 0.0 ? pos / duration : 0.0);

    const auto newTime = beat != nullptr ? formatTime (pos) + "  /  " + formatTime (duration) : juce::String();

    if (newTime != timeText)
    {
        timeText = newTime;
        needsRepaint = true;
    }

    playButton.setIcon (processor.isUserPlaying() || processor.isHostPlaying() ? IconButton::Icon::stop
                                                                               : IconButton::Icon::play);

    if (needsRepaint)
        repaint();
}
