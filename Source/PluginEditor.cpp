#include "PluginEditor.h"

using namespace amb;
namespace col = ui::colours;

namespace ui
{
//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& state, const char* paramId, const juce::String& text, const juce::String& tooltip)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 18);
    slider.setTextBoxIsEditable (true);
    slider.setVelocityModeParameters (1.0, 1, 0.0, false);
    slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    slider.setTooltip (tooltip);
    slider.setScrollWheelEnabled (true);
    addAndMakeVisible (slider);

    caption.setText (text.toUpperCase(), juce::dontSendNotification);
    caption.setJustificationType (juce::Justification::centred);
    caption.setFont (AmbienceLookAndFeel::font (10.5f, true));
    caption.setColour (juce::Label::textColourId, colours::textDim);
    caption.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (caption);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);
    // Double-click returns to the parameter default.
    if (auto* p = state.getParameter (paramId))
        slider.setDoubleClickReturnValue (true, (double) p->convertFrom0to1 (p->getDefaultValue()));
}

void Knob::resized()
{
    auto b = getLocalBounds();
    caption.setBounds (b.removeFromTop (14));
    slider.setBounds (b);
}

//==============================================================================
Segmented::Segmented (juce::AudioProcessorValueTreeState& state, const char* paramId, const juce::StringArray& labels, const juce::String& tooltip)
    : attachment (*state.getParameter (paramId),
                  [this] (float v)
                  {
                      for (int i = 0; i < buttons.size(); ++i)
                          buttons[i]->setToggleState (i == juce::roundToInt (v), juce::dontSendNotification);
                  },
                  state.undoManager)
{
    for (int i = 0; i < labels.size(); ++i)
    {
        auto* b = buttons.add (new juce::TextButton (labels[i]));
        b->setClickingTogglesState (false);
        b->setTooltip (tooltip);
        b->setConnectedEdges ((i > 0 ? juce::Button::ConnectedOnLeft : 0) | (i < labels.size() - 1 ? juce::Button::ConnectedOnRight : 0));
        b->onClick = [this, i] { attachment.setValueAsCompleteGesture ((float) i); };
        addAndMakeVisible (b);
    }
    attachment.sendInitialUpdate();
}

void Segmented::resized()
{
    auto b = getLocalBounds();
    const int w = b.getWidth() / std::max (1, buttons.size());
    for (int i = 0; i < buttons.size(); ++i)
        buttons[i]->setBounds (i == buttons.size() - 1 ? b : b.removeFromLeft (w));
}

void Segmented::setEnabledAll (bool e)
{
    for (auto* b : buttons)
        b->setEnabled (e);
}

void IconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto b = getLocalBounds().toFloat().reduced (0.5f);
    const bool on = getToggleState();
    g.setColour (highlighted || down ? colours::surfaceRaised.brighter (0.08f) : colours::surfaceRaised);
    g.fillRoundedRectangle (b, 5.0f);
    g.setColour (icon == Icon::power && on ? colours::accent : colours::line);
    g.drawRoundedRectangle (b, 5.0f, 1.0f);

    const auto c = b.getCentre();
    const float r = std::min (b.getWidth(), b.getHeight()) * 0.26f;
    if (icon == Icon::power)
    {
        const auto colour = on ? colours::accent : colours::textFaint;
        g.setColour (colour);
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, r, r, 0, juce::degreesToRadians (35.0f), juce::degreesToRadians (325.0f), true);
        g.strokePath (arc, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.drawLine (c.x, c.y - r * 1.25f, c.x, c.y - r * 0.15f, 1.8f);
    }
    else
    {
        g.setColour (highlighted ? colours::text : colours::textDim);
        juce::Path gear;
        const int teeth = 8;
        for (int i = 0; i < teeth * 2; ++i)
        {
            const float a0 = juce::MathConstants<float>::twoPi * (float) i / (teeth * 2);
            const float a1 = juce::MathConstants<float>::twoPi * (float) (i + 1) / (teeth * 2);
            const float rr = (i % 2 == 0) ? r * 1.25f : r * 0.95f;
            const juce::Point<float> p0 (c.x + rr * std::sin (a0), c.y - rr * std::cos (a0));
            const juce::Point<float> p1 (c.x + rr * std::sin (a1), c.y - rr * std::cos (a1));
            if (i == 0)
                gear.startNewSubPath (p0);
            else
                gear.lineTo (p0);
            gear.lineTo (p1);
        }
        gear.closeSubPath();
        gear.addEllipse (c.x - r * 0.45f, c.y - r * 0.45f, r * 0.9f, r * 0.9f);
        gear.setUsingNonZeroWinding (false);
        g.fillPath (gear);
    }
}

void Card::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (colours::surface);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (colours::line);
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
    g.setColour (colours::textDim);
    g.setFont (AmbienceLookAndFeel::font (10.5f, true));
    g.drawText (title, getLocalBounds().reduced (12, 8).removeFromTop (12), juce::Justification::topLeft);
}
} // namespace ui

