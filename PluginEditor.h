#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

namespace s7ui
{
using SliderAttachment   = juce::AudioProcessorValueTreeState::SliderAttachment;
using ButtonAttachment   = juce::AudioProcessorValueTreeState::ButtonAttachment;
using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

inline juce::Colour bandColour (int i)
{
    static const juce::Colour c[s7::numBands] = {
        juce::Colour (0xffff2d6f), // SUB      magenta
        juce::Colour (0xffff7a1a), // LOW      naranja
        juce::Colour (0xffffd21a), // LOW-MID  amarillo
        juce::Colour (0xff4cff6a), // MID      verde
        juce::Colour (0xff1af0e0), // HI-MID   cian
        juce::Colour (0xff2f8bff), // HIGH     azul
        juce::Colour (0xffb455ff)  // AIR      violeta
    };
    return c[i];
}

inline juce::Font makeFont (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

//==============================================================================
class NeonLookAndFeel : public juce::LookAndFeel_V4
{
public:
    NeonLookAndFeel()
    {
        using namespace juce;
        setColour (Slider::textBoxTextColourId, Colours::white.withAlpha (0.85f));
        setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
        setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
        setColour (ComboBox::backgroundColourId, Colour (0xff15171c));
        setColour (ComboBox::textColourId, Colours::white);
        setColour (ComboBox::outlineColourId, Colour (0xff3a3d46));
        setColour (ComboBox::arrowColourId, Colours::white);
        setColour (PopupMenu::backgroundColourId, Colour (0xff15171c));
        setColour (PopupMenu::textColourId, Colours::white);
        setColour (PopupMenu::highlightedBackgroundColourId, Colour (0xff3a3d46));
        setColour (TextButton::buttonColourId, Colour (0xff22252c));
        setColour (TextButton::textColourOffId, Colours::white.withAlpha (0.8f));
        setColour (TextButton::textColourOnId, Colours::black);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, juce::Slider& slider) override
    {
        using namespace juce;
        const auto col = slider.findColour (Slider::rotarySliderFillColourId);
        auto bounds = Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (6.0f);
        const float radius = jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const float arcR = radius - 3.0f;
        const float angle = startAngle + pos * (endAngle - startAngle);

        g.setColour (Colour (0xff0e0f13));
        g.fillEllipse (centre.x - radius + 7.0f, centre.y - radius + 7.0f, (radius - 7.0f) * 2.0f, (radius - 7.0f) * 2.0f);

        Path track;
        track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (Colour (0xff2a2d35));
        g.strokePath (track, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));

        Path value;
        value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, angle, true);
        g.setColour (col.withAlpha (0.25f));
        g.strokePath (value, PathStrokeType (8.0f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (col);
        g.strokePath (value, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));

        const float r1 = radius * 0.30f, r2 = radius - 11.0f;
        Line<float> ptr (centre.x + r1 * std::sin (angle), centre.y - r1 * std::cos (angle),
                         centre.x + r2 * std::sin (angle), centre.y - r2 * std::cos (angle));
        g.setColour (Colours::white);
        g.drawLine (ptr, 2.5f);
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                           float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        using namespace juce;
        if (style != Slider::LinearVertical)
        {
            LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
            return;
        }

        const auto col = slider.findColour (Slider::thumbColourId);
        const float cx = (float) x + (float) width * 0.5f;
        const float trackW = 8.0f;
        Rectangle<float> track (cx - trackW * 0.5f, (float) y, trackW, (float) height);

        g.setColour (Colour (0xff0b0c0f));
        g.fillRoundedRectangle (track, 4.0f);

        Rectangle<float> fill (track.getX(), sliderPos, trackW, track.getBottom() - sliderPos);
        g.setColour (col.withAlpha (0.30f));
        g.fillRoundedRectangle (fill.expanded (3.0f, 0.0f), 5.0f);
        g.setColour (col);
        g.fillRoundedRectangle (fill, 4.0f);

        Rectangle<float> thumb (0.0f, 0.0f, 34.0f, 12.0f);
        thumb.setCentre (cx, sliderPos);
        g.setColour (Colour (0xffe8e8ec));
        g.fillRoundedRectangle (thumb, 3.0f);
        g.setColour (col);
        g.fillRect (thumb.withSizeKeepingCentre (thumb.getWidth() - 10.0f, 2.0f));
    }
};

//==============================================================================
class BandMeter : public juce::Component
{
public:
    explicit BandMeter (juce::Colour c) : colour (c) {}

