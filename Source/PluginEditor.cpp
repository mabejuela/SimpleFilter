#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// VintageLookAndFeel
//==============================================================================

VintageLookAndFeel::VintageLookAndFeel()
{
    // Amber/gold palette
    setColour(juce::Slider::rotarySliderFillColourId,    juce::Colour(0xFFD4841A));
    setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xFF3D2200));
    setColour(juce::Slider::thumbColourId,               juce::Colour(0xFFFFD080));
    setColour(juce::Label::textColourId,                 juce::Colour(0xFFD4A855));
    setColour(juce::Label::backgroundColourId,           juce::Colours::transparentBlack);
}

void VintageLookAndFeel::drawRotarySlider(juce::Graphics& g,
    int x, int y, int width, int height,
    float sliderPos, float rotaryStartAngle, float rotaryEndAngle, juce::Slider&)
{
    const float radius = (float)juce::jmin(width, height) * 0.5f - 3.0f;
    const float cx = (float)x + (float)width  * 0.5f;
    const float cy = (float)y + (float)height * 0.5f;

    // Outer bezel ring
    g.setColour(juce::Colour(0xFF1A0F00));
    g.fillEllipse(cx - radius - 4, cy - radius - 4, (radius + 4) * 2, (radius + 4) * 2);

    // Knob body with warm radial gradient
    juce::ColourGradient body(
        juce::Colour(0xFF6B3E10), cx - radius * 0.35f, cy - radius * 0.35f,
        juce::Colour(0xFF1E1000), cx + radius * 0.35f, cy + radius * 0.35f, false);
    g.setGradientFill(body);
    g.fillEllipse(cx - radius, cy - radius, radius * 2, radius * 2);

    // Subtle rim highlight
    g.setColour(juce::Colour(0xFFD4841A).withAlpha(0.3f));
    g.drawEllipse(cx - radius, cy - radius, radius * 2, radius * 2, 1.5f);

    // Arc track (background)
    const float trackRadius = radius + 5.5f;
    juce::Path track;
    track.addCentredArc(cx, cy, trackRadius, trackRadius, 0.0f,
                        rotaryStartAngle, rotaryEndAngle, true);
    g.setColour(juce::Colour(0xFF3D2200));
    g.strokePath(track, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));

    // Arc fill (value)
    const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    juce::Path fill;
    fill.addCentredArc(cx, cy, trackRadius, trackRadius, 0.0f,
                       rotaryStartAngle, angle, true);
    g.setColour(juce::Colour(0xFFD4841A));
    g.strokePath(fill, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));

    // Pointer
    const float pointerLen = radius * 0.55f;
    const juce::Point<float> tip(cx + std::sin(angle) * pointerLen,
                                  cy - std::cos(angle) * pointerLen);
    g.setColour(juce::Colour(0xFFFFD080));
    g.drawLine(juce::Line<float>(cx, cy, tip.x, tip.y), 2.0f);
    g.fillEllipse(cx - 2.5f, cy - 2.5f, 5.0f, 5.0f);
}

//==============================================================================
// SpectrumAnalyzer
//==============================================================================

SpectrumAnalyzer::SpectrumAnalyzer(SimpleFilterAudioProcessor& p)
    : processor(p),
      fft(SimpleFilterAudioProcessor::fftOrder),
      window(SimpleFilterAudioProcessor::fftSize,
             juce::dsp::WindowingFunction<float>::hann)
{
    displayData.fill(0.0f);
    startTimerHz(30);
}

void SpectrumAnalyzer::timerCallback()
{
    if (!processor.nextFFTBlockReady.load()) return;

    fftWorkBuffer.fill(0.0f);
    std::copy(processor.fftData.begin(),
              processor.fftData.begin() + fftSize,
              fftWorkBuffer.begin());
    processor.nextFFTBlockReady.store(false);

    window.multiplyWithWindowingTable(fftWorkBuffer.data(), fftSize);
    fft.performFrequencyOnlyForwardTransform(fftWorkBuffer.data());

    // Smooth into displayData
    for (int i = 0; i < fftSize / 2; ++i)
        displayData[i] = displayData[i] * 0.82f + (fftWorkBuffer[i] / (float)fftSize) * 0.18f;

    repaint();
}