//==============================================================================
class AmbienceEditor::AdvancedPanel : public juce::Component
{
public:
    AdvancedPanel (AmbienceProcessor& p)
        : proc (p),
          recKnob (p.apvts, pid::recDistance, "Recorded", "Distance at which the material was recorded. The plugin moves the source from here to Distance."),
          rolloffKnob (p.apvts, pid::rolloff, "Rolloff", "Level drop per doubling of distance. 6 dB = point source (inverse square law), 3 dB = line source (traffic)."),
          timePanKnob (p.apvts, pid::timePan, "Time Pan", "Share of inter-channel time difference in the Speakers panner (time = phase shift growing with frequency)."),
          tempKnob (p.apvts, pid::temperature, "Temperature", "Air temperature for ISO 9613-1 absorption."),
          humidityKnob (p.apvts, pid::humidity, "Humidity", "Relative humidity for ISO 9613-1 absorption."),
          decayKnob (p.apvts, pid::decay, "Decay", "Scales the room reverberation time (T60)."),
          proximityKnob (p.apvts, pid::proximity, "Proximity", "Microphone / listener distance to the walls. 0 % = middle of the room (reflections late and even), "
                         "100 % = near a wall or corner (strong reflections right after the direct sound). 50 % = default position."),
          thicknessKnob (p.apvts, pid::thickness, "Thickness", "Scales the partition mass (and its coincidence frequency)."),
          leakKnob (p.apvts, pid::leak, "Leak", "Gaps and cracks (open area 0-10 %, logarithmic). Let the brighter part of the sound through, e.g. the gap under a door."),
          agRangeKnob (p.apvts, pid::autoGainRange, "AG Range", "Maximum auto gain compensation.")
    {
        for (auto* k : { &recKnob, &rolloffKnob, &timePanKnob, &tempKnob, &humidityKnob, &decayKnob, &proximityKnob, &thicknessKnob, &leakKnob, &agRangeKnob })
            addAndMakeVisible (k);

        auto combo = [this, &p] (juce::ComboBox& box, const char* id, std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>& att, const juce::String& tip)
        {
            for (auto& s : paramSpecs())
                if (juce::String (s.id) == id)
                    for (int i = 0; i < (int) s.choices.size(); ++i)
                        box.addItem (s.choices[(size_t) i], i + 1);
            box.setTooltip (tip);
            addAndMakeVisible (box);
            att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, id, box);
        };
        combo (panLawBox, pid::panLaw, panLawAtt, "Pan law: centre attenuation of a panned mono source.");
        combo (micBox, pid::micPattern, micAtt, "Microphone used for the recording: directional mics add proximity bass at close range, removed when the source is moved away.");
        combo (bassMonoBox, pid::bassMono, bassMonoAtt, "Keeps the stereo side signal mono below this frequency.");

        for (auto* t : { &airToggle, &propToggle })
            addAndMakeVisible (t);
        airToggle.setTooltip ("Frequency-dependent air absorption (ISO 9613-1).");
        propToggle.setTooltip ("Delays the sound by distance / speed of sound (Doppler on moves). Off by default to keep sync with picture.");
        airAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, pid::airAbsorption, airToggle);
        propAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, pid::propDelay, propToggle);

        closeBtn.onClick = [this] { if (onClose) onClose(); };
        addAndMakeVisible (closeBtn);
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (col::surface);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (col::lineStrong);
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
        g.setColour (col::text);
        g.setFont (ui::AmbienceLookAndFeel::font (13.0f, true));
        g.drawText ("ADVANCED", getLocalBounds().reduced (16, 12).removeFromTop (16), juce::Justification::topLeft);

        g.setFont (ui::AmbienceLookAndFeel::font (10.5f, true));
        g.setColour (col::textDim);
        for (auto& [label, rect] : sectionLabels)
            g.drawText (label, rect, juce::Justification::bottomLeft);
        for (auto& [label, comp] : comboLabels)
            g.drawText (label, comp->getBounds().translated (0, -16).withHeight (14), juce::Justification::bottomLeft);
    }

    void resized() override
    {
        sectionLabels.clear();
        comboLabels.clear();
        auto b = getLocalBounds().reduced (16, 12);
        closeBtn.setBounds (b.removeFromTop (22).removeFromRight (70));
        b.removeFromTop (6);
        const int kw = 96, kh = 92;

        auto section = [&] (const juce::String& name, std::initializer_list<juce::Component*> knobs)
        {
            sectionLabels.push_back ({ name, b.removeFromTop (18) });
            auto row = b.removeFromTop (kh);
            for (auto* k : knobs)
                k->setBounds (row.removeFromLeft (kw).reduced (4, 0));
            b.removeFromTop (8);
            return row;
        };

        auto r1 = section ("DISTANCE", { &recKnob, &rolloffKnob, &tempKnob, &humidityKnob });
        {
            r1.removeFromLeft (8);
            micBox.setBounds (r1.removeFromTop (58).removeFromBottom (26).removeFromLeft (150));
            comboLabels.push_back ({ "MIC PATTERN", &micBox });
            airToggle.setBounds (r1.removeFromTop (26).removeFromLeft (150));
        }
        auto r2 = section ("PANNING & WIDTH", { &timePanKnob });
        {
            r2.removeFromLeft (8);
            auto c1 = r2.removeFromLeft (130);
            panLawBox.setBounds (c1.removeFromTop (58).removeFromBottom (26).reduced (0, 0).withWidth (120));
            comboLabels.push_back ({ "PAN LAW", &panLawBox });
            auto c2 = r2.removeFromLeft (130);
            bassMonoBox.setBounds (c2.removeFromTop (58).removeFromBottom (26).withWidth (120));
            comboLabels.push_back ({ "BASS MONO", &bassMonoBox });
            propToggle.setBounds (r2.removeFromTop (58).removeFromBottom (26).withWidth (170));
        }
        section ("ROOM, OBSTACLE & GAIN", { &decayKnob, &proximityKnob, &thicknessKnob, &leakKnob, &agRangeKnob });
    }

    void setMonoOutput (bool mono)
    {
        timePanKnob.setEnabled (! mono);
        panLawBox.setEnabled (! mono);
        bassMonoBox.setEnabled (! mono);
    }

    std::function<void()> onClose;

