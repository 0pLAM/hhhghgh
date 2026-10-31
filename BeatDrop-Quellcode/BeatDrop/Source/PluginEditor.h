#pragma once

#include "PluginProcessor.h"
#include "Util.h"

//==============================================================================
// Eine Akzentfarbe, sonst nur Schwarz/Grau/Weiss.
namespace Palette
{
    inline const juce::Colour bg     { 0xff0c0c0e };
    inline const juce::Colour panel  { 0xff151518 };
    inline const juce::Colour line   { 0xff26262c };
    inline const juce::Colour text   { 0xffeeeeee };
    inline const juce::Colour dim    { 0xff85858f };
    inline const juce::Colour accent { 0xffff4d3d };
}

inline juce::Font uiFont (float size, bool bold = false, float kerning = 0.0f)
{
    auto opts = juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain);

    if (kerning != 0.0f)
        opts = opts.withKerningFactor (kerning);

    return juce::Font (opts);
}

//==============================================================================
class MinimalLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    MinimalLookAndFeel()
    {
        setColour (juce::TextEditor::backgroundColourId, Palette::panel);
        setColour (juce::TextEditor::textColourId, Palette::text);
        setColour (juce::TextEditor::highlightColourId, Palette::accent.withAlpha (0.35f));
        setColour (juce::TextEditor::highlightedTextColourId, Palette::text);
        setColour (juce::CaretComponent::caretColourId, Palette::accent);

        setColour (juce::PopupMenu::backgroundColourId, Palette::panel);
        setColour (juce::PopupMenu::textColourId, Palette::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Palette::line);
        setColour (juce::PopupMenu::highlightedTextColourId, Palette::text);
    }

    void fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor& t) override
    {
        g.setColour (t.findColour (juce::TextEditor::backgroundColourId));
        g.fillRoundedRectangle (0.0f, 0.0f, (float) w, (float) h, 10.0f);
    }

    void drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& t) override
    {
        g.setColour (t.hasKeyboardFocus (true) ? Palette::accent.withAlpha (0.8f) : Palette::line);
        g.drawRoundedRectangle (0.5f, 0.5f, (float) w - 1.0f, (float) h - 1.0f, 10.0f, 1.0f);
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                           float, float, juce::Slider::SliderStyle, juce::Slider&) override
    {
        const float cy = (float) y + (float) height * 0.5f;

        g.setColour (Palette::line);
        g.fillRoundedRectangle ((float) x, cy - 1.5f, (float) width, 3.0f, 1.5f);

        g.setColour (Palette::text);
        g.fillRoundedRectangle ((float) x, cy - 1.5f, sliderPos - (float) x, 3.0f, 1.5f);
        g.fillEllipse (sliderPos - 5.0f, cy - 5.0f, 10.0f, 10.0f);
    }
};

//==============================================================================
class IconButton final : public juce::Component
{
public:
    enum class Icon { play, stop, go };

    std::function<void()> onClick;

    IconButton() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

    void setIcon (Icon i)
    {
        if (icon != i)
        {
            icon = i;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (1.0f);
        const bool over = isMouseOver();
        const auto c = r.getCentre();

        if (icon == Icon::go)
        {
            g.setColour (over ? Palette::accent : Palette::panel);
            g.fillRoundedRectangle (r, 10.0f);
            g.setColour (over ? Palette::accent : Palette::line);
            g.drawRoundedRectangle (r, 10.0f, 1.0f);

            g.setColour (Palette::text);
            juce::Path arrow;
            arrow.startNewSubPath (c.x - 7.0f, c.y);
            arrow.lineTo (c.x + 7.0f, c.y);
            arrow.startNewSubPath (c.x + 1.0f, c.y - 6.0f);
            arrow.lineTo (c.x + 7.0f, c.y);
            arrow.lineTo (c.x + 1.0f, c.y + 6.0f);
            g.strokePath (arrow, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            return;
        }

        g.setColour (over ? Palette::accent.brighter (0.15f) : Palette::accent);
        g.fillEllipse (r);

        g.setColour (Palette::bg);

        if (icon == Icon::play)
        {
            juce::Path p;
            p.addTriangle (c.x - 5.0f, c.y - 8.0f, c.x - 5.0f, c.y + 8.0f, c.x + 8.0f, c.y);
            g.fillPath (p);
        }
        else
        {
            g.fillRoundedRectangle (c.x - 6.0f, c.y - 6.0f, 12.0f, 12.0f, 2.0f);
        }
    }

    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (onClick != nullptr && contains (e.getPosition()))
            onClick();
    }

private:
    Icon icon = Icon::play;
};

//==============================================================================
class WaveformView final : public juce::Component
{
public:
    std::function<void (double)> onSeek;
    std::function<void()> onDragOut;