    void setLevel (float lin)
    {
        level = lin;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        using namespace juce;
        auto r = getLocalBounds().toFloat();
        g.setColour (Colour (0xff0b0c0f));
        g.fillRoundedRectangle (r, 3.0f);

        const int segs = 14;
        const float gap = 1.5f;
        const float sw = (r.getWidth() - gap * (float) (segs + 1)) / (float) segs;
        const float db = Decibels::gainToDecibels (level, -60.0f);
        const float prop = jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        const int lit = roundToInt (prop * (float) segs);

        for (int i = 0; i < segs; ++i)
        {
            Rectangle<float> s (r.getX() + gap + (float) i * (sw + gap), r.getY() + gap, sw, r.getHeight() - 2.0f * gap);
            const bool on = i < lit;
            const Colour c = (i >= segs - 2) ? Colour (0xffff3b3b) : colour;
            g.setColour (on ? c : colour.withAlpha (0.12f));
            g.fillRect (s);
        }
    }

private:
    juce::Colour colour;
    float level = 0.0f;
};

//==============================================================================
class BandStrip : public juce::Component
{
public:
    BandStrip (juce::AudioProcessorValueTreeState& apvts, int index)
        : idx (index), colour (bandColour (index)), meter (bandColour (index))
    {
        const juce::String n (index + 1);

        mix.setSliderStyle (juce::Slider::LinearVertical);
        mix.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 20);
        mix.setColour (juce::Slider::thumbColourId, colour);
        mix.setDoubleClickReturnValue (true, 50.0);

        solo.setClickingTogglesState (true);
        mute.setClickingTogglesState (true);
        solo.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffffd21a));
        mute.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffff4040));

        addAndMakeVisible (mix);
        addAndMakeVisible (solo);
        addAndMakeVisible (mute);
        addAndMakeVisible (meter);

        mixAtt  = std::make_unique<SliderAttachment> (apvts, "mix" + n, mix);
        soloAtt = std::make_unique<ButtonAttachment> (apvts, "solo" + n, solo);
        muteAtt = std::make_unique<ButtonAttachment> (apvts, "mute" + n, mute);
    }

    void setMeterLevel (float v) { meter.setLevel (v); }

    void paint (juce::Graphics& g) override
    {
        using namespace juce;
        auto b = getLocalBounds().toFloat().reduced (3.0f);
        g.setColour (Colours::black.withAlpha (0.42f));
        g.fillRoundedRectangle (b, 8.0f);
        g.setColour (colour.withAlpha (0.55f));
        g.drawRoundedRectangle (b, 8.0f, 1.2f);

        const auto& e = s7::bandEdges();
        auto f = [] (double v)
        {
            if (v >= 1000.0)
                return String (v / 1000.0, std::fmod (v, 1000.0) == 0.0 ? 0 : 1) + "k";
            return String ((int) v);
        };

        g.setColour (colour);
        g.setFont (makeFont (15.0f, true));
        g.drawText (s7::bandName (idx), getLocalBounds().withTrimmedTop (10).withHeight (20), Justification::centred);

        g.setColour (Colours::white.withAlpha (0.55f));
        g.setFont (makeFont (11.0f));
        g.drawText (f (e[(size_t) idx]) + "-" + f (e[(size_t) idx + 1]) + " Hz",
                    getLocalBounds().withTrimmedTop (30).withHeight (14), Justification::centred);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 8);
        r.removeFromTop (40);
        auto row = r.removeFromTop (26);
        solo.setBounds (row.removeFromLeft (row.getWidth() / 2 - 2));
        row.removeFromLeft (4);
        mute.setBounds (row);
        r.removeFromTop (6);
        meter.setBounds (r.removeFromBottom (14));
        r.removeFromBottom (6);
        mix.setBounds (r);
    }

private:
    int idx;
    juce::Colour colour;
    juce::Slider mix;
    juce::TextButton solo { "S" }, mute { "M" };
    BandMeter meter;
    std::unique_ptr<SliderAttachment> mixAtt;
    std::unique_ptr<ButtonAttachment> soloAtt, muteAtt;
};

//==============================================================================
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID,
          const juce::String& title, juce::Colour c)
    {
        label.setText (title, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (makeFont (12.0f, true));
        label.setColour (juce::Label::textColourId, c);

        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 18);
        slider.setColour (juce::Slider::rotarySliderFillColourId, c);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f,
                                    juce::MathConstants<float>::pi * 2.8f, true);

        addAndMakeVisible (label);
        addAndMakeVisible (slider);
        att = std::make_unique<SliderAttachment> (apvts, paramID, slider);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        label.setBounds (r.removeFromTop (16));
        slider.setBounds (r);
    }

private:
    juce::Label label;
    juce::Slider slider;
    std::unique_ptr<SliderAttachment> att;
};

//==============================================================================
// Analizador FFT: cada banda ocupa la misma anchura que su columna de controles.
class SpectrumView : public juce::Component
{
public:
    explicit SpectrumView (Saturate7AudioProcessor& p)
        : proc (p), fft (s7::fftOrder),
          window ((size_t) s7::fftSize, juce::dsp::WindowingFunction<float>::hann)
    {
        fftData.fill (0.0f);
        smoothDb.fill (-120.0f);
        setInterceptsMouseClicks (false, false);
    }

