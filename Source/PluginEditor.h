#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "UpdateChecker.h"
#include "ui/EqView.h"
#include "ui/LookAndFeel.h"
#include "ui/Meters.h"
#include "ui/StageView.h"

namespace ui
{
// Rotary knob with a caption and an editable value box. Cmd/Ctrl-drag = fine adjustment.
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& state, const char* paramId, const juce::String& caption, const juce::String& tooltip);
    void resized() override;

    class FineSlider : public juce::Slider
    {
    public:
        void mouseDown (const juce::MouseEvent& e) override
        {
            setMouseDragSensitivity (e.mods.isCommandDown() || e.mods.isCtrlDown() ? 1400 : 220);
            juce::Slider::mouseDown (e);
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            setMouseDragSensitivity (e.mods.isCommandDown() || e.mods.isCtrlDown() ? 1400 : 220);
            juce::Slider::mouseDrag (e);
        }
    };

    FineSlider slider;
    juce::Label caption;

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// Segmented radio control bound to a choice parameter.
class Segmented : public juce::Component
{
public:
    Segmented (juce::AudioProcessorValueTreeState& state, const char* paramId, const juce::StringArray& labels, const juce::String& tooltip);
    void resized() override;
    void setEnabledAll (bool e);

private:
    juce::OwnedArray<juce::TextButton> buttons;
    juce::ParameterAttachment attachment;
};

// Flat icon button (power / gear) drawn with vector paths.
class IconButton : public juce::Button
{
public:
    enum class Icon
    {
        power,
        gear
    };
    IconButton (Icon i, const juce::String& name) : juce::Button (name), icon (i) {}
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    Icon icon;
};

class Card : public juce::Component
{
public:
    explicit Card (const juce::String& t) : title (t) { setInterceptsMouseClicks (false, true); }
    void paint (juce::Graphics& g) override;
    juce::String title;
};
} // namespace ui

class AmbienceEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AmbienceEditor (AmbienceProcessor&);
    ~AmbienceEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;

    static constexpr int baseWidth = 940;
    static constexpr int baseHeight = 640;

private:
    void timerCallback() override;
    void layoutContent();
    void refreshPresetList();
    void showPresetMenu();
    void savePresetDialog();
    void showScaleMenu();
    void switchAB (int slot);
    void setScale (int percent);
    void showSettingsMenu();
    void checkForUpdates();

    AmbienceProcessor& proc;
    ui::AmbienceLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    juce::Component content;

    // Header
    juce::ComboBox presetBox;
    juce::TextButton prevBtn { juce::String::fromUTF8 ("\xe2\x80\xb9") }, nextBtn { juce::String::fromUTF8 ("\xe2\x80\xba") },
        saveBtn { "Save" }, menuBtn { juce::String::fromUTF8 ("\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2") };
    juce::TextButton undoBtn { "Undo" }, redoBtn { "Redo" }, abA { "A" }, abB { "B" }, scaleBtn { "100%" };
    ui::IconButton powerBtn { ui::IconButton::Icon::power, "Power" }, settingsBtn { ui::IconButton::Icon::gear, "Settings" };
    juce::ParameterAttachment powerAttachment;
    UpdateChecker updateChecker;

    // Toolbar
    ui::Segmented inputSeg, renderSeg;
    juce::ToggleButton monoSafeToggle { "Mono-safe" }, autoGainToggle { "Auto Gain" }, monoDepthToggle { "Mono Depth" },
        eqToggle { "Equalized Ambience" }, monoToStereoToggle { juce::String::fromUTF8 ("Mono \xe2\x86\x92 Stereo") },
        keepMonoToggle { "Keep Mono" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> monoSafeAtt, autoGainAtt, monoDepthAtt, eqAtt, monoToStereoAtt, keepMonoAtt;
    juce::TextButton advancedBtn { "Advanced" };
    juce::Label statusLabel;
    juce::String monoInsertWarning; // status text when Mono to Stereo sits on a mono (1 in, 1 out) insert, per host

    // Main
    ui::StageView stage;
    ui::Card positionCard { "POSITION" }, roomCard { "ROOM" }, obstacleCard { "OBSTACLE" }, outputCard { "OUTPUT" };
    ui::Knob distanceKnob, panKnob, widthKnob, roomLevelKnob, amountKnob, sizeKnob, outputKnob;
    juce::ComboBox roomBox, obstacleBox, materialBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> roomAtt, obstacleAtt, materialAtt;
    ui::Meters meters;

    // Advanced panel
    class AdvancedPanel;
    std::unique_ptr<AdvancedPanel> advanced;

    std::array<juce::ValueTree, 2> abSlots;
    int abCurrent = 0;
    bool initialised = false;
    juce::String lastPresetName;
    juce::StringArray lastUserList;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmbienceEditor)
};
