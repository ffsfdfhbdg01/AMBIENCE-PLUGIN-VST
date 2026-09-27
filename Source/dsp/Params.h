// Parameter table shared by the plugin, the offline renderer and the tests.
// IDs are part of the saved-state format: never rename or reuse an ID.
#pragma once

#include <map>
#include <string>
#include <vector>

namespace amb
{
enum class ParamKind
{
    floating,
    choice,
    toggle
};

enum class ParamScale
{
    linear,
    logarithmic
};

struct ParamSpec
{
    const char* id;
    const char* name;
    ParamKind kind;
    float minValue;
    float maxValue;
    float defaultValue;
    float step;
    ParamScale scale;
    const char* unit;
    std::vector<const char*> choices;
};

namespace pid
{
    inline constexpr const char* inputMode = "inputMode";
    inline constexpr const char* renderMode = "renderMode";
    inline constexpr const char* pan = "pan";
    inline constexpr const char* timePan = "timePan";
    inline constexpr const char* panLaw = "panLaw";
    inline constexpr const char* monoSafe = "monoSafe";
    inline constexpr const char* distance = "distance";
    inline constexpr const char* recDistance = "recDistance";
    inline constexpr const char* rolloff = "rolloff";
    inline constexpr const char* airAbsorption = "airAbsorption";
    inline constexpr const char* temperature = "temperature";
    inline constexpr const char* humidity = "humidity";
    inline constexpr const char* micPattern = "micPattern";
    inline constexpr const char* propDelay = "propDelay";
    inline constexpr const char* room = "room";
    inline constexpr const char* roomLevel = "roomLevel";
    inline constexpr const char* decay = "decay";
    inline constexpr const char* width = "width";
    inline constexpr const char* bassMono = "bassMono";
    inline constexpr const char* obstacle = "obstacle";
    inline constexpr const char* material = "material";
    inline constexpr const char* amount = "amount";
    inline constexpr const char* obstacleSize = "obstacleSize";
    inline constexpr const char* thickness = "thickness";
    inline constexpr const char* leak = "leak";
    inline constexpr const char* autoGain = "autoGain";
    inline constexpr const char* autoGainRange = "autoGainRange";
    inline constexpr const char* output = "output";
    inline constexpr const char* monoDepth = "monoDepth";
    inline constexpr const char* eqAmbience = "eqAmbience";
    inline constexpr const char* monoToStereo = "monoToStereo";
    inline constexpr const char* keepMono = "keepMono";
} // namespace pid

inline const std::vector<ParamSpec>& paramSpecs()
{
    using K = ParamKind;
    using S = ParamScale;
    static const std::vector<ParamSpec> specs = {
        { pid::inputMode, "Input", K::choice, 0, 2, 0, 1, S::linear, "", { "Auto", "Mono", "Stereo" } },
        { pid::renderMode, "Render Mode", K::choice, 0, 1, 0, 1, S::linear, "", { "Speakers", "Headphones" } },
        { pid::pan, "Pan", K::floating, -100, 100, 0, 0.1f, S::linear, "", {} },
        { pid::timePan, "Time Pan", K::floating, 0, 100, 50, 0.1f, S::linear, "%", {} },
        { pid::panLaw, "Pan Law", K::choice, 0, 3, 1, 1, S::linear, "", { "0 dB", "-3 dB", "-4.5 dB", "-6 dB" } },
        { pid::monoSafe, "Mono-safe", K::toggle, 0, 1, 1, 1, S::linear, "", {} },
        { pid::distance, "Distance", K::floating, 0.1f, 1000, 1, 0, S::logarithmic, "m", {} },
        { pid::recDistance, "Recorded Distance", K::floating, 0.1f, 10, 1, 0, S::logarithmic, "m", {} },
        { pid::rolloff, "Rolloff", K::floating, 0, 12, 6, 0.1f, S::linear, "dB/dd", {} },
        { pid::airAbsorption, "Air Absorption", K::toggle, 0, 1, 1, 1, S::linear, "", {} },
        { pid::temperature, "Temperature", K::floating, -10, 40, 20, 0.5f, S::linear, "C", {} },
        { pid::humidity, "Humidity", K::floating, 10, 100, 50, 1, S::linear, "%", {} },
        { pid::micPattern, "Mic Pattern", K::choice, 0, 2, 0, 1, S::linear, "", { "Omni", "Cardioid", "Figure-8" } },
        { pid::propDelay, "Propagation Delay", K::toggle, 0, 1, 0, 1, S::linear, "", {} },
        { pid::room, "Room", K::choice, 0, 15, 0, 1, S::linear, "",
          { "Off", "Open A (field)", "Open B (street)", "Open C (courtyard)", "Small A", "Small B (dry)", "Small C (tiled)",
            "Medium A", "Medium B (furnished)", "Medium C (empty)", "Large A", "Large B (studio)", "Large C (warehouse)",
            "Hall A", "Hall B (theatre)", "Hall C (church)" } },
        { pid::roomLevel, "Room Level", K::floating, -24, 12, 0, 0.1f, S::linear, "dB", {} },
        { pid::decay, "Decay", K::floating, 50, 200, 100, 1, S::linear, "%", {} },
        { pid::width, "Width", K::floating, 0, 200, 100, 0.1f, S::linear, "%", {} },
        { pid::bassMono, "Bass Mono", K::choice, 0, 8, 0, 1, S::linear, "", { "Off", "40 Hz", "60 Hz", "80 Hz", "100 Hz", "120 Hz", "150 Hz", "200 Hz", "300 Hz" } },
        { pid::obstacle, "Obstacle", K::choice, 0, 2, 0, 1, S::linear, "", { "None", "Obstruction", "Occlusion" } },
        { pid::material, "Material", K::choice, 0, 7, 3, 1, S::linear, "",
          { "Heavy Curtain", "Glass 6 mm", "Hollow Door", "Solid Wood Door", "Drywall (stud wall)", "Brick 12 cm", "Concrete 20 cm", "Car Body" } },
        { pid::amount, "Amount", K::floating, 0, 100, 100, 0.1f, S::linear, "%", {} },
        { pid::obstacleSize, "Obstacle Size", K::floating, 0, 3, 1, 0.01f, S::linear, "m", {} },
        { pid::thickness, "Thickness", K::floating, 0.25f, 4, 1, 0, S::logarithmic, "x", {} },
        { pid::leak, "Leak", K::floating, 0, 100, 0, 0.1f, S::linear, "%", {} },
        { pid::autoGain, "Auto Gain", K::toggle, 0, 1, 0, 1, S::linear, "", {} },
        { pid::autoGainRange, "Auto Gain Range", K::floating, 0, 36, 24, 0.5f, S::linear, "dB", {} },
        { pid::output, "Output", K::floating, -24, 24, 0, 0.1f, S::linear, "dB", {} },
        { pid::monoDepth, "Mono Depth", K::toggle, 0, 1, 0, 1, S::linear, "", {} },
        { pid::eqAmbience, "Equalized Ambience", K::toggle, 0, 1, 1, 1, S::linear, "", {} },
        { pid::monoToStereo, "Mono to Stereo", K::toggle, 0, 1, 0, 1, S::linear, "", {} },
        { pid::keepMono, "Keep Mono", K::toggle, 0, 1, 1, 1, S::linear, "", {} },
    };
    return specs;
}

inline const float* bassMonoFrequencies()
{
    static const float f[] = { 0, 40, 60, 80, 100, 120, 150, 200, 300 };
    return f;
}

// Plain parameter values used by the engine (units as in the table above).
struct EngineParams
{
    int inputMode = 0;  // 0 auto, 1 mono, 2 stereo
    int renderMode = 0; // 0 speakers, 1 headphones
    float pan = 0;
    float timePan = 50;
    int panLaw = 1;
    bool monoSafe = true;
    float distance = 1;
    float recDistance = 1;
    float rolloff = 6;
    bool airAbsorption = true;
    float temperature = 20;
    float humidity = 50;
    int micPattern = 0;
    bool propDelay = false;
    int room = 0;
    float roomLevel = 0;
    float decay = 100;
    float width = 100;
    int bassMono = 0;
    int obstacle = 0;
    int material = 3;
    float amount = 100;
    float obstacleSize = 1;
    float thickness = 1;
    float leak = 0;
    bool autoGain = false;
    float autoGainRange = 24;
    float output = 0;
    bool monoDepth = false; // mono output: depth cues only (distance, air, room, obstacle)
    bool eqAmbience = true; // automatic distance/position EQ (air absorption, proximity, position shading)
    bool monoToStereo = false; // mono signal in -> mono-compatible stereo out (amount follows Width)
    bool keepMono = true;      // with Mono to Stereo: only the recorded ambience is widened, the voice stays mono

