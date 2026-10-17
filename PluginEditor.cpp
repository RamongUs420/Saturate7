#include "PluginEditor.h"

namespace s7ui
{
namespace
{
    void drawPanel (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title, juce::Colour accent)
    {
        g.setColour (juce::Colours::white.withAlpha (0.04f));
        g.fillRoundedRectangle (r.toFloat(), 8.0f);
        g.setColour (accent.withAlpha (0.35f));
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 8.0f, 1.0f);
        g.setColour (accent);
        g.setFont (makeFont (12.0f, true));
        g.drawText (title, r.getX() + 12, r.getY() + 4, 220, 16, juce::Justification::centredLeft);
    }
}

//==============================================================================
Saturate7AudioProcessorEditor::Saturate7AudioProcessorEditor (Saturate7AudioProcessor& p)
    : juce::AudioProcessorEditor (&p),
      proc (p),
      spectrum (p),
      hpf     (p.apvts, "hpf",     "HIGH-PASS", bandColour (4)),
      lpf     (p.apvts, "lpf",     "LOW-PASS",  bandColour (5)),
      inKnob  (p.apvts, "inGain",  "INPUT",     bandColour (3)),
      thrKnob (p.apvts, "thr",     "THRESHOLD", bandColour (1)),
      outKnob (p.apvts, "outGain", "OUTPUT",    bandColour (0))
{
    setLookAndFeel (&laf);
    setSize (1040, 700);

    addAndMakeVisible (spectrum);

    for (int i = 0; i < s7::numBands; ++i)
    {
        strips[(size_t) i] = std::make_unique<BandStrip> (p.apvts, i);
        addAndMakeVisible (*strips[(size_t) i]);
    }

    osBox.addItemList (s7::osNames(), 1);
    addAndMakeVisible (osBox);
    osAtt = std::make_unique<ComboBoxAttachment> (p.apvts, "os", osBox);

    addAndMakeVisible (hpf);
    addAndMakeVisible (lpf);
    addAndMakeVisible (inKnob);
    addAndMakeVisible (thrKnob);
    addAndMakeVisible (outKnob);

    // Selector de modo Saturate / Distort
    satBtn.setClickingTogglesState (true);
    distBtn.setClickingTogglesState (true);
    satBtn.setRadioGroupId (101);
    distBtn.setRadioGroupId (101);
    satBtn.setColour (juce::TextButton::buttonOnColourId, bandColour (3));
    distBtn.setColour (juce::TextButton::buttonOnColourId, bandColour (0));
    addAndMakeVisible (satBtn);
    addAndMakeVisible (distBtn);

    modeAtt = std::make_unique<juce::ParameterAttachment> (*p.apvts.getParameter ("mode"),
        [this] (float v)
        {
            const bool d = v > 0.5f;
            satBtn.setToggleState (! d, juce::dontSendNotification);
            distBtn.setToggleState (d, juce::dontSendNotification);
        });
    modeAtt->sendInitialUpdate();

    satBtn.onClick  = [this] { modeAtt->setValueAsCompleteGesture (0.0f); };
    distBtn.onClick = [this] { modeAtt->setValueAsCompleteGesture (1.0f); };

    curveBox.addItemList (s7::curveNames(), 1);
    addAndMakeVisible (curveBox);
    curveAtt = std::make_unique<ComboBoxAttachment> (p.apvts, "curve", curveBox);

    startTimerHz (30);
}

Saturate7AudioProcessorEditor::~Saturate7AudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

//==============================================================================
void Saturate7AudioProcessorEditor::timerCallback()
{
    if (spectrum.update())
        spectrum.repaint();

    for (size_t b = 0; b < (size_t) s7::numBands; ++b)
    {
        const float peak = proc.bandPeak[b].exchange (0.0f);
        meterLevel[b] = juce::jmax (peak, meterLevel[b] * 0.82f);
        strips[b]->setMeterLevel (meterLevel[b]);
    }
}

//==============================================================================
void Saturate7AudioProcessorEditor::paint (juce::Graphics& g)
{
    using namespace juce;

    g.setGradientFill (ColourGradient (Colour (0xff0b0c10), 0.0f, 0.0f,
                                       Colour (0xff171a21), 0.0f, (float) getHeight(), false));
    g.fillAll();

    // Titulo
    AttributedString title;
    title.append ("SATURATE", makeFont (26.0f, true), Colours::white);
    title.append ("7", makeFont (26.0f, true), bandColour (0));
    title.draw (g, Rectangle<float> (20.0f, 10.0f, 260.0f, 36.0f));

    g.setColour (Colours::white.withAlpha (0.45f));
    g.setFont (makeFont (11.0f));
    g.drawText ("7-BAND MULTIBAND SATURATOR / DISTORTION", 190, 20, 340, 16, Justification::centredLeft);

    g.setColour (Colours::white.withAlpha (0.6f));
    g.setFont (makeFont (12.0f, true));
    g.drawText ("OVERSAMPLING", osLabelArea, Justification::centredRight);

    drawPanel (g, filtersPanel, "MAIN FILTERS", bandColour (4));
    drawPanel (g, masterPanel, "MASTER SECTION", bandColour (0));

    g.setColour (Colours::white.withAlpha (0.7f));
    g.setFont (makeFont (12.0f, true));
    g.drawText ("MODE",  modeLabelArea,  Justification::centred);
    g.drawText ("CURVE", curveLabelArea, Justification::centred);
}

void Saturate7AudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced (16, 10);

    auto top = r.removeFromTop (40);
    osBox.setBounds (top.removeFromRight (110).reduced (0, 6));
    osLabelArea = top.removeFromRight (120);
    r.removeFromTop (6);

    auto bandArea = r.removeFromTop (350);
    spectrum.setBounds (bandArea);
    const int w = bandArea.getWidth() / s7::numBands;
    for (int i = 0; i < s7::numBands; ++i)
    {
        auto cell = (i == s7::numBands - 1) ? bandArea : bandArea.removeFromLeft (w);
        strips[(size_t) i]->setBounds (cell);
    }

    r.removeFromTop (8);
    filtersPanel = r.removeFromTop (118);
    {
        auto a = filtersPanel.reduced (10, 4);
        a.removeFromTop (18);
        const int kw = 150;
        const int cx = a.getCentreX();
        hpf.setBounds (cx - kw - 20, a.getY(), kw, a.getHeight());
        lpf.setBounds (cx + 20, a.getY(), kw, a.getHeight());
    }

    r.removeFromTop (8);
    masterPanel = r;
    {
        auto a = masterPanel.reduced (10, 4);
        a.removeFromTop (18);
        const int cw = a.getWidth() / 5;

        inKnob.setBounds (a.removeFromLeft (cw));

        auto modeCol = a.removeFromLeft (cw).reduced (30, 0);
        modeLabelArea = modeCol.removeFromTop (22);
        modeCol.removeFromTop (6);
        satBtn.setBounds (modeCol.removeFromTop (30));
        modeCol.removeFromTop (8);
        distBtn.setBounds (modeCol.removeFromTop (30));

        thrKnob.setBounds (a.removeFromLeft (cw));

        auto curveCol = a.removeFromLeft (cw).reduced (24, 0);
        curveLabelArea = curveCol.removeFromTop (22);
        curveCol.removeFromTop (6);
        curveBox.setBounds (curveCol.removeFromTop (30));

        outKnob.setBounds (a);
    }
}
} // namespace s7ui