private:
    AmbienceProcessor& proc;
    ui::Knob recKnob, rolloffKnob, timePanKnob, tempKnob, humidityKnob, decayKnob, proximityKnob, thicknessKnob, leakKnob, agRangeKnob;
    juce::ComboBox panLawBox, micBox, bassMonoBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> panLawAtt, micAtt, bassMonoAtt;
    juce::ToggleButton airToggle { "Air Absorption" }, propToggle { "Propagation Delay" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> airAtt, propAtt;
    juce::TextButton closeBtn { "Close" };
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> sectionLabels;
    std::vector<std::pair<juce::String, juce::Component*>> comboLabels;
};

//==============================================================================
AmbienceEditor::AmbienceEditor (AmbienceProcessor& p)
    : AudioProcessorEditor (&p),
      proc (p),
      inputSeg (p.apvts, pid::inputMode, { "Auto", "Mono", "Stereo" },
                "Input: Auto follows the track; Mono sums L+R to a point source (dialogue, foley); Stereo keeps the stereo image (ambiences, music)."),
      renderSeg (p.apvts, pid::renderMode, { "Speakers", "Headphones" },
                 "Speakers: hybrid level + time panning for loudspeakers. Headphones: binaural spherical-head model (not mono-compatible)."),
      stage (p.apvts),
      distanceKnob (p.apvts, pid::distance, "Distance", "Source distance. Level, air absorption, room and obstacle cues follow it."),
      panKnob (p.apvts, pid::pan, "Pan", "Direction left-right with level and time/phase differences."),
      widthKnob (p.apvts, pid::width, "Width", "0 % mono, 100 % unchanged, 200 % wide. Mono-compatible (the L+R sum does not change)."),
      roomLevelKnob (p.apvts, pid::roomLevel, "Level", "Room level relative to the physical direct-to-reverberant ratio."),
      amountKnob (p.apvts, pid::amount, "Amount", "0 % = no obstacle, 100 % = full obstacle. Automate it to open a door."),
      sizeKnob (p.apvts, pid::obstacleSize, "Size", "Detour around the obstacle (path-length difference, Maekawa)."),
      outputKnob (p.apvts, pid::output, "Output", "Output gain."),
      meters (p),
      powerAttachment (*p.apvts.getParameter ("bypass"),
                       [this] (float bypassed)
                       {
                           powerBtn.setToggleState (bypassed < 0.5f, juce::dontSendNotification);
                           repaint();
                       },
                       p.apvts.undoManager)
{
    const int initialScale = juce::jlimit (50, 200, proc.uiScalePercent.load());
    setLookAndFeel (&lnf);
    content.setLookAndFeel (&lnf);
    addAndMakeVisible (content);
    content.addMouseListener (this, true);

    // Header
    for (auto* c : std::initializer_list<juce::Component*> { &presetBox, &prevBtn, &nextBtn, &saveBtn, &menuBtn, &undoBtn, &redoBtn, &abA, &abB, &scaleBtn,
                                                             &powerBtn, &settingsBtn })
        content.addAndMakeVisible (c);
    powerBtn.setTooltip ("On / Off (bypass). Off passes the signal through unchanged.");
    powerBtn.onClick = [this] { powerAttachment.setValueAsCompleteGesture (powerBtn.getToggleState() ? 1.0f : 0.0f); };
    powerAttachment.sendInitialUpdate();
    settingsBtn.setTooltip ("Settings: check for updates, about");
    settingsBtn.onClick = [this] { showSettingsMenu(); };
    presetBox.setTextWhenNothingSelected ("Init (Neutral)");
    presetBox.setTooltip ("Presets");
    presetBox.onChange = [this]
    {
        const int id = presetBox.getSelectedId();
        if (id >= 1 && id < 1000)
            proc.presets.loadFactory (id - 1);
        else if (id >= 1000)
            proc.presets.loadUser (presetBox.getItemText (presetBox.indexOfItemId (id)));
    };
    prevBtn.onClick = [this] { proc.presets.loadNext (-1); };
    nextBtn.onClick = [this] { proc.presets.loadNext (1); };
    prevBtn.setTooltip ("Previous preset");
    nextBtn.setTooltip ("Next preset");
    saveBtn.onClick = [this] { savePresetDialog(); };
    saveBtn.setTooltip ("Save as user preset");
    menuBtn.onClick = [this] { showPresetMenu(); };
    undoBtn.onClick = [this] { proc.undoManager.undo(); };
    redoBtn.onClick = [this] { proc.undoManager.redo(); };
    undoBtn.setTooltip ("Undo (Ctrl/Cmd+Z)");
    redoBtn.setTooltip ("Redo (Ctrl/Cmd+Shift+Z)");
    abA.setConnectedEdges (juce::Button::ConnectedOnRight);
    abB.setConnectedEdges (juce::Button::ConnectedOnLeft);
    abA.setToggleState (true, juce::dontSendNotification);
    abA.onClick = [this] { switchAB (0); };
    abB.onClick = [this] { switchAB (1); };
    abA.setTooltip ("A/B comparison: two independent settings");
    abB.setTooltip ("A/B comparison: two independent settings");
    scaleBtn.onClick = [this] { showScaleMenu(); };
    scaleBtn.setTooltip ("Interface size");

    // Toolbar
    content.addAndMakeVisible (inputSeg);
    content.addAndMakeVisible (renderSeg);
    content.addAndMakeVisible (monoSafeToggle);
    content.addAndMakeVisible (autoGainToggle);
    content.addAndMakeVisible (monoDepthToggle);
    monoDepthToggle.setTooltip ("Mono Depth: mono output (L = R) for mono dialogue. Only depth cues act: distance, air, room (true mono reverb), obstacle. Pan, width and time differences are off.");
    monoDepthAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, pid::monoDepth, monoDepthToggle);
    content.addAndMakeVisible (advancedBtn);
    content.addAndMakeVisible (statusLabel);
    monoSafeToggle.setTooltip ("Limits and band-limits time panning so the mono fold-down stays within 3 dB per third-octave.");
    autoGainToggle.setTooltip ("Keeps loudness (ITU-R BS.1770 K-weighting) constant so only tone and space change. Model-based: no pumping.");
    monoSafeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, pid::monoSafe, monoSafeToggle);
    autoGainAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, pid::autoGain, autoGainToggle);
    advancedBtn.setClickingTogglesState (true);
    advancedBtn.onClick = [this] { advanced->setVisible (advancedBtn.getToggleState()); };
    statusLabel.setFont (ui::AmbienceLookAndFeel::font (11.5f, true));
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    statusLabel.setInterceptsMouseClicks (false, false);

    // Main
    content.addAndMakeVisible (stage);
    for (auto* c : { &positionCard, &roomCard, &obstacleCard, &outputCard })
        content.addAndMakeVisible (c);
    for (auto* k : { &distanceKnob, &panKnob, &widthKnob, &roomLevelKnob, &amountKnob, &sizeKnob, &outputKnob })
        content.addAndMakeVisible (k);
    content.addAndMakeVisible (meters);
    content.addAndMakeVisible (eqToggle);
    eqToggle.setTooltip ("Equalized Ambience (on by default): automatic EQ from distance and position — air absorption (ISO 9613-1), "
                         "removal of close-mic proximity bass and a gentle high-frequency shading on the side away from the source. "
                         "Off: only level, timing and room change; the tone stays as recorded.");
    eqAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, pid::eqAmbience, eqToggle);
    content.addAndMakeVisible (monoToStereoToggle);
    monoToStereoToggle.setTooltip ("Mono to Stereo: a mono signal becomes a natural stereo image - a mono input, Input = Mono, or mono "
                                   "content on a stereo track (identical, panned, slightly different or in one channel only; it is centred "
                                   "first). Mono-compatible: L + R stays the original sound, both channels keep the same tone and level, bass "
                                   "stays centred. The amount follows Width. True stereo input is not changed. Needs a stereo output: on a "
                                   "mono track (1 in, 1 out) use a stereo track, a stereo FX / aux channel or the Mono->Stereo version (Logic).");
    monoToStereoAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, pid::monoToStereo, monoToStereoToggle);
    content.addAndMakeVisible (keepMonoToggle);
    keepMonoToggle.setTooltip ("Keep Mono (with Mono to Stereo, on by default): the voice / direct sound stays mono in the centre and only "
                               "the space in the recording becomes stereo - its reverberation after each word and the room tone. "
                               "Off: the whole signal is widened. Zero latency; the mono sum is unchanged either way.");
    keepMonoAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, pid::keepMono, keepMonoToggle);

    auto fillCombo = [] (juce::ComboBox& box, const char* id)
    {
        for (auto& s : paramSpecs())
            if (juce::String (s.id) == id)
                for (int i = 0; i < (int) s.choices.size(); ++i)
                    box.addItem (s.choices[(size_t) i], i + 1);
    };
    {
        // Room list grouped by size (item ID = parameter index + 1, as the attachment expects).
        const auto& names = paramSpecs()[14].choices;
        roomBox.addItem (names[0], 1);
        const std::pair<const char*, int> groups[] = { { "OPEN", 1 }, { "SMALL", 4 }, { "MEDIUM", 7 }, { "LARGE", 10 }, { "HALL", 13 } };
        for (auto& [heading, first] : groups)
        {
            roomBox.addSectionHeading (heading);
            for (int i = first; i < first + 3; ++i)
                roomBox.addItem (names[(size_t) i], i + 1);
        }
    }
    fillCombo (obstacleBox, pid::obstacle);
    fillCombo (materialBox, pid::material);
    roomBox.setTooltip ("Source room. Off = direct cues only (use your own reverb sends).");
    obstacleBox.setTooltip ("Obstruction: object in the same room (sound bends around it). Occlusion: sound passes through a partition.");
    materialBox.setTooltip ("Partition material (mass law, coincidence, double-leaf resonance).");
    for (auto* b : { &roomBox, &obstacleBox, &materialBox })
        content.addAndMakeVisible (b);
    roomAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, pid::room, roomBox);
    obstacleAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, pid::obstacle, obstacleBox);
    materialAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, pid::material, materialBox);

    advanced = std::make_unique<AdvancedPanel> (p);
    advanced->onClose = [this]
    {
        advancedBtn.setToggleState (false, juce::dontSendNotification);
        advanced->setVisible (false);
    };
    content.addChildComponent (*advanced);

    refreshPresetList();
    layoutContent();

    setResizable (true, true);
    setResizeLimits (baseWidth / 2, baseHeight / 2, baseWidth * 2, baseHeight * 2);
    getConstrainer()->setFixedAspectRatio ((double) baseWidth / baseHeight);
    setSize (baseWidth * initialScale / 100, baseHeight * initialScale / 100);
    initialised = true;
    resized();
    setWantsKeyboardFocus (true);
    startTimerHz (15);
}