    WaveformView() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

    void setBeat (std::shared_ptr<const Beat> b)
    {
        beat = std::move (b);
        repaint();
    }

    void setPosition (double fraction)
    {
        if (std::abs (fraction - position) > 0.0004)
        {
            position = fraction;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();

        g.setColour (Palette::panel);
        g.fillRoundedRectangle (r, 12.0f);

        if (beat == nullptr || beat->peaks.empty())
        {
            g.setColour (Palette::line);
            g.fillRect (r.getX() + 20.0f, r.getCentreY() - 0.5f, r.getWidth() - 40.0f, 1.0f);

            g.setColour (Palette::dim);
            g.setFont (uiFont (13.0f));
            g.drawText (T ("Beat laden, dann hier anh\xc3\xb6ren und in die Playlist ziehen"),
                        getLocalBounds(), juce::Justification::centred);
            return;
        }

        const auto area = getWaveArea();
        const float pitch = 3.0f;
        const int cols = juce::jmax (1, (int) (area.getWidth() / pitch));
        const auto& peaks = beat->peaks;
        const int numPeaks = (int) peaks.size();
        const float playX = area.getX() + area.getWidth() * (float) position;

        for (int c = 0; c < cols; ++c)
        {
            const int i0 = (int) ((juce::int64) c * numPeaks / cols);
            const int i1 = juce::jmin (numPeaks, juce::jmax (i0 + 1, (int) ((juce::int64) (c + 1) * numPeaks / cols)));

            float p = 0.0f;

            for (int i = i0; i < i1; ++i)
                p = juce::jmax (p, peaks[(size_t) i]);

            const float h = juce::jmax (2.0f, p * area.getHeight());
            const float x = area.getX() + (float) c * pitch;

            g.setColour (x < playX ? Palette::accent : Palette::dim.withAlpha (0.55f));
            g.fillRoundedRectangle (x, area.getCentreY() - h * 0.5f, 2.0f, h, 1.0f);
        }

        g.setColour (Palette::text);
        g.fillRect (playX - 0.5f, area.getY() - 4.0f, 1.0f, area.getHeight() + 8.0f);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragStarted = false;

        if (beat != nullptr && onSeek != nullptr)
        {
            const auto area = getWaveArea();
            onSeek (juce::jlimit (0.0, 1.0, (double) ((e.position.x - area.getX()) / area.getWidth())));
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        // Ziehen = Datei in die DAW-Playlist ziehen
        if (beat != nullptr && ! dragStarted && e.getDistanceFromDragStart() > 10)
        {
            dragStarted = true;

            if (onDragOut != nullptr)
                onDragOut();
        }
    }

    void mouseUp (const juce::MouseEvent&) override { dragStarted = false; }

private:
    juce::Rectangle<float> getWaveArea() const { return getLocalBounds().toFloat().reduced (16.0f, 14.0f); }

    std::shared_ptr<const Beat> beat;
    double position = 0.0;
    bool dragStarted = false;
};

//==============================================================================
class DragChip final : public juce::Component
{
public:
    std::function<void()> onDragOut;

    DragChip() { setMouseCursor (juce::MouseCursor::DraggingHandCursor); }

    void setActive (bool a)
    {
        if (active != a)
        {
            active = a;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (1.0f);

        g.setColour (Palette::panel);
        g.fillRoundedRectangle (r, 12.0f);
        g.setColour (active && isMouseOver() ? Palette::accent : Palette::line);
        g.drawRoundedRectangle (r, 12.0f, 1.0f);

        g.setColour (active ? Palette::dim : Palette::line);

        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 2; ++col)
                g.fillEllipse (r.getX() + 16.0f + (float) col * 6.0f, r.getCentreY() - 8.0f + (float) row * 6.0f, 2.5f, 2.5f);

        g.setColour (active ? Palette::text : Palette::dim);
        g.setFont (uiFont (13.0f));
        g.drawText (T ("Beat in die Playlist ziehen"), r.withTrimmedLeft (38.0f).toNearestInt(),
                    juce::Justification::centredLeft);
    }

    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }
    void mouseDown (const juce::MouseEvent&) override  { dragStarted = false; }
    void mouseUp (const juce::MouseEvent&) override    { dragStarted = false; }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (active && ! dragStarted && e.getDistanceFromDragStart() > 6)
        {
            dragStarted = true;

            if (onDragOut != nullptr)
                onDragOut();
        }
    }

private:
    bool active = false;
    bool dragStarted = false;
};

//==============================================================================
class ResultsPanel final : public juce::Component
{
public:
    static constexpr int rowHeight = 46;

