#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
SimpleFilterAudioProcessor::SimpleFilterAudioProcessor()
    : AudioProcessor(BusesProperties()
                     .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout())
{
}

SimpleFilterAudioProcessor::~SimpleFilterAudioProcessor() {}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
SimpleFilterAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"ratio", 1}, "FM Ratio",
        juce::NormalisableRange<float>(0.5f, 16.0f, 0.01f, 0.5f), 2.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"index", 1}, "FM Index",
        juce::NormalisableRange<float>(0.0f, 10.0f, 0.01f), 1.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"attack", 1}, "Attack",
        juce::NormalisableRange<float>(0.001f, 3.0f, 0.001f, 0.3f), 0.01f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"decay", 1}, "Decay",
        juce::NormalisableRange<float>(0.001f, 3.0f, 0.001f, 0.3f), 0.15f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"sustain", 1}, "Sustain",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.7f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"release", 1}, "Release",
        juce::NormalisableRange<float>(0.001f, 5.0f, 0.001f, 0.3f), 0.4f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"cutoff", 1}, "Filter Cutoff",
        juce::NormalisableRange<float>(80.0f, 20000.0f, 1.0f, 0.25f), 8000.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"resonance", 1}, "Resonance",
        juce::NormalisableRange<float>(0.1f, 4.0f, 0.01f), 0.707f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"lfoRate", 1}, "LFO Rate",
        juce::NormalisableRange<float>(0.1f, 10.0f, 0.01f), 1.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"lfoDepth", 1}, "LFO Depth",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"gain", 1}, "Master Gain",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.7f));

    return layout;
}

//==============================================================================
const juce::String SimpleFilterAudioProcessor::getName() const { return JucePlugin_Name; }
bool SimpleFilterAudioProcessor::acceptsMidi()  const { return true;  }
bool SimpleFilterAudioProcessor::producesMidi() const { return false; }
bool SimpleFilterAudioProcessor::isMidiEffect() const { return false; }
double SimpleFilterAudioProcessor::getTailLengthSeconds() const { return 1.0; }
int SimpleFilterAudioProcessor::getNumPrograms()    { return 1; }
int SimpleFilterAudioProcessor::getCurrentProgram() { return 0; }
void SimpleFilterAudioProcessor::setCurrentProgram(int) {}
const juce::String SimpleFilterAudioProcessor::getProgramName(int) { return {}; }
void SimpleFilterAudioProcessor::changeProgramName(int, const juce::String&) {}

//==============================================================================
void SimpleFilterAudioProcessor::prepareToPlay(double sampleRate, int)
{
    currentSampleRate = sampleRate;
    lfoPhase          = 0.0;
    spectrumFifoIndex = 0;
    fftData.fill(0.0f);
    nextFFTBlockReady = false;
    for (auto& v : voices) v = FMVoice{};
}

void SimpleFilterAudioProcessor::releaseResources() {}

bool SimpleFilterAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    return true;
}

//==============================================================================
int SimpleFilterAudioProcessor::findFreeVoice() const
{
    for (int i = 0; i < numVoices; ++i)
        if (!voices[i].active) return i;
    return 0; // steal voice 0 if all busy
}

int SimpleFilterAudioProcessor::findVoiceWithNote(int note) const
{
    for (int i = 0; i < numVoices; ++i)
        if (voices[i].active && voices[i].midiNote == note) return i;
    return -1;
}

void SimpleFilterAudioProcessor::startNote(int note, float vel)
{
    const double sr      = currentSampleRate;
    const double freq    = 440.0 * std::pow(2.0, (note - 69) / 12.0);
    const float  attack  = apvts.getRawParameterValue("attack")->load();
    const float  decay   = apvts.getRawParameterValue("decay")->load();
    const float  sustain = apvts.getRawParameterValue("sustain")->load();
    const float  cutoff  = apvts.getRawParameterValue("cutoff")->load();
    const float  res     = apvts.getRawParameterValue("resonance")->load();

    const double atkInc = 1.0 / std::max(attack * sr, 1.0);
    const double decInc = (1.0 - (double)sustain) / std::max(decay * sr, 1.0);

    int idx = findFreeVoice();
    voices[idx].noteOn(note, vel, freq, atkInc, decInc, sustain);
    voices[idx].updateFilter(cutoff, res, sr);
}