AmbienceEditor::~AmbienceEditor()
{
    stopTimer();
    content.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void AmbienceEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::background);
    g.addTransform (juce::AffineTransform::scale ((float) getWidth() / (float) baseWidth));

    // Wordmark
    g.setColour (col::text);
    g.setFont (ui::AmbienceLookAndFeel::font (15.0f, true));
    g.drawText ("SAMS", juce::Rectangle<int> (52, 13, 60, 20), juce::Justification::centredLeft);
    g.setColour (col::accent);
    g.drawText ("AMBIENCE CONTROL", juce::Rectangle<int> (97, 13, 170, 20), juce::Justification::centredLeft);
    g.setColour (col::textFaint);
    g.setFont (ui::AmbienceLookAndFeel::font (10.5f));
    g.drawText (juce::String::fromUTF8 ("distance \xc2\xb7 direction \xc2\xb7 obstacles  \xc2\xb7  v") + JucePlugin_VersionString,
                juce::Rectangle<int> (52, 32, 220, 14), juce::Justification::centredLeft);

    // Toolbar captions
    g.setColour (col::textDim);
    g.setFont (ui::AmbienceLookAndFeel::font (10.5f, true));
    g.drawText ("INPUT", juce::Rectangle<int> (16, 54, 50, 34), juce::Justification::centredLeft);
    g.drawText ("RENDER", juce::Rectangle<int> (232, 54, 56, 34), juce::Justification::centredLeft);
}

