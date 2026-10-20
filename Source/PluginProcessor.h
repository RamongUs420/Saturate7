#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <memory>

#include "Dsp.h"

namespace s7
{
constexpr int numBands = 7;
constexpr int fftOrder = 12;
constexpr int fftSize  = 1 << fftOrder;

// Bordes de banda en Hz: SUB | LOW | LOW-MID | MID | HI-MID | HIGH | AIR
inline const std::array<double, numBands + 1>& bandEdges()
{
    static const std::array<double, numBands + 1> e { 20.0, 60.0, 250.0, 1000.0, 3500.0, 8000.0, 14000.0, 20000.0 };
    return e;
}

inline const char* bandName (int i)
{
    static const char* n[numBands] = { "SUB", "LOW", "LOW-MID", "MID", "HI-MID", "HIGH", "AIR" };
    return n[i];
}

inline juce::StringArray curveNames() { return { "Tanh Soft", "Hard Clip", "Tube", "Tape", "Asymmetric", "Foldback", "Arctan", "Sine" }; }
inline juce::StringArray osNames()    { return { "Off", "2x", "4x", "8x", "16x" }; }
inline juce::StringArray modeNames()  { return { "Saturate", "Distort" }; }
} // namespace s7

//==============================================================================
class Saturate7AudioProcessor : public juce::AudioProcessor
{
public:
    Saturate7AudioProcessor();
    ~Saturate7AudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Saturate7"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::AudioProcessorValueTreeState apvts;

    // Datos para la GUI
    std::array<std::atomic<float>, s7::numBands> bandPeak;
    // Copia las ultimas 'num' muestras (mono, ya procesadas) para el analizador
    void copyLatestSamples (float* dest, int num) const;
    double getCurrentSampleRate() const { return currentSampleRate.load(); }

private:
    void prepareDsp (int osIdx);
    void updateParameters();
    void processCore (juce::dsp::AudioBlock<float>& block);

    std::atomic<double> currentSampleRate { 44100.0 };
    int maxBlock = 512;
    int activeOs = -1;
    double osRate = 44100.0;

    // Parametros (punteros cacheados)
    std::atomic<float>* pIn = nullptr;
    std::atomic<float>* pOut = nullptr;
    std::atomic<float>* pThr = nullptr;
    std::atomic<float>* pHp = nullptr;
    std::atomic<float>* pLp = nullptr;
    std::atomic<float>* pMode = nullptr;
    std::atomic<float>* pCurve = nullptr;
    std::atomic<float>* pOs = nullptr;
    std::array<std::atomic<float>*, s7::numBands> pMix {};
    std::array<std::atomic<float>*, s7::numBands> pSolo {};
    std::array<std::atomic<float>*, s7::numBands> pMute {};

    // Oversampling (2x, 4x, 8x, 16x)
    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 4> oversamplers;

    // Multibanda
    std::array<s7::Crossover, s7::numBands - 1> xover;
    std::array<std::array<s7::Crossover, s7::numBands - 1>, s7::numBands - 1> comp;

    // Filtros master y DC
    s7::Biquad hpF, lpF;
    float lastHp = -1.0f, lastLp = -1.0f;
    bool hpOn = false, lpOn = false;
    double dcR = 0.999, dcX1[2] = { 0.0, 0.0 }, dcY1[2] = { 0.0, 0.0 };

    // Estado por bloque
    int curveIdx = 0;
    double thrLin = 0.25, drive = 1.0, post = 1.0;
    std::array<bool, s7::numBands> audible {};
    juce::SmoothedValue<float> inSm, outSm;
    std::array<juce::SmoothedValue<float>, s7::numBands> mixSm;

    // Buffer circular para el analizador (solapamiento continuo => animacion fluida)
    static constexpr int ringSize = 8192;
    std::array<float, ringSize> ring {};
    std::atomic<int> ringWrite { 0 };
    bool prepared = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Saturate7AudioProcessor)
};
