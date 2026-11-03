#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParamIDs.h"
#include "Presets.h"

#include <array>
#include <cmath>

namespace
{
    constexpr double kPi = 3.14159265358979323846;
    constexpr float  kFMin = 20.0f, kFMax = 20000.0f;
    constexpr float  kDbMin = -18.0f, kDbMax = 18.0f;
    constexpr int    kNumBands = 4;
    constexpr float  kDefaultFreq[kNumBands] = { 100.0f, 300.0f, 1000.0f, 4000.0f };

    const juce::Colour kBandColours[kNumBands] = {
        juce::Colour (0xff3fa9f5),
        juce::Colour (0xff7ed957),
        juce::Colour (0xfff7b731),
        juce::Colour (0xfff0557d)
    };
    const juce::Colour kBg     (0xff14171a);
    const juce::Colour kPanel  (0xff1b1f23);
    const juce::Colour kGrid   (0xff272c31);
    const juce::Colour kCurve  (0xffe6e6e6);
    const juce::Colour kSpec   (0x33ffffff);
}

// ============================== Graph ========================================
class DynEQAudioProcessorEditor::Graph final
    : public juce::Component
    , private juce::Timer
{
public:
    Graph (DynEQAudioProcessor& p, std::function<void (int)> onSelect, std::function<void()> onChanged)
        : proc (p), apvts (p.apvts), eq (p.eq), onSelectFn (std::move (onSelect)), onChangedFn (std::move (onChanged))
    {
        startTimerHz (30);
    }

    float xOfFreq (float f) const noexcept
    { return (std::log10 (f) - std::log10 (kFMin)) / (std::log10 (kFMax) - std::log10 (kFMin)) * (float) getWidth(); }

    float freqOfX (float x) const noexcept
    { return kFMin * std::pow (10.0f, (x / (float) getWidth()) * (std::log10 (kFMax) - std::log10 (kFMin))); }

    float yOfDb (float db) const noexcept
    { return (kDbMax - db) / (kDbMax - kDbMin) * (float) getHeight(); }

    float dbOfY (float y) const noexcept
    { return kDbMax - (y / (float) getHeight()) * (kDbMax - kDbMin); }

    void timerCallback() override
    {
        const int n = juce::jmin (proc.analyzerFifo.getNumReady(), (int) proc.analyzerFifo.getTotalSize());
        if (n > 0)
        {
            int s1, sz1, s2, sz2;
            proc.analyzerFifo.prepareToRead (n, s1, sz1, s2, sz2);
            auto feed = [&] (int s, int sz)
            {
                for (int i = 0; i < sz; ++i)
                {
                    timeDomain[(size_t) tdPos] = proc.analyzerBuf[(size_t) s + (size_t) i];
                    tdPos = (tdPos + 1) & (fftSize - 1);
                }
            };
            feed (s1, sz1);
            feed (s2, sz2);
            proc.analyzerFifo.finishedRead (n);
        }

        for (int i = 0; i < fftSize; ++i)
        {
            const float w = 0.5f * (1.0f - std::cos (2.0f * (float) kPi * i / (fftSize - 1)));
            fftBuf[i] = { timeDomain[i] * w, 0.0f };
        }
        fft.perform (fftBuf.data(), false);
        for (int k = 0; k < fftSize / 2; ++k)
        {
            const float re = fftBuf[k].real(), im = fftBuf[k].imag();
            const float mag = std::sqrt (re * re + im * im);
            const float db = 20.0f * std::log10 (mag / (fftSize * 0.5f) + 1e-9f);
            spectrum[k] = juce::jlimit (-90.0f, 0.0f, db);
        }

        for (int b = 0; b < kNumBands; ++b) grDb[b] = eq.currentGrDb (b);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const int w = getWidth(), h = getHeight();
        g.fillAll (kBg);

        // Grid.
        g.setColour (kGrid);
        const float decadeFs[] = { 20, 30, 40, 50, 60, 80, 100, 200, 300, 400, 500, 800,
                                   1000, 2000, 3000, 5000, 8000, 10000, 20000 };
        for (float f : decadeFs) { const int x = (int) xOfFreq (f); g.drawVerticalLine (x, 0, h); }
        for (int db = -18; db <= 18; db += 6)
        { const int y = (int) yOfDb ((float) db); g.setColour (db == 0 ? kCurve.withAlpha (0.25f) : kGrid); g.drawHorizontalLine (y, 0, w); g.setColour (kGrid); }

        // Spectrum.
        if (*apvts.getRawParameterValue (ParamIDs::analyzer) > 0.5f)
        {
            juce::Path p;
            p.startNewSubPath (0.0f, (float) h);
            for (int x = 0; x <= w; ++x)
            {
                const float f = freqOfX ((float) x);
                const int bin = juce::jlimit (0, fftSize / 2 - 1, (int) (f / (eq.sampleRate() * 0.5) * (fftSize / 2)));
                const float db = spectrum[bin];
                const float y = (0.0f - db) / 90.0f * (float) h;
                p.lineTo ((float) x, y);
            }
            p.lineTo ((float) w, (float) h);
            p.closeSubPath();
            g.setColour (kSpec);
            g.fillPath (p);
        }

        // EQ curve (static + dynamic-off response of the current parameters).
        dyneq::BandParams bands[kNumBands];
        for (int b = 0; b < kNumBands; ++b) readBand (b, bands[b]);

        juce::Path curve;
        bool started = false;
        for (int x = 0; x <= w; ++x)
        {
            const float f = freqOfX ((float) x);
            const auto resp = dyneq::totalResponse (bands, f, eq.sampleRate());
            const float db = 20.0f * std::log10 (std::abs (resp) + 1e-9f);
            const float y = yOfDb (juce::jlimit (kDbMin, kDbMax, db));
            if (!started) { curve.startNewSubPath ((float) x, y); started = true; }
            else curve.lineTo ((float) x, y);
        }
        g.setColour (kCurve);
        g.strokePath (curve, juce::PathStrokeType (1.6f));

        // Band nodes + live gain-reduction markers.
        for (int b = 0; b < kNumBands; ++b)
        {
            const float f = bands[b].freq, gain = bands[b].gainDb;
            const float nx = xOfFreq (f), ny = yOfDb (juce::jlimit (kDbMin, kDbMax, gain));
            g.setColour (kBandColours[b]);
            g.fillEllipse (nx - 7.0f, ny - 7.0f, 14.0f, 14.0f);
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.drawEllipse (nx - 7.0f, ny - 7.0f, 14.0f, 14.0f, 1.0f);
            g.setColour (juce::Colours::white);
            g.drawText (juce::String (b + 1), juce::Rectangle<int> ((int) nx - 7, (int) ny - 7, 14, 14),
                        juce::Justification::centred, false);

            if (bands[b].dynamic && grDb[b] > 0.15f)
            {
                const float gy = yOfDb (juce::jlimit (kDbMin, kDbMax, gain - grDb[b]));
                g.setColour (kBandColours[b].withAlpha (0.85f));
                g.drawEllipse (nx - 5.0f, gy - 5.0f, 10.0f, 10.0f, 1.8f);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int hit = hitTest (e.position);
        if (hit < 0) return;

        selected = hit;
        onSelectFn (hit);
        if (auto* f = apvts.getParameter (ParamIDs::b (hit, "freq"))) f->beginChangeGesture();
        if (auto* g = apvts.getParameter (ParamIDs::b (hit, "gain"))) g->beginChangeGesture();
        dragging = true;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (!dragging || selected < 0) return;
        const float f = juce::jlimit (kFMin, kFMax, freqOfX ((float) e.x));
        const float g = juce::jlimit (kDbMin, kDbMax, dbOfY ((float) e.y));
        if (auto* p = apvts.getParameter (ParamIDs::b (selected, "freq")))
            p->setValueNotifyingHost (p->getNormalisableRange().convertTo0to1 (f));
        if (auto* p = apvts.getParameter (ParamIDs::b (selected, "gain")))
            p->setValueNotifyingHost (p->getNormalisableRange().convertTo0to1 (g));
        onChangedFn();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (!dragging || selected < 0) return;
        if (auto* f = apvts.getParameter (ParamIDs::b (selected, "freq"))) f->endChangeGesture();
        if (auto* g = apvts.getParameter (ParamIDs::b (selected, "gain"))) g->endChangeGesture();
        dragging = false;
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        const int hit = hitTest (e.position);
        if (hit < 0) return;
        if (auto* p = apvts.getParameter (ParamIDs::b (hit, "q")))
        {
            const auto rng = p->getNormalisableRange();
            const float cur = rng.convertFrom0to1 (p->getValue());
            const float next = juce::jlimit (0.1f, 18.0f, cur + wheel.deltaY * 0.5f);
            p->setValueNotifyingHost (rng.convertTo0to1 (next));
            onChangedFn();
        }
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int hit = hitTest (e.position);
        if (hit < 0) return;
        resetBand (hit);
        onChangedFn();
    }

private:
    int hitTest (juce::Point<int> pos) const noexcept
    {
        for (int b = 0; b < kNumBands; ++b)
        {
            const float f = *apvts.getRawParameterValue (ParamIDs::b (b, "freq"));
            const float gain = *apvts.getRawParameterValue (ParamIDs::b (b, "gain"));
            const float nx = xOfFreq (f), ny = yOfDb (juce::jlimit (kDbMin, kDbMax, gain));
            const float dx = (float) pos.x - nx, dy = (float) pos.y - ny;
            if (dx * dx + dy * dy <= 13.0f * 13.0f) return b;
        }
        return -1;
    }

    void readBand (int b, dyneq::BandParams& p) const noexcept
    {
        p.freq    = *apvts.getRawParameterValue (ParamIDs::b (b, "freq"));
        p.gainDb  = *apvts.getRawParameterValue (ParamIDs::b (b, "gain"));
        p.q       = *apvts.getRawParameterValue (ParamIDs::b (b, "q"));
        p.mix     = *apvts.getRawParameterValue (ParamIDs::b (b, "mix"));
        p.type    = static_cast<dyneq::FilterType> ((int) *apvts.getRawParameterValue (ParamIDs::b (b, "type")));
        p.dynamic = *apvts.getRawParameterValue (ParamIDs::b (b, "dyn")) > 0.5f;
    }

    void resetBand (int b)
    {
        auto set = [&] (const char* what, float val)
        {
            if (auto* p = apvts.getParameter (ParamIDs::b (b, what)))
                p->setValueNotifyingHost (p->getNormalisableRange().convertTo0to1 (val));
        };
        set ("freq", kDefaultFreq[b]);
        set ("gain", 0.0f);
        set ("q", 1.0f);
        set ("mix", 1.0f);
        set ("type", 0.0f);
        set ("dyn", 0.0f);
        set ("thresh", -30.0f);
        set ("ratio", 3.0f);
        set ("range", 12.0f);
        set ("attack", 5.0f);
        set ("release", 120.0f);
    }

    DynEQAudioProcessor& proc;
    juce::AudioProcessorValueTreeState& apvts;
    dyneq::DynamicEQ& eq;
    std::function<void (int)> onSelectFn;
    std::function<void()> onChangedFn;

    int selected = 0;
    bool dragging = false;

    static constexpr int fftSize = 2048;
    juce::dsp::FFT fft { 11 };
    std::array<float, fftSize> timeDomain {};
    std::array<float, fftSize / 2> spectrum {};
    std::array<juce::dsp::Complex<float>, fftSize> fftBuf {};
    std::array<float, kNumBands> grDb {};
    int tdPos = 0;
};

// ============================== Editor =======================================
DynEQAudioProcessorEditor::DynEQAudioProcessorEditor (DynEQAudioProcessor& p)
    : juce::AudioProcessorEditor (p), proc (p), apvts (p.apvts)
{
    graph = std::make_unique<Graph> (p,
        [this] (int b) { selectedBand = b; updateBandControls(); },
        [this] { updateBandControls(); });
    addAndMakeVisible (*graph);

    titleLabel.setText ("DynEQ  ·  4-band parallel dynamic EQ", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (15.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (titleLabel);

    bypassButton.setButtonText ("Bypass");
    bypassButton.setClickingTogglesState (true);
    bypassButton.onClick = [this] { apvts.getParameter (ParamIDs::bypass)->setValueNotifyingHost (bypassButton.getToggleState() ? 1.0f : 0.0f); };
    addAndMakeVisible (bypassButton);

    analyzerButton.setButtonText ("Analyzer");
    analyzerButton.setClickingTogglesState (true);
    analyzerButton.setToggleState (*apvts.getRawParameterValue (ParamIDs::analyzer) > 0.5f, juce::dontSendNotification);
    analyzerButton.onClick = [this] { apvts.getParameter (ParamIDs::analyzer)->setValueNotifyingHost (analyzerButton.getToggleState() ? 1.0f : 0.0f); };
    addAndMakeVisible (analyzerButton);

    presetCombo.addItemList (Presets::names(), 1);
    presetCombo.setSelectedItemIndex (0, juce::dontSendNotification);
    presetCombo.onChange = [this]
    {
        Presets::apply (apvts, presetCombo.getSelectedItemIndex());
        updateBandControls();
        graph->repaint();
    };
    addAndMakeVisible (presetCombo);

    auto setupSlider = [this] (juce::Slider& s, std::function<juce::String()> idFn, const juce::String& unit, int decimals)
    {
        s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 52, 16);
        s.setRange (0.0, 1.0);
        s.textFromValueFunction = [this, idFn, unit, decimals] (double v)
        {
            if (auto* p = apvts.getParameter (idFn()))
                return juce::String (p->getNormalisableRange().convertFrom0to1 ((float) v), decimals) + unit;
            return juce::String (v, decimals);
        };
        s.onValueChange = [this, idFn, &s]
        {
            if (auto* p = apvts.getParameter (idFn())) { p->beginChangeGesture(); p->setValueNotifyingHost ((float) s.getValue()); p->endChangeGesture(); }
            updateBandControls();
        };
        addAndMakeVisible (s);
    };

    setupSlider (outGainSlider, [] { return juce::String (ParamIDs::outGain); }, " dB", 1);

    auto bandId = [this] (const char* what) { return ParamIDs::b (selectedBand, what); };

    setupSlider (freqSlider,    [this, bandId] { return bandId ("freq"); },    " Hz", 0);
    setupSlider (gainSlider,    [this, bandId] { return bandId ("gain"); },    " dB", 1);
    setupSlider (qSlider,       [this, bandId] { return bandId ("q"); },       "", 2);
    setupSlider (mixSlider,     [this, bandId] { return bandId ("mix"); },     "", 2);
    setupSlider (threshSlider,  [this, bandId] { return bandId ("thresh"); },  " dB", 1);
    setupSlider (ratioSlider,   [this, bandId] { return bandId ("ratio"); },   ":1", 0);
    setupSlider (rangeSlider,   [this, bandId] { return bandId ("range"); },   " dB", 1);
    setupSlider (attackSlider,  [this, bandId] { return bandId ("attack"); },  " ms", 1);
    setupSlider (releaseSlider, [this, bandId] { return bandId ("release"); }, " ms", 0);

    typeCombo.addItemList ({ "Bell", "Low Shelf", "High Shelf" }, 1);
    typeCombo.onChange = [this]
    {
        if (auto* p = apvts.getParameter (ParamIDs::b (selectedBand, "type")))
            p->setValueNotifyingHost ((float) (typeCombo.getSelectedItemIndex() / 2.0f));
        graph->repaint();
    };
    addAndMakeVisible (typeCombo);

    dynToggle.setButtonText ("Dyn");
    dynToggle.onClick = [this]
    {
        if (auto* p = apvts.getParameter (ParamIDs::b (selectedBand, "dyn")))
            p->setValueNotifyingHost (dynToggle.getToggleState() ? 1.0f : 0.0f);
        updateBandControls();
    };
    addAndMakeVisible (dynToggle);

    auto makeLabel = [this] (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::Font (11.0f));
        l.setColour (juce::Label::textColourId, juce::Colour (0xff9aa0a6));
        l.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (l);
    };
    makeLabel (typeLabel, "Type");
    makeLabel (dynLabel, "");
    makeLabel (freqLabel, "Freq");
    makeLabel (gainLabel, "Gain");
    makeLabel (qLabel, "Q");
    makeLabel (mixLabel, "Mix");
    makeLabel (threshLabel, "Thresh");
    makeLabel (ratioLabel, "Ratio");
    makeLabel (rangeLabel, "Range");
    makeLabel (attackLabel, "Attack");
    makeLabel (releaseLabel, "Release");
    makeLabel (presetLabel, "Preset");

    setSize (900, 620);
    updateBandControls();
}

DynEQAudioProcessorEditor::~DynEQAudioProcessorEditor() = default;

void DynEQAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (kPanel);
}

void DynEQAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();
    auto top = bounds.removeFromTop (52);
    auto bottom = bounds.removeFromBottom (168);

    graph->setBounds (bounds.reduced (6));

    auto tr = top.reduced (10, 8);
    titleLabel.setBounds (tr.removeFromLeft (200));
    auto presetCol = tr.removeFromLeft (150);
    presetLabel.setBounds (presetCol.removeFromTop (15));
    presetCombo.setBounds (presetCol);
    tr.removeFromLeft (8);
    outGainSlider.setBounds (tr.removeFromRight (64).withSizeKeepingCentre (64, 48));
    analyzerButton.setBounds (tr.removeFromRight (86).withSizeKeepingCentre (86, 24));
    bypassButton.setBounds (tr.removeFromRight (78).withSizeKeepingCentre (78, 24));

    auto ctl = bottom.reduced (10, 6);
    const int cols = 11;
    const int cw = ctl.getWidth() / cols;
    int x = ctl.getX();

    auto place = [&] (juce::Label& l, juce::Component& c, int span = 1)
    {
        juce::Rectangle<int> col (x, ctl.getY(), cw * span, ctl.getHeight());
        l.setBounds (col.removeFromTop (16));
        c.setBounds (col.reduced (3, 0).withTrimmedTop (4));
        x += cw * span;
    };

    place (typeLabel, typeCombo);
    place (dynLabel, dynToggle);
    place (freqLabel, freqSlider);
    place (gainLabel, gainSlider);
    place (qLabel, qSlider);
    place (mixLabel, mixSlider);
    place (threshLabel, threshSlider);
    place (ratioLabel, ratioSlider);
    place (rangeLabel, rangeSlider);
    place (attackLabel, attackSlider);
    place (releaseLabel, releaseSlider);
}