void AmbienceEditor::resized()
{
    if (! initialised)
        return;
    const float s = (float) getWidth() / (float) baseWidth;
    content.setBounds (0, 0, baseWidth, baseHeight);
    content.setTransform (juce::AffineTransform::scale (s));
    const int percent = juce::roundToInt (s * 100.0f);
    proc.uiScalePercent = percent;
    scaleBtn.setButtonText (juce::String (percent) + "%");
}

void AmbienceEditor::layoutContent()
{
    // Header (wordmark painted by the header area below via a Label-free approach)
    auto area = juce::Rectangle<int> (0, 0, baseWidth, baseHeight).reduced (16, 0);
    auto header = area.removeFromTop (54).withTrimmedTop (12).withTrimmedBottom (10);
    powerBtn.setBounds (header.removeFromLeft (28));
    header.removeFromLeft (252); // wordmark
    settingsBtn.setBounds (header.removeFromRight (32));
    header.removeFromRight (6);
    scaleBtn.setBounds (header.removeFromRight (58));
    header.removeFromRight (8);
    abB.setBounds (header.removeFromRight (30));
    abA.setBounds (header.removeFromRight (30));
    header.removeFromRight (8);
    redoBtn.setBounds (header.removeFromRight (48));
    header.removeFromRight (4);
    undoBtn.setBounds (header.removeFromRight (48));
    header.removeFromRight (12);
    auto presetArea = header;
    prevBtn.setBounds (presetArea.removeFromLeft (26));
    presetArea.removeFromLeft (4);
    menuBtn.setBounds (presetArea.removeFromRight (34));
    presetArea.removeFromRight (4);
    saveBtn.setBounds (presetArea.removeFromRight (50));
    presetArea.removeFromRight (4);
    nextBtn.setBounds (presetArea.removeFromRight (26));
    presetArea.removeFromRight (4);
    presetBox.setBounds (presetArea);

    auto toolbar = area.removeFromTop (34);
    auto t = toolbar;
    inputSeg.setBounds (t.removeFromLeft (200).withTrimmedLeft (52).reduced (0, 3));
    t.removeFromLeft (16);
    renderSeg.setBounds (t.removeFromLeft (222).withTrimmedLeft (56).reduced (0, 3));
    t.removeFromLeft (16);
    monoSafeToggle.setBounds (t.removeFromLeft (108));
    autoGainToggle.setBounds (t.removeFromLeft (106));
    monoDepthToggle.setBounds (t.removeFromLeft (118));
    advancedBtn.setBounds (t.removeFromRight (90).reduced (0, 3));

    area.removeFromTop (10);
    area.removeFromBottom (16);
    auto right = area.removeFromRight (332);
    area.removeFromRight (16);
    stage.setBounds (area);
    advanced->setBounds (area);
    statusLabel.setBounds (area.reduced (14, 10).removeFromTop (16).withTrimmedRight (140));
    statusLabel.toFront (false);

    auto pos = right.removeFromTop (138);
    positionCard.setBounds (pos);
    eqToggle.setBounds (pos.getRight() - 162, pos.getY() + 4, 156, 22);
    {
        auto k = pos.reduced (8, 0).withTrimmedTop (26).withTrimmedBottom (8);
        const int w = k.getWidth() / 3;
        distanceKnob.setBounds (k.removeFromLeft (w));
        panKnob.setBounds (k.removeFromLeft (w));
        widthKnob.setBounds (k);
    }
    right.removeFromTop (8);
    auto room = right.removeFromTop (120);
    roomCard.setBounds (room);
    {
        auto k = room.reduced (12, 0).withTrimmedTop (24).withTrimmedBottom (8);
        roomLevelKnob.setBounds (k.removeFromRight (84).withTrimmedTop (-4));
        roomBox.setBounds (k.withSizeKeepingCentre (k.getWidth() - 8, 28).withX (k.getX()));
    }
    right.removeFromTop (8);
    auto obs = right.removeFromTop (120);
    obstacleCard.setBounds (obs);
    {
        auto k = obs.reduced (12, 0).withTrimmedTop (26).withTrimmedBottom (8);
        amountKnob.setBounds (k.removeFromRight (84));
        k.removeFromRight (8);
        obstacleBox.setBounds (k.removeFromTop (28));
        k.removeFromTop (10);
        materialBox.setBounds (k.removeFromTop (28));
        sizeKnob.setBounds (obs.reduced (12, 0).withTrimmedTop (26).withTrimmedBottom (8).withTrimmedRight (92).withTrimmedTop (38).withSizeKeepingCentre (84, 58));
    }
    right.removeFromTop (8);
    outputCard.setBounds (right);
    monoToStereoToggle.setBounds (right.getRight() - 112 - 140, right.getY() + 4, 136, 22);
    keepMonoToggle.setBounds (right.getRight() - 112, right.getY() + 4, 106, 22);
    {
        auto k = right.reduced (12, 0).withTrimmedTop (26).withTrimmedBottom (10);
        outputKnob.setBounds (k.removeFromLeft (84));
        k.removeFromLeft (10);
        meters.setBounds (k.withTrimmedTop (2));
    }
}