void SimpleFilterAudioProcessor::stopNote(int note)
{
    int idx = findVoiceWithNote(note);
    if (idx < 0) return;

    const float  release = apvts.getRawParameterValue("release")->load();
    const double env     = voices[idx].envelope;
    voices[idx].releaseInc = (env > 0.0)
                           ? env / std::max(release * currentSampleRate, 1.0)
                           : 1.0 / (0.001 * currentSampleRate);
    voices[idx].noteOff();
}

//==============================================================================
void SimpleFilterAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                               juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numOutCh   = getTotalNumOutputChannels();
    for (int ch = 0; ch < numOutCh; ++ch)
        buffer.clear(ch, 0, numSamples);

    // Let keyboard component inject its events and update key state display
    keyboardState.processNextMidiBuffer(midiMessages, 0, numSamples, true);

    // Read parameters atomically (lock-free, thread-safe)
    const float ratio    = apvts.getRawParameterValue("ratio")->load();
    const float fmIndex  = apvts.getRawParameterValue("index")->load();
    const float cutoff   = apvts.getRawParameterValue("cutoff")->load();
    const float res      = apvts.getRawParameterValue("resonance")->load();
    const float lfoRate  = apvts.getRawParameterValue("lfoRate")->load();
    const float lfoDepth = apvts.getRawParameterValue("lfoDepth")->load();
    const float gain     = apvts.getRawParameterValue("gain")->load();

    const double twoPi  = juce::MathConstants<double>::twoPi;
    const double sr     = currentSampleRate;
    const double lfoInc = twoPi * lfoRate / sr;

    // MIDI event handling (block-boundary for simplicity)
    for (const auto metadata : midiMessages)
    {
        const auto& msg = metadata.getMessage();
        if      (msg.isNoteOn())                         startNote(msg.getNoteNumber(), msg.getFloatVelocity());
        else if (msg.isNoteOff())                        stopNote(msg.getNoteNumber());
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            for (auto& v : voices) v.noteOff();
    }
    midiMessages.clear();

    // Update filter coefficients for all active voices
    for (auto& v : voices)
        if (v.active) v.updateFilter(cutoff, res, sr);

    auto* left  = buffer.getWritePointer(0);
    auto* right = (numOutCh > 1) ? buffer.getWritePointer(1) : nullptr;

    for (int s = 0; s < numSamples; ++s)
    {
        // LFO: sine wave for pitch vibrato
        const double lfoVal = std::sin(lfoPhase);
        lfoPhase += lfoInc;
        if (lfoPhase >= twoPi) lfoPhase -= twoPi;

        double mix = 0.0;

        for (auto& v : voices)
        {
            if (!v.active) continue;

            // LFO pitch modulation (max ±1 semitone at full depth)
            const double carrierFreq = v.frequency * (1.0 + lfoDepth * 0.0595 * lfoVal);
            const double modFreq     = carrierFreq * ratio;

            // Advance oscillator phases
            v.modulatorPhase += twoPi * modFreq    / sr;
            if (v.modulatorPhase >= twoPi) v.modulatorPhase -= twoPi;

            v.carrierPhase   += twoPi * carrierFreq / sr;
            if (v.carrierPhase   >= twoPi) v.carrierPhase   -= twoPi;

            // FM synthesis: carrier angle modulated by modulator output
            const double fm       = std::sin(v.carrierPhase + (double)fmIndex * std::sin(v.modulatorPhase));
            const double env      = v.tickEnvelope();
            const double filtered = v.filterSample(fm * env * v.velocity);

            mix += filtered;
        }

        // Master gain, scale by voice count, hard clip for safety
        const float out = static_cast<float>(juce::jlimit(-1.0, 1.0, mix * gain / numVoices));
        left[s] = out;
        if (right != nullptr) right[s] = out;

        // Push to spectrum FIFO (visualizer handoff)
        spectrumFifo[spectrumFifoIndex++] = out;
        if (spectrumFifoIndex >= fftSize)
        {
            if (!nextFFTBlockReady.load())
            {
                std::copy(spectrumFifo.begin(), spectrumFifo.end(), fftData.begin());
                nextFFTBlockReady.store(true);
            }
            spectrumFifoIndex = 0;
        }
    }
}

//==============================================================================
bool SimpleFilterAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* SimpleFilterAudioProcessor::createEditor()
{
    return new SimpleFilterAudioProcessorEditor(*this);
}

void SimpleFilterAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void SimpleFilterAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SimpleFilterAudioProcessor();
}