void DynEQAudioProcessorEditor::updateBandControls()
{
    if (auto* p = apvts.getParameter (ParamIDs::outGain))
        outGainSlider.setValue (p->getValue(), juce::dontSendNotification);

    auto set = [this] (juce::Slider& s, const char* what)
    {
        if (auto* p = apvts.getParameter (ParamIDs::b (selectedBand, what)))
            s.setValue (p->getValue(), juce::dontSendNotification);
    };
    set (freqSlider, "freq");
    set (gainSlider, "gain");
    set (qSlider, "q");
    set (mixSlider, "mix");
    set (threshSlider, "thresh");
    set (ratioSlider, "ratio");
    set (rangeSlider, "range");
    set (attackSlider, "attack");
    set (releaseSlider, "release");

    if (auto* p = apvts.getParameter (ParamIDs::b (selectedBand, "type")))
        typeCombo.setSelectedItemIndex ((int) (p->getValue() * 2.0f + 0.5f), juce::dontSendNotification);
    if (auto* p = apvts.getParameter (ParamIDs::b (selectedBand, "dyn")))
        dynToggle.setToggleState (p->getValue() > 0.5f, juce::dontSendNotification);

    const bool dynOn = dynToggle.getToggleState();
    threshSlider.setEnabled (dynOn);
    ratioSlider.setEnabled (dynOn);
    rangeSlider.setEnabled (dynOn);
    attackSlider.setEnabled (dynOn);
    releaseSlider.setEnabled (dynOn);
}