void AmbienceEditor::timerCallback()
{
    const bool monoTrack = proc.isMonoOutput();
    const bool monoDepth = proc.apvts.getRawParameterValue (pid::monoDepth)->load() > 0.5f;
    const bool mono = monoTrack || monoDepth;
    stage.setMonoOutput (mono);
    panKnob.setEnabled (! mono);
    widthKnob.setEnabled (! mono);
    const bool multichannel = proc.isSurroundOutput();
    renderSeg.setEnabledAll (! mono && ! multichannel);
    monoSafeToggle.setEnabled (! mono && ! multichannel);
    // Mono to Stereo stays clickable on mono tracks too: the status line then explains why it cannot act.
    const bool m2sOn = proc.apvts.getRawParameterValue (pid::monoToStereo)->load() > 0.5f;
    using M2S = amb::SpatialEngine::MonoToStereoStatus;
    const auto m2sStatus = (M2S) proc.monoToStereoStatus.load();
    keepMonoToggle.setEnabled (m2sOn && ! mono);
    advanced->setMonoOutput (mono);

    const int obstacle = (int) proc.apvts.getRawParameterValue (pid::obstacle)->load();
    materialBox.setVisible (obstacle != 1);
    materialBox.setEnabled (obstacle == 2);
    sizeKnob.setVisible (obstacle == 1);
    amountKnob.setEnabled (obstacle != 0);
    roomLevelKnob.setEnabled (proc.apvts.getRawParameterValue (pid::room)->load() > 0.5f);

    const bool powered = powerBtn.getToggleState();
    for (juce::Component* c : { (juce::Component*) &stage, (juce::Component*) &positionCard, (juce::Component*) &roomCard, (juce::Component*) &obstacleCard,
                                (juce::Component*) &distanceKnob, (juce::Component*) &panKnob, (juce::Component*) &widthKnob, (juce::Component*) &roomLevelKnob,
                                (juce::Component*) &amountKnob, (juce::Component*) &sizeKnob, (juce::Component*) &roomBox, (juce::Component*) &obstacleBox,
                                (juce::Component*) &materialBox })
        c->setAlpha (powered ? 1.0f : 0.4f);

    // Status line: bypass, headphone note and mono warning.
    const bool headphones = proc.apvts.getRawParameterValue (pid::renderMode)->load() > 0.5f && ! mono && ! multichannel;
    const bool monoSafe = proc.apvts.getRawParameterValue (pid::monoSafe)->load() > 0.5f;
    if (updateChecker.isChecking())
    {
        statusLabel.setText ("Checking for updates...", juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, col::textDim);
    }
    else if (! powered)
    {
        statusLabel.setText (juce::String::fromUTF8 ("OFF \xe2\x80\x94 bypassed"), juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, col::warn);
    }
    else if (m2sOn && m2sStatus == M2S::monoOutput)
    {
        statusLabel.setText (juce::String::fromUTF8 ("\xe2\x9a\xa0 Mono \xe2\x86\x92 Stereo needs a stereo output: this track is mono (1 in, 1 out)"),
                             juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, col::warn);
    }
    else if (m2sOn && m2sStatus == M2S::monoDepth)
    {
        statusLabel.setText (juce::String::fromUTF8 ("Mono \xe2\x86\x92 Stereo is inactive while Mono Depth is on"), juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, col::warn);
    }
    else if (m2sOn && m2sStatus == M2S::stereoContent)
    {
        statusLabel.setText (juce::String::fromUTF8 ("Mono \xe2\x86\x92 Stereo: the input is stereo \xe2\x80\x94 left unchanged"), juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, col::textDim);
    }
    else if (multichannel && ! monoDepth)
    {
        const int ch = proc.getOutputChannelsForUi();
        const juce::String name = ch == 6 ? "5.1" : ch == 5 ? "5.0" : ch == 8 ? "7.1" : ch == 7 ? "7.0" : ch == 12 ? "7.1.4" : ch == 10 ? "7.1.2 / 5.1.4" : juce::String (ch) + " ch";
        statusLabel.setText (name + juce::String::fromUTF8 (" \xe2\x80\x94 direct over L-C-R, room in all channels"), juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, col::textDim);
    }
    else if (headphones)
    {
        statusLabel.setText ("Headphones render is not mono-compatible", juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, col::cool);
    }
    else if (! mono && ! monoSafe && proc.correlation.load() < 0.0f)
    {
        statusLabel.setText (juce::String::fromUTF8 ("\xe2\x9a\xa0 Negative correlation \xe2\x80\x94 check mono"), juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, col::warn);
    }
    else if (m2sOn && m2sStatus == M2S::active)
    {
        const bool keep = proc.apvts.getRawParameterValue (pid::keepMono)->load() > 0.5f;
        statusLabel.setText (juce::String::fromUTF8 (keep ? "MONO \xe2\x86\x92 STEREO \xe2\x80\x94 voice centred, recorded room widened (Keep Mono)"
                                                          : "MONO \xe2\x86\x92 STEREO \xe2\x80\x94 whole signal widened"),
                             juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, col::accent);
    }
    else if (monoDepth && ! monoTrack)
    {
        statusLabel.setText (juce::String::fromUTF8 ("MONO DEPTH \xe2\x80\x94 mono output, depth only"), juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, col::accent);
    }
    else if (mono)
    {
        statusLabel.setText (juce::String::fromUTF8 ("Mono track \xe2\x80\x94 depth only (pan & width inactive)"), juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, col::textDim);
    }
    else
        statusLabel.setText ({}, juce::dontSendNotification);

    undoBtn.setEnabled (proc.undoManager.canUndo());
    redoBtn.setEnabled (proc.undoManager.canRedo());

    const auto users = proc.presets.userNames();
    if (users != lastUserList)
        refreshPresetList();
    if (proc.presets.currentName() != lastPresetName)
    {
        lastPresetName = proc.presets.currentName();
        const int fi = proc.presets.currentFactoryIndex();
        if (fi >= 0)
            presetBox.setSelectedId (fi + 1, juce::dontSendNotification);
        else
        {
            const int ui = users.indexOf (lastPresetName);
            if (ui >= 0)
                presetBox.setSelectedId (1000 + ui, juce::dontSendNotification);
            else
                presetBox.setText (lastPresetName, juce::dontSendNotification);
        }
    }
}

