#pragma once
#include <JuceHeader.h>
#include <array>

//==============================================================================
struct FMVoice
{
    int    midiNote  = -1;
    bool   active    = false;
    double frequency = 440.0;
    float  velocity  = 0.0f;

    double carrierPhase    = 0.0;
    double modulatorPhase  = 0.0;

    enum class ADSRState { Idle, Attack, Decay, Sustain, Release };
    ADSRState adsrState = ADSRState::Idle;

    double envelope     = 0.0;
    double attackInc    = 0.0;
    double decayInc     = 0.0;
    double sustainLevel = 0.7;
    double releaseInc   = 0.0;

    // Per-voice biquad low-pass filter state (Audio EQ Cookbook)
    double x1=0, x2=0, y1=0, y2=0;
    double b0=1, b1=0, b2=0, a1=0, a2=0;

    void updateFilter(double cutoffHz, double q, double sampleRate)
    {
        cutoffHz = std::min(cutoffHz, sampleRate * 0.45);
        double w0    = juce::MathConstants<double>::twoPi * cutoffHz / sampleRate;
        double cosW0 = std::cos(w0);
        double alpha = std::sin(w0) / (2.0 * q);
        double norm  = 1.0 / (1.0 + alpha);
        b0 = (1.0 - cosW0) * 0.5 * norm;
        b1 = (1.0 - cosW0) * norm;
        b2 = b0;
        a1 = -2.0 * cosW0 * norm;
        a2 = (1.0 - alpha) * norm;
    }

    double filterSample(double in)
    {
        double out = b0*in + b1*x1 + b2*x2 - a1*y1 - a2*y2;
        if (!std::isfinite(out)) { out = 0.0; x1=x2=y1=y2=0.0; }
        x2=x1; x1=in; y2=y1; y1=out;
        return out;
    }

    void noteOn(int note, float vel, double freq, double atkInc, double decInc, double sus)
    {
        midiNote      = note;
        active        = true;
        frequency     = freq;
        velocity      = vel;
        carrierPhase  = 0.0;
        modulatorPhase= 0.0;
        envelope      = 0.0;
        adsrState     = ADSRState::Attack;
        attackInc     = atkInc;
        decayInc      = decInc;
        sustainLevel  = sus;
        x1=x2=y1=y2  = 0.0;
    }

    void noteOff() { if (adsrState != ADSRState::Idle) adsrState = ADSRState::Release; }

    double tickEnvelope()
    {
        switch (adsrState)
        {
            case ADSRState::Attack:
                envelope += attackInc;
                if (envelope >= 1.0) { envelope = 1.0; adsrState = ADSRState::Decay; }
                break;
            case ADSRState::Decay:
                envelope -= decayInc;
                if (decayInc <= 0.0 || envelope <= sustainLevel)
                    { envelope = sustainLevel; adsrState = ADSRState::Sustain; }
                break;
            case ADSRState::Sustain:
                break;
            case ADSRState::Release:
                envelope -= releaseInc;
                if (envelope <= 0.0)
                    { envelope = 0.0; adsrState = ADSRState::Idle; active = false; midiNote = -1; }
                break;
            default: break;
        }
        return envelope;
    }
};

//==============================================================================
class SimpleFilterAudioProcessor : public juce::AudioProcessor
{
public:
    SimpleFilterAudioProcessor();
    ~SimpleFilterAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int) override;
    const juce::String getProgramName(int) override;
    void changeProgramName(int, const juce::String&) override;

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;

    // Spectrum analyzer handoff (audio thread writes, UI timer reads)
    static constexpr int fftOrder = 11;
    static constexpr int fftSize  = 1 << fftOrder;  // 2048
    std::array<float, fftSize * 2> fftData { {} };
    std::atomic<bool> nextFFTBlockReady { false };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    static constexpr int numVoices = 8;
    std::array<FMVoice, numVoices> voices;
    double currentSampleRate = 44100.0;
    double lfoPhase = 0.0;

    std::array<float, fftSize> spectrumFifo { {} };
    int spectrumFifoIndex = 0;

    void startNote(int note, float velocity);
    void stopNote(int note);
    int  findFreeVoice() const;
    int  findVoiceWithNote(int note) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SimpleFilterAudioProcessor)
};