void SpectrumAnalyzer::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const float w = bounds.getWidth();
    const float h = bounds.getHeight();

    // Background
    g.setColour(juce::Colour(0xFF0D0804));
    g.fillRoundedRectangle(bounds, 6.0f);

    // Horizontal grid lines
    g.setColour(juce::Colour(0xFF221500));
    for (int i = 1; i < 4; ++i)
        g.drawHorizontalLine((int)(bounds.getY() + h * i / 4.0f),
                             bounds.getX() + 1, bounds.getRight() - 1);

    // Frequency labels
    g.setFont(juce::FontOptions(8.0f));
    g.setColour(juce::Colour(0xFF4A3010));
    const std::pair<float,const char*> freqMarkers[] = {
        {100,"100"},{500,"500"},{1000,"1k"},{5000,"5k"},{10000,"10k"}
    };
    for (auto& [freq, label] : freqMarkers)
    {
        // Map frequency to x position (log scale, 20Hz–20kHz)
        float normX = std::log2(freq / 20.0f) / std::log2(20000.0f / 20.0f);
        float fx    = bounds.getX() + normX * w;
        g.drawText(label, (int)fx - 10, (int)(bounds.getBottom() - 14), 20, 12,
                   juce::Justification::centred);
    }

    // Spectrum curve
    const int numBins = fftSize / 2;
    juce::Path curve;
    bool started = false;

    for (int i = 2; i < numBins; ++i)
    {
        // Log-frequency x mapping (roughly 20 Hz–20 kHz)
        const float binFreq = (float)i * 44100.0f / (float)fftSize;
        if (binFreq < 20.0f || binFreq > 20000.0f) continue;

        const float normX = std::log2(binFreq / 20.0f) / std::log2(20000.0f / 20.0f);
        const float fx    = bounds.getX() + normX * w;

        // dB scale y mapping (−80 dB to 0 dB)
        const float dB    = juce::Decibels::gainToDecibels(displayData[i], -80.0f);
        const float normY = juce::jlimit(0.0f, 1.0f, (dB + 80.0f) / 80.0f);
        const float fy    = bounds.getBottom() - normY * (h - 14.0f);

        if (!started)
        {
            curve.startNewSubPath(fx, bounds.getBottom());
            curve.lineTo(fx, fy);
            started = true;
        }
        else curve.lineTo(fx, fy);
    }

    if (started)
    {
        curve.lineTo(bounds.getRight(), bounds.getBottom());
        curve.closeSubPath();

        // Gradient fill under curve
        juce::ColourGradient fill(juce::Colour(0xCCD4841A), 0, bounds.getY(),
                                   juce::Colour(0x22D4841A), 0, bounds.getBottom(), false);
        g.setGradientFill(fill);
        g.fillPath(curve);

        // Curve stroke
        g.setColour(juce::Colour(0xFFFFB84D));
        juce::Path stroke = curve;
        g.strokePath(stroke, juce::PathStrokeType(1.5f));
    }

    // Border + label
    g.setColour(juce::Colour(0xFF4A3000));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);
    g.setFont(juce::FontOptions(9.0f));
    g.setColour(juce::Colour(0xFF6B4A18));
    g.drawText("SPECTRUM ANALYZER",
               juce::Rectangle<float>(bounds.getX(), bounds.getY(), bounds.getWidth(), 13.0f).toNearestInt(),
               juce::Justification::centred);
}

//==============================================================================
// Editor
//==============================================================================