    // Sets a value by parameter ID (plain units). Unknown IDs are ignored.
    void set (const std::string& id, float v)
    {
        auto i = [v] { return (int) (v + 0.5f); };
        auto b = [v] { return v >= 0.5f; };
        if (id == pid::inputMode) inputMode = i();
        else if (id == pid::renderMode) renderMode = i();
        else if (id == pid::pan) pan = v;
        else if (id == pid::timePan) timePan = v;
        else if (id == pid::panLaw) panLaw = i();
        else if (id == pid::monoSafe) monoSafe = b();
        else if (id == pid::distance) distance = v;
        else if (id == pid::recDistance) recDistance = v;
        else if (id == pid::rolloff) rolloff = v;
        else if (id == pid::airAbsorption) airAbsorption = b();
        else if (id == pid::temperature) temperature = v;
        else if (id == pid::humidity) humidity = v;
        else if (id == pid::micPattern) micPattern = i();
        else if (id == pid::propDelay) propDelay = b();
        else if (id == pid::room) room = i();
        else if (id == pid::roomLevel) roomLevel = v;
        else if (id == pid::decay) decay = v;
        else if (id == pid::width) width = v;
        else if (id == pid::bassMono) bassMono = i();
        else if (id == pid::obstacle) obstacle = i();
        else if (id == pid::material) material = i();
        else if (id == pid::amount) amount = v;
        else if (id == pid::obstacleSize) obstacleSize = v;
        else if (id == pid::thickness) thickness = v;
        else if (id == pid::leak) leak = v;
        else if (id == pid::autoGain) autoGain = b();
        else if (id == pid::autoGainRange) autoGainRange = v;
        else if (id == pid::output) output = v;
        else if (id == pid::monoDepth) monoDepth = b();
        else if (id == pid::eqAmbience) eqAmbience = b();
        else if (id == pid::monoToStereo) monoToStereo = b();
        else if (id == pid::keepMono) keepMono = b();
    }
};

//==============================================================================
// Factory presets: only the values that differ from the defaults are listed; everything else
// (including Auto Gain OFF and Output 0 dB) comes from the defaults.
struct FactoryPreset
{
    const char* name;
    std::vector<std::pair<const char*, float>> values;
};

inline const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> presets = {
        { "Init (Neutral)", {} },
        { "Dialogue - Close-Up", { { pid::recDistance, 0.5f }, { pid::distance, 0.7f }, { pid::room, 4 }, { pid::roomLevel, -9 } } },
        { "Dialogue - Medium Shot", { { pid::recDistance, 0.5f }, { pid::distance, 2.5f }, { pid::room, 4 }, { pid::roomLevel, -4 } } },
        { "Dialogue - Wide Shot", { { pid::recDistance, 0.5f }, { pid::distance, 8 }, { pid::room, 7 }, { pid::roomLevel, -2 } } },
        { "Across the Room", { { pid::distance, 5 }, { pid::pan, -30 }, { pid::room, 7 } } },
        { "Off-Screen Left", { { pid::distance, 4 }, { pid::pan, -85 }, { pid::room, 4 } } },
        { "Off-Screen Right", { { pid::distance, 4 }, { pid::pan, 85 }, { pid::room, 4 } } },
        { "Across the Street", { { pid::distance, 25 }, { pid::room, 1 } } },
        { "Far Away", { { pid::distance, 150 }, { pid::room, 1 } } },
        { "Behind Closed Door", { { pid::obstacle, 2 }, { pid::material, 3 }, { pid::leak, 10 }, { pid::distance, 3 }, { pid::room, 4 } } },
        { "Door Ajar", { { pid::obstacle, 2 }, { pid::material, 3 }, { pid::leak, 60 }, { pid::distance, 3 }, { pid::room, 4 } } },
        { "Next Room (Drywall)", { { pid::obstacle, 2 }, { pid::material, 4 }, { pid::distance, 4 }, { pid::room, 7 } } },
        { "Neighbours (Brick Wall)", { { pid::obstacle, 2 }, { pid::material, 5 }, { pid::distance, 5 }, { pid::room, 7 } } },
        { "Through Concrete", { { pid::obstacle, 2 }, { pid::material, 6 }, { pid::distance, 8 }, { pid::room, 10 } } },
        { "Through Window", { { pid::obstacle, 2 }, { pid::material, 1 }, { pid::leak, 5 }, { pid::distance, 15 }, { pid::room, 1 } } },
        { "Behind Curtain", { { pid::obstacle, 2 }, { pid::material, 0 }, { pid::distance, 2 }, { pid::room, 4 } } },
        { "Around the Corner", { { pid::obstacle, 1 }, { pid::obstacleSize, 1.5f }, { pid::distance, 8 }, { pid::room, 1 } } },
        { "Street from Inside a Car", { { pid::obstacle, 2 }, { pid::material, 7 }, { pid::distance, 6 }, { pid::room, 1 } } },
        { "Wide Ambience", { { pid::inputMode, 2 }, { pid::width, 150 }, { pid::bassMono, 5 } } },
        { "Narrow Ambience", { { pid::inputMode, 2 }, { pid::width, 50 } } },
        { "Mono Dialogue - Medium Shot", { { pid::monoDepth, 1 }, { pid::inputMode, 1 }, { pid::recDistance, 0.5f }, { pid::distance, 3 }, { pid::room, 8 }, { pid::roomLevel, -3 } } },
        { "Mono Dialogue - Wide Shot", { { pid::monoDepth, 1 }, { pid::inputMode, 1 }, { pid::recDistance, 0.5f }, { pid::distance, 9 }, { pid::room, 7 }, { pid::roomLevel, -1 } } },
        { "Mono Dialogue - Next Room", { { pid::monoDepth, 1 }, { pid::inputMode, 1 }, { pid::recDistance, 0.5f }, { pid::distance, 4 }, { pid::room, 8 }, { pid::obstacle, 2 }, { pid::material, 4 } } },
        { "Mono to Stereo", { { pid::monoToStereo, 1 } } },
        { "Mono to Stereo - Wide", { { pid::monoToStereo, 1 }, { pid::keepMono, 0 }, { pid::width, 160 }, { pid::bassMono, 3 } } },
        { "Mono to Stereo - Dialogue in Room", { { pid::monoToStereo, 1 }, { pid::recDistance, 0.5f }, { pid::distance, 3 }, { pid::room, 7 }, { pid::roomLevel, -4 } } },
    };
    return presets;
}

inline EngineParams paramsForPreset (const FactoryPreset& p)
{
    EngineParams e;
    for (auto& [id, v] : p.values)
        e.set (id, v);
    return e;
}

} // namespace amb
