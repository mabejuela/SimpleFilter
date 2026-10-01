#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
class SpectrumAnalyzer : public juce::Component,
                         private juce::Timer
{
public:
    explicit SpectrumAnalyzer(SimpleFilterAudioProcessor& p);
    void paint(juce::Graphics& g) override;

private:
    void timerCallback() override;

    SimpleFilterAudioProcessor& processor;
    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> window;

    static constexpr int fftSize = SimpleFilterAudioProcessor::fftSize;
    std::array<float, fftSize * 2> fftWorkBuffer {};
    std::array<float, fftSize / 2> displayData   {};
};

//==============================================================================
class VintageLookAndFeel : public juce::LookAndFeel_V4
{
public:
    VintageLookAndFeel();
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h,
                          float pos, float startAngle, float endAngle,
                          juce::Slider&) override;
};

//==============================================================================
class SimpleFilterAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit SimpleFilterAudioProcessorEditor(SimpleFilterAudioProcessor&);
    ~SimpleFilterAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    SimpleFilterAudioProcessor& audioProcessor;

    VintageLookAndFeel     laf;
    SpectrumAnalyzer       spectrumView;
    juce::MidiKeyboardComponent keyboard;

    // Knobs
    juce::Slider ratioKnob,     indexKnob;
    juce::Slider attackKnob,    decayKnob,   sustainKnob, releaseKnob;
    juce::Slider cutoffKnob,    resonanceKnob;
    juce::Slider lfoRateKnob,   lfoDepthKnob;
    juce::Slider gainKnob;

    // Labels
    juce::Label ratioLabel,     indexLabel;
    juce::Label attackLabel,    decayLabel,   sustainLabel, releaseLabel;
    juce::Label cutoffLabel,    resonanceLabel;
    juce::Label lfoRateLabel,   lfoDepthLabel;
    juce::Label gainLabel;
    juce::Label fmHeader, adsrHeader, filterHeader, lfoHeader, masterHeader;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    SliderAttachment ratioAtt,      indexAtt;
    SliderAttachment attackAtt,     decayAtt,   sustainAtt, releaseAtt;
    SliderAttachment cutoffAtt,     resonanceAtt;
    SliderAttachment lfoRateAtt,    lfoDepthAtt;
    SliderAttachment gainAtt;

    void setupKnob(juce::Slider& knob, juce::Label& label, const juce::String& text);
    void setupHeader(juce::Label& label, const juce::String& text);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SimpleFilterAudioProcessorEditor)
};