    // Devuelve true si hay datos nuevos
    bool update()
    {
        if (! proc.spectrumReady.load())
            return false;

        std::copy (proc.spectrumData.begin(), proc.spectrumData.end(), fftData.begin());
        std::fill (fftData.begin() + s7::fftSize, fftData.end(), 0.0f);
        proc.spectrumReady.store (false);

        window.multiplyWithWindowingTable (fftData.data(), (size_t) s7::fftSize);
        fft.performFrequencyOnlyForwardTransform (fftData.data());

        const double fs = proc.getCurrentSampleRate();
        for (int k = 0; k <= s7::fftSize / 2; ++k)
        {
            const float mag = fftData[(size_t) k] / ((float) s7::fftSize * 0.25f);
            float db = juce::Decibels::gainToDecibels (mag, -120.0f);

            if (k > 0)
                db += 2.5f * (float) std::log2 ((double) k * fs / (double) s7::fftSize / 1000.0);

            auto& s = smoothDb[(size_t) k];
            s = db > s ? db : s * 0.88f + db * 0.12f;
        }
        return true;
    }

    void paint (juce::Graphics& g) override
    {
        using namespace juce;
        const float w = (float) getWidth(), h = (float) getHeight();
        const float colW = w / (float) s7::numBands;
        const double fs = proc.getCurrentSampleRate();
        const auto& e = s7::bandEdges();

        g.setColour (Colour (0xff08090c));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);

        for (int b = 0; b < s7::numBands; ++b)
        {
            const float x0 = (float) b * colW;
            const int steps = jmax (8, (int) (colW / 2.0f));
            Path line;

            for (int s = 0; s <= steps; ++s)
            {
                const float t = (float) s / (float) steps;
                const double f = e[(size_t) b] * std::pow (e[(size_t) b + 1] / e[(size_t) b], (double) t);
                const double pos = f * (double) s7::fftSize / fs;
                const int k = jlimit (0, s7::fftSize / 2 - 1, (int) pos);
                const float frac = (float) (pos - (double) k);
                const float db = smoothDb[(size_t) k] * (1.0f - frac) + smoothDb[(size_t) k + 1] * frac;
                const float norm = jlimit (0.0f, 1.0f, jmap (db, -90.0f, -6.0f, 0.0f, 1.0f));
                const float px = x0 + t * colW;
                const float py = h - norm * h * 0.95f;

                if (s == 0) line.startNewSubPath (px, py);
                else        line.lineTo (px, py);
            }

            Path fill (line);
            fill.lineTo (x0 + colW, h);
            fill.lineTo (x0, h);
            fill.closeSubPath();

            const auto col = bandColour (b);
            g.setGradientFill (ColourGradient (col.withAlpha (0.45f), 0.0f, 0.0f,
                                               col.withAlpha (0.05f), 0.0f, h, false));
            g.fillPath (fill);
            g.setColour (col.withAlpha (0.9f));
            g.strokePath (line, PathStrokeType (1.5f));

            if (b > 0)
            {
                g.setColour (Colours::white.withAlpha (0.06f));
                g.drawVerticalLine ((int) x0, 0.0f, h);
            }
        }
    }

private:
    Saturate7AudioProcessor& proc;
    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> window;
    std::array<float, 2 * s7::fftSize> fftData;
    std::array<float, s7::fftSize / 2 + 2> smoothDb;
};

//==============================================================================
class Saturate7AudioProcessorEditor : public juce::AudioProcessorEditor,
                                      private juce::Timer
{
public:
    explicit Saturate7AudioProcessorEditor (Saturate7AudioProcessor&);
    ~Saturate7AudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    Saturate7AudioProcessor& proc;
    NeonLookAndFeel laf;

    SpectrumView spectrum;
    std::array<std::unique_ptr<BandStrip>, s7::numBands> strips;
    std::array<float, s7::numBands> meterLevel {};

    juce::ComboBox osBox;
    std::unique_ptr<ComboBoxAttachment> osAtt;

    Knob hpf, lpf, inKnob, thrKnob, outKnob;

    juce::TextButton satBtn { "SATURATE" }, distBtn { "DISTORT" };
    std::unique_ptr<juce::ParameterAttachment> modeAtt;

    juce::ComboBox curveBox;
    std::unique_ptr<ComboBoxAttachment> curveAtt;

    juce::Rectangle<int> osLabelArea, filtersPanel, masterPanel, modeLabelArea, curveLabelArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Saturate7AudioProcessorEditor)
};
} // namespace s7ui

using Saturate7AudioProcessorEditor = s7ui::Saturate7AudioProcessorEditor;