void AmbienceEditor::refreshPresetList()
{
    presetBox.clear (juce::dontSendNotification);
    presetBox.addSectionHeading ("Factory");
    const auto factory = proc.presets.factoryNames();
    for (int i = 0; i < factory.size(); ++i)
        presetBox.addItem (factory[i], i + 1);
    lastUserList = proc.presets.userNames();
    if (! lastUserList.isEmpty())
    {
        presetBox.addSectionHeading ("User");
        for (int i = 0; i < lastUserList.size(); ++i)
            presetBox.addItem (lastUserList[i], 1000 + i);
    }
    lastPresetName = {};
}

void AmbienceEditor::showPresetMenu()
{
    juce::PopupMenu m;
    const bool isUser = proc.presets.currentIsUser();
    m.addItem (1, "Delete \"" + proc.presets.currentName() + "\"", isUser);
    m.addItem (2, "Show preset folder");
    m.addSeparator();
    m.addItem (3, "Copy current setting to the other A/B slot");
    m.addItem (4, "Reset to Init (Neutral)");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&menuBtn),
                     [this] (int r)
                     {
                         if (r == 1)
                             proc.presets.deleteUser (proc.presets.currentName());
                         else if (r == 2)
                         {
                             auto dir = PresetManager::userPresetFolder();
                             dir.createDirectory();
                             dir.revealToUser();
                         }
                         else if (r == 3)
                             abSlots[(size_t) (1 - abCurrent)] = proc.apvts.copyState();
                         else if (r == 4)
                             proc.presets.loadFactory (0);
                     });
}