    std::function<void (const SearchHit&)> onPick;

    void setHits (juce::Array<SearchHit> h)
    {
        hits = std::move (h);
        hover = -1;
        repaint();
    }

    int getPreferredHeight() const { return hits.size() * rowHeight + 2; }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();

        g.setColour (Palette::panel);
        g.fillRoundedRectangle (r, 12.0f);

        for (int i = 0; i < hits.size(); ++i)
        {
            const auto row = juce::Rectangle<int> (1, 1 + i * rowHeight, getWidth() - 2, rowHeight);

            if (i == hover)
            {
                g.setColour (Palette::line);
                g.fillRect (row.reduced (4, 2));
            }

            const auto& h = hits.getReference (i);
            const auto inner = row.reduced (16, 0);

            g.setColour (Palette::text);
            g.setFont (uiFont (14.0f));
            g.drawText (h.title, inner.withTrimmedBottom (rowHeight / 2 - 2), juce::Justification::bottomLeft);

            g.setColour (Palette::dim);
            g.setFont (uiFont (12.0f));

            auto sub = h.channel;

            if (h.duration.isNotEmpty())
                sub << (sub.isEmpty() ? "" : "  -  ") << h.duration;

            g.drawText (sub, inner.withTrimmedTop (rowHeight / 2 + 1), juce::Justification::topLeft);
        }

        g.setColour (Palette::line);
        g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);
    }

    void mouseMove (const juce::MouseEvent& e) override { setHover (rowAt (e.getPosition())); }
    void mouseExit (const juce::MouseEvent&) override   { setHover (-1); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = rowAt (e.getPosition());

        if (i >= 0 && onPick != nullptr)
            onPick (hits.getReference (i));
    }

private:
    int rowAt (juce::Point<int> p) const
    {
        const int i = (p.y - 1) / rowHeight;
        return (p.y >= 1 && i >= 0 && i < hits.size()) ? i : -1;
    }

    void setHover (int i)
    {
        if (i != hover)
        {
            hover = i;
            repaint();
        }
    }

    juce::Array<SearchHit> hits;
    int hover = -1;
};

//==============================================================================
class BeatDropEditor final : public juce::AudioProcessorEditor,
                             private juce::Timer
{
public:
    explicit BeatDropEditor (BeatDropProcessor&);
    ~BeatDropEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void timerCallback() override;
    void submit();
    void startFileDrag();
    void layoutResults();

    BeatDropProcessor& processor;
    MinimalLookAndFeel lnf;

    juce::TextEditor input;
    IconButton goButton, playButton;
    WaveformView waveform;
    DragChip chip;
    ResultsPanel results;
    juce::Slider volume { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    std::unique_ptr<juce::SliderParameterAttachment> volumeAttachment;

    std::shared_ptr<const Beat> shownBeat;
    int shownResultsVersion = -1;

    juce::String statusText, titleText, lengthText, timeText;
    juce::Colour statusColour = Palette::dim;
    float progress = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BeatDropEditor)
};
