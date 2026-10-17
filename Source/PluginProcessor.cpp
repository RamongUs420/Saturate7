#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

//==============================================================================
APVTS::ParameterLayout Saturate7AudioProcessor::createLayout()
{
    using namespace juce;
    APVTS::ParameterLayout layout;

    auto dbAttr = []
    {
        return AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; });
    };

    auto hzAttr = []
    {
        return AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int)
            {
                return v >= 1000.0f ? String (v / 1000.0f, 2) + " kHz" : String (roundToInt (v)) + " Hz";
            })
            .withValueFromStringFunction ([] (const String& t)
            {
                float v = t.getFloatValue();
                if (t.containsIgnoreCase ("k")) v *= 1000.0f;
                return v;
            });
    };

    auto pctAttr = []
    {
        return AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " %"; });
    };

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "inGain", 1 }, "Input Gain",
                                                       NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f, dbAttr()));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "thr", 1 }, "Threshold",
                                                       NormalisableRange<float> (-48.0f, 0.0f, 0.01f), -12.0f, dbAttr()));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "outGain", 1 }, "Output Gain",
                                                       NormalisableRange<float> (-24.0f, 12.0f, 0.01f), 0.0f, dbAttr()));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "hpf", 1 }, "High-Pass",
                                                       NormalisableRange<float> (20.0f, 5000.0f, 0.0f, 0.3f), 20.0f, hzAttr()));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "lpf", 1 }, "Low-Pass",
                                                       NormalisableRange<float> (200.0f, 20000.0f, 0.0f, 0.3f), 20000.0f, hzAttr()));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Mode", s7::modeNames(), 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "curve", 1 }, "Curve", s7::curveNames(), 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "os", 1 }, "Oversampling", s7::osNames(), 1));

    for (int i = 0; i < s7::numBands; ++i)
    {
        const String n (s7::bandName (i));
        const String idx (i + 1);
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "mix" + idx, 1 }, n + " Mix",
                                                           NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f, pctAttr()));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { "solo" + idx, 1 }, n + " Solo", false));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { "mute" + idx, 1 }, n + " Mute", false));
    }

    return layout;
}

//==============================================================================
Saturate7AudioProcessor::Saturate7AudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createLayout())
{
    pIn    = apvts.getRawParameterValue ("inGain");
    pOut   = apvts.getRawParameterValue ("outGain");
    pThr   = apvts.getRawParameterValue ("thr");
    pHp    = apvts.getRawParameterValue ("hpf");
    pLp    = apvts.getRawParameterValue ("lpf");
    pMode  = apvts.getRawParameterValue ("mode");
    pCurve = apvts.getRawParameterValue ("curve");
    pOs    = apvts.getRawParameterValue ("os");

    for (int i = 0; i < s7::numBands; ++i)
    {
        const juce::String idx (i + 1);
        pMix[(size_t) i]  = apvts.getRawParameterValue ("mix" + idx);
        pSolo[(size_t) i] = apvts.getRawParameterValue ("solo" + idx);
        pMute[(size_t) i] = apvts.getRawParameterValue ("mute" + idx);
        bandPeak[(size_t) i].store (0.0f);
        audible[(size_t) i] = true;
    }

    for (size_t i = 0; i < oversamplers.size(); ++i)
        oversamplers[i] = std::make_unique<juce::dsp::Oversampling<float>> (
            2, i + 1, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true);
}

//==============================================================================
bool Saturate7AudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;

    return in == out;
}

void Saturate7AudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    maxBlock = juce::jmax (1, samplesPerBlock);

    for (auto& o : oversamplers)
    {
        o->initProcessing ((size_t) maxBlock);
        o->reset();
    }

    fifoIndex = 0;
    activeOs = -1;
    prepareDsp (juce::jlimit (0, 4, juce::roundToInt (pOs->load())));
}

void Saturate7AudioProcessor::prepareDsp (int osIdx)
{
    activeOs = osIdx;
    const double fs = currentSampleRate.load() * (double) (1 << osIdx);
    osRate = fs;

    const auto& e = s7::bandEdges();

    for (int k = 0; k < s7::numBands - 1; ++k)
        xover[(size_t) k].setup (e[(size_t) k + 1], fs);

    // Compensacion all-pass: la banda k pasa por los crossovers j > k
    for (int k = 0; k < s7::numBands - 2; ++k)
        for (int j = k + 1; j < s7::numBands - 1; ++j)
            comp[(size_t) k][(size_t) j].setup (e[(size_t) j + 1], fs);

    dcR = 1.0 - 2.0 * s7::kPi * 10.0 / fs;
    for (int c = 0; c < 2; ++c) { dcX1[c] = 0.0; dcY1[c] = 0.0; }

    hpF.reset(); lpF.reset();
    lastHp = lastLp = -1.0f;

    inSm.reset (fs, 0.03);
    outSm.reset (fs, 0.03);
    inSm.setCurrentAndTargetValue  (juce::Decibels::decibelsToGain (pIn->load()));
    outSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pOut->load()));

    for (size_t b = 0; b < (size_t) s7::numBands; ++b)
    {
        mixSm[b].reset (fs, 0.03);
        mixSm[b].setCurrentAndTargetValue (pMix[b]->load() * 0.01f);
    }

    if (osIdx == 0)
        setLatencySamples (0);
    else
        setLatencySamples (juce::roundToInt (oversamplers[(size_t) osIdx - 1]->getLatencyInSamples()));
}