void AmbienceEditor::savePresetDialog()
{
    auto* w = new juce::AlertWindow ("Save preset", "Name of the user preset:", juce::MessageBoxIconType::NoIcon, this);
    w->setLookAndFeel (&lnf);
    w->addTextEditor ("name", proc.presets.currentIsUser() ? proc.presets.currentName() : juce::String ("My preset"));
    w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int result)
                                                                  {
                                                                      if (result == 1)
                                                                          proc.presets.saveUser (w->getTextEditorContents ("name"));
                                                                      refreshPresetList();
                                                                  }),
                        true);
}

void AmbienceEditor::showScaleMenu()
{
    juce::PopupMenu m;
    for (int s : { 75, 100, 125, 150, 175, 200 })
        m.addItem (s, juce::String (s) + " %", true, s == proc.uiScalePercent.load());
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&scaleBtn), [this] (int r) { if (r > 0) setScale (r); });
}

void AmbienceEditor::setScale (int percent)
{
    setSize (baseWidth * percent / 100, baseHeight * percent / 100);
}

void AmbienceEditor::switchAB (int slot)
{
    abA.setToggleState (slot == 0, juce::dontSendNotification);
    abB.setToggleState (slot == 1, juce::dontSendNotification);
    if (slot == abCurrent)
        return;
    abSlots[(size_t) abCurrent] = proc.apvts.copyState();
    if (abSlots[(size_t) slot].isValid())
        proc.apvts.replaceState (abSlots[(size_t) slot].createCopy());
    else
        abSlots[(size_t) slot] = proc.apvts.copyState();
    abCurrent = slot;
}

bool AmbienceEditor::keyPressed (const juce::KeyPress& k)
{
    if (k.getModifiers().isCommandDown() && k.getKeyCode() == 'Z')
    {
        if (k.getModifiers().isShiftDown())
            proc.undoManager.redo();
        else
            proc.undoManager.undo();
        return true;
    }
    return false;
}

void AmbienceEditor::mouseDown (const juce::MouseEvent&)
{
    // Every mouse gesture starts a new undo transaction.
    proc.undoManager.beginNewTransaction();
}

void AmbienceEditor::showSettingsMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("SAMS AMBIENCE CONTROL  v" + UpdateChecker::currentVersion());
    m.addItem (1, "Check for updates...", ! updateChecker.isChecking());
    m.addItem (2, "Open download page (GitHub)");
    m.addSeparator();
    m.addItem (3, "Show preset folder");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&settingsBtn),
                     [this] (int r)
                     {
                         if (r == 1)
                             checkForUpdates();
                         else if (r == 2)
                             juce::URL (UpdateChecker::releasesPageUrl()).launchInDefaultBrowser();
                         else if (r == 3)
                         {
                             auto dir = PresetManager::userPresetFolder();
                             dir.createDirectory();
                             dir.revealToUser();
                         }
                     });
}

void AmbienceEditor::checkForUpdates()
{
    juce::Component::SafePointer<AmbienceEditor> safe (this);
    updateChecker.checkAsync ([safe] (const UpdateChecker::Result& r)
    {
        if (safe == nullptr)
            return;
        using Status = UpdateChecker::Result::Status;
        auto options = juce::MessageBoxOptions().withAssociatedComponent (safe.getComponent()).withTitle ("SAMS AMBIENCE CONTROL");

        if (r.status == Status::updateAvailable)
        {
            const auto url = r.releaseUrl;
            juce::AlertWindow::showAsync (options.withIconType (juce::MessageBoxIconType::NoIcon)
                                              .withMessage ("A new version is available: " + r.latestVersion + "\nInstalled version: " + r.currentVersion
                                                            + "\n\nDo you want to update now? The download page on GitHub will open.")
                                              .withButton ("Yes, update")
                                              .withButton ("Not now"),
                                          [url] (int result)
                                          {
                                              if (result == 1)
                                                  juce::URL (url).launchInDefaultBrowser();
                                          });
        }
        else if (r.status == Status::upToDate)
            juce::AlertWindow::showAsync (options.withIconType (juce::MessageBoxIconType::NoIcon)
                                              .withMessage ("You are using the latest version (" + r.currentVersion + ").")
                                              .withButton ("OK"),
                                          nullptr);
        else
            juce::AlertWindow::showAsync (options.withIconType (juce::MessageBoxIconType::NoIcon)
                                              .withMessage ("Could not check for updates.\n" + r.error)
                                              .withButton ("OK"),
                                          nullptr);
    });
}