SimpleFilterAudioProcessorEditor::SimpleFilterAudioProcessorEditor(SimpleFilterAudioProcessor& p)
    : AudioProcessorEditor(&p),
      audioProcessor(p),
      spectrumView(p),
      keyboard(p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard),
      ratioAtt    (p.apvts, "ratio",     ratioKnob),
      indexAtt    (p.apvts, "index",     indexKnob),
      attackAtt   (p.apvts, "attack",    attackKnob),
      decayAtt    (p.apvts, "decay",     decayKnob),
      sustainAtt  (p.apvts, "sustain",   sustainKnob),
      releaseAtt  (p.apvts, "release",   releaseKnob),
      cutoffAtt   (p.apvts, "cutoff",    cutoffKnob),
      resonanceAtt(p.apvts, "resonance", resonanceKnob),
      lfoRateAtt  (p.apvts, "lfoRate",   lfoRateKnob),
      lfoDepthAtt (p.apvts, "lfoDepth",  lfoDepthKnob),
      gainAtt     (p.apvts, "gain",      gainKnob)
{
    setLookAndFeel(&laf);

    setupKnob(ratioKnob,      ratioLabel,      "RATIO");
    setupKnob(indexKnob,      indexLabel,      "INDEX");
    setupKnob(attackKnob,     attackLabel,     "ATTACK");
    setupKnob(decayKnob,      decayLabel,      "DECAY");
    setupKnob(sustainKnob,    sustainLabel,    "SUSTAIN");
    setupKnob(releaseKnob,    releaseLabel,    "RELEASE");
    setupKnob(cutoffKnob,     cutoffLabel,     "CUTOFF");
    setupKnob(resonanceKnob,  resonanceLabel,  "RESO");
    setupKnob(lfoRateKnob,    lfoRateLabel,    "RATE");
    setupKnob(lfoDepthKnob,   lfoDepthLabel,   "DEPTH");
    setupKnob(gainKnob,       gainLabel,       "GAIN");

    setupHeader(fmHeader,     "FM OPERATOR");
    setupHeader(adsrHeader,   "ENVELOPE");
    setupHeader(filterHeader, "FILTER");
    setupHeader(lfoHeader,    "LFO");
    setupHeader(masterHeader, "MASTER");

    // Keyboard styling
    keyboard.setLowestVisibleKey(36);   // C2
    keyboard.setScrollButtonsVisible(false);
    keyboard.setColour(juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour(0xFFE8D5A0));
    keyboard.setColour(juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour(0xFF1A0F00));
    keyboard.setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, juce::Colour(0xFFD4841A));
    keyboard.setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, juce::Colour(0x66D4841A));
    addAndMakeVisible(keyboard);
    addAndMakeVisible(spectrumView);

    setSize(600, 540);
}

SimpleFilterAudioProcessorEditor::~SimpleFilterAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

void SimpleFilterAudioProcessorEditor::setupKnob(juce::Slider& knob, juce::Label& label,
                                                   const juce::String& text)
{
    knob.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    knob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    knob.setPopupDisplayEnabled(true, false, this);
    addAndMakeVisible(knob);

    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setFont(juce::FontOptions(10.0f));
    addAndMakeVisible(label);
}

void SimpleFilterAudioProcessorEditor::setupHeader(juce::Label& label, const juce::String& text)
{
    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setFont(juce::FontOptions(9.5f, juce::Font::bold));
    label.setColour(juce::Label::textColourId, juce::Colour(0xFF8A5C20));
    addAndMakeVisible(label);
}