void Saturate7AudioProcessor::updateParameters()
{
    const bool distort = pMode->load() > 0.5f;
    curveIdx = juce::jlimit (0, 7, juce::roundToInt (pCurve->load()));
    thrLin   = (double) juce::Decibels::decibelsToGain (pThr->load());
    drive    = distort ? 5.0 : 1.0;
    post     = distort ? 0.7 : 1.0;

    inSm.setTargetValue  (juce::Decibels::decibelsToGain (pIn->load()));
    outSm.setTargetValue (juce::Decibels::decibelsToGain (pOut->load()));

    bool anySolo = false;
    for (size_t b = 0; b < (size_t) s7::numBands; ++b)
        if (pSolo[b]->load() > 0.5f) anySolo = true;

    for (size_t b = 0; b < (size_t) s7::numBands; ++b)
    {
        mixSm[b].setTargetValue (pMix[b]->load() * 0.01f);
        const bool muted  = pMute[b]->load() > 0.5f;
        const bool soloed = pSolo[b]->load() > 0.5f;
        audible[b] = ! muted && (! anySolo || soloed);
    }

    const float hp = pHp->load();
    hpOn = hp > 20.5f;
    if (hpOn)
    {
        if (hp != lastHp) { hpF.setHighpass ((double) hp, osRate); lastHp = hp; }
    }
    else { hpF.reset(); lastHp = -1.0f; }

    const float lp = pLp->load();
    lpOn = lp < 19900.0f;
    if (lpOn)
    {
        if (lp != lastLp) { lpF.setLowpass ((double) lp, osRate); lastLp = lp; }
    }
    else { lpF.reset(); lastLp = -1.0f; }
}

//==============================================================================
void Saturate7AudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh = juce::jmin (2, buffer.getNumChannels());
    const int numSamples = buffer.getNumSamples();

    if (numCh == 0 || numSamples == 0)
        return;

    for (int ch = numCh; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    const int osIdx = juce::jlimit (0, 4, juce::roundToInt (pOs->load()));
    if (osIdx != activeOs)
        prepareDsp (osIdx);

    updateParameters();

    juce::dsp::AudioBlock<float> block (buffer);
    auto sub = block.getSubsetChannelBlock (0, (size_t) numCh);

    if (osIdx == 0)
    {
        processCore (sub);
    }
    else
    {
        auto& os = *oversamplers[(size_t) osIdx - 1];
        auto up = os.processSamplesUp (sub);
        processCore (up);
        os.processSamplesDown (sub);
    }

    // Alimentar el analizador de espectro (mono, a la frecuencia base)
    const float* l = buffer.getReadPointer (0);
    const float* r = numCh > 1 ? buffer.getReadPointer (1) : l;

    for (int i = 0; i < numSamples; ++i)
    {
        fifo[(size_t) fifoIndex++] = 0.5f * (l[i] + r[i]);

        if (fifoIndex >= s7::fftSize)
        {
            fifoIndex = 0;
            if (! spectrumReady.load())
            {
                spectrumData = fifo;
                spectrumReady.store (true);
            }
        }
    }
}

void Saturate7AudioProcessor::processCore (juce::dsp::AudioBlock<float>& blk)
{
    const int numCh = (int) blk.getNumChannels();
    const int n = (int) blk.getNumSamples();

    float* ptr[2] = { blk.getChannelPointer (0), numCh > 1 ? blk.getChannelPointer (1) : nullptr };

    float localPeak[s7::numBands] = {};

    for (int i = 0; i < n; ++i)
    {
        const double inG  = (double) inSm.getNextValue();
        const double outG = (double) outSm.getNextValue();

        double mix[s7::numBands];
        for (size_t b = 0; b < (size_t) s7::numBands; ++b)
            mix[b] = (double) mixSm[b].getNextValue();

        for (int c = 0; c < numCh; ++c)
        {
            // 1) Division en 7 bandas (LR4 en serie)
            double rem = (double) ptr[c][i] * inG;
            double bands[s7::numBands];

            for (int k = 0; k < s7::numBands - 1; ++k)
            {
                double lo, hi;
                xover[(size_t) k].process (c, rem, lo, hi);
                bands[k] = lo;
                rem = hi;
            }
            bands[s7::numBands - 1] = rem;

            // 2) Compensacion de fase para que la suma sea plana con mix = 0
            for (int k = 0; k < s7::numBands - 2; ++k)
                for (int j = k + 1; j < s7::numBands - 1; ++j)
                    bands[k] = comp[(size_t) k][(size_t) j].allpass (c, bands[k]);

            // 3) Procesado por banda
            double sum = 0.0;
            for (int b = 0; b < s7::numBands; ++b)
            {
                const double dry = bands[b];
                double y = dry;

                if (mix[b] > 0.0001)
                {
                    const double wet = thrLin * s7::shape (curveIdx, drive * dry / thrLin) * post;
                    y = dry + (wet - dry) * mix[b];
                }

                if (! audible[(size_t) b])
                    y = 0.0;

                const float ay = (float) std::abs (y);
                if (ay > localPeak[b]) localPeak[b] = ay;

                sum += y;
            }

            // 4) Filtros master, bloqueo de DC y ganancia de salida
            if (hpOn) sum = hpF.process (c, sum);
            if (lpOn) sum = lpF.process (c, sum);

            const double dcOut = sum - dcX1[c] + dcR * dcY1[c];
            dcX1[c] = sum;
            dcY1[c] = dcOut;

            double out = dcOut * outG;
            if (! std::isfinite (out)) out = 0.0;

            ptr[c][i] = (float) out;
        }
    }

    for (size_t b = 0; b < (size_t) s7::numBands; ++b)
        if (localPeak[b] > bandPeak[b].load())
            bandPeak[b].store (localPeak[b]);
}

//==============================================================================
juce::AudioProcessorEditor* Saturate7AudioProcessor::createEditor()
{
    return new Saturate7AudioProcessorEditor (*this);
}

void Saturate7AudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void Saturate7AudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new Saturate7AudioProcessor();
}