void SimpleFilterAudioProcessorEditor::paint(juce::Graphics& g)
{
    // Dark warm background gradient
    juce::ColourGradient bg(juce::Colour(0xFF1C1208), 0.0f, 0.0f,
                             juce::Colour(0xFF0E0804), 0.0f, (float)getHeight(), false);
    g.setGradientFill(bg);
    g.fillAll();

    auto drawPanel = [&](float px, float py, float pw, float ph) {
        g.setColour(juce::Colour(0xFF1E1200));
        g.fillRoundedRectangle(px, py, pw, ph, 6.0f);
        g.setColour(juce::Colour(0xFF4A3000));
        g.drawRoundedRectangle(px + 0.5f, py + 0.5f, pw - 1.0f, ph - 1.0f, 6.0f, 1.0f);
    };

    // Row 1 panels
    drawPanel(  8.0f, 142.0f, 128.0f, 140.0f);  // FM
    drawPanel(144.0f, 142.0f, 270.0f, 140.0f);  // ADSR
    drawPanel(422.0f, 142.0f, 170.0f, 140.0f);  // Filter

    // Row 2 panels
    drawPanel(  8.0f, 290.0f, 128.0f, 130.0f);  // LFO
    drawPanel(144.0f, 290.0f,  88.0f, 130.0f);  // Master

    // Plugin title
    g.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    g.setColour(juce::Colour(0xFFD4841A));
    g.drawFittedText("FM SYNTH", { 0, 6, getWidth(), 28 }, juce::Justification::centred, 1);

    g.setFont(juce::FontOptions(10.0f));
    g.setColour(juce::Colour(0xFF6B4A18));
    g.drawFittedText("8-voice polyphonic  \xb7  FM synthesis  \xb7  resonant filter  \xb7  LFO vibrato",
                     { 0, 30, getWidth(), 14 }, juce::Justification::centred, 1);
}

void SimpleFilterAudioProcessorEditor::resized()
{
    spectrumView.setBounds(8, 48, 584, 88);

    constexpr int knobSz = 52;

    // ---- Row 1 (panels at y=142) ----
    constexpr int kY1 = 168;  // knob top
    constexpr int lY1 = kY1 + knobSz + 2;
    constexpr int hY1 = 148;  // header top

    // FM (panel x=8, w=128 → 2 knobs)
    fmHeader.setBounds(8, hY1, 128, 14);
    ratioKnob.setBounds( 14, kY1, knobSz, knobSz);
    ratioLabel.setBounds(14, lY1, knobSz, 13);
    indexKnob.setBounds( 78, kY1, knobSz, knobSz);
    indexLabel.setBounds(78, lY1, knobSz, 13);

    // ADSR (panel x=144, w=270 → 4 knobs)
    adsrHeader.setBounds(144, hY1, 270, 14);
    attackKnob.setBounds(  152, kY1, knobSz, knobSz);
    attackLabel.setBounds( 152, lY1, knobSz, 13);
    decayKnob.setBounds(   219, kY1, knobSz, knobSz);
    decayLabel.setBounds(  219, lY1, knobSz, 13);
    sustainKnob.setBounds( 287, kY1, knobSz, knobSz);
    sustainLabel.setBounds(287, lY1, knobSz, 13);
    releaseKnob.setBounds( 354, kY1, knobSz, knobSz);
    releaseLabel.setBounds(354, lY1, knobSz, 13);

    // Filter (panel x=422, w=170 → 2 knobs)
    filterHeader.setBounds(422, hY1, 170, 14);
    cutoffKnob.setBounds(    436, kY1, knobSz, knobSz);
    cutoffLabel.setBounds(   436, lY1, knobSz, 13);
    resonanceKnob.setBounds( 522, kY1, knobSz, knobSz);
    resonanceLabel.setBounds(522, lY1, knobSz, 13);

    // ---- Row 2 (panels at y=290) ----
    constexpr int kY2 = 316;
    constexpr int lY2 = kY2 + knobSz + 2;
    constexpr int hY2 = 296;

    // LFO (panel x=8, w=128 → 2 knobs)
    lfoHeader.setBounds(8, hY2, 128, 14);
    lfoRateKnob.setBounds(  14, kY2, knobSz, knobSz);
    lfoRateLabel.setBounds( 14, lY2, knobSz, 13);
    lfoDepthKnob.setBounds( 78, kY2, knobSz, knobSz);
    lfoDepthLabel.setBounds(78, lY2, knobSz, 13);

    // Master (panel x=144, w=88 → 1 knob)
    masterHeader.setBounds(144, hY2, 88, 14);
    gainKnob.setBounds(    162, kY2, knobSz, knobSz);
    gainLabel.setBounds(   162, lY2, knobSz, 13);

    // MIDI keyboard at bottom
    keyboard.setBounds(0, 432, getWidth(), 108);
}
