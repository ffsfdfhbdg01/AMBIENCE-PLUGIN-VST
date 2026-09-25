#include "PresetManager.h"

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& state) : apvts (state) {}

juce::File PresetManager::userPresetFolder()
{
#if JUCE_MAC
    auto dir = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Audio/Presets/Ambience Audio/Ambience");
#elif JUCE_WINDOWS
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Ambience/Presets");
#else
    auto dir = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".config/Ambience/Presets");
#endif
    return dir;
}

juce::StringArray PresetManager::factoryNames() const
{
    juce::StringArray names;
    for (const auto& p : amb::factoryPresets())
        names.add (p.name);
    return names;
}

juce::StringArray PresetManager::userNames() const
{
    juce::StringArray names;
    for (const auto& f : userPresetFolder().findChildFiles (juce::File::findFiles, false, "*" + extension))
        names.add (f.getFileNameWithoutExtension());
    names.sortNatural();
    return names;
}

void PresetManager::setParam (const juce::String& id, float plainValue)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
}

void PresetManager::resetToDefaults()
{
    for (const auto& s : amb::paramSpecs())
        setParam (s.id, s.defaultValue);
}

void PresetManager::loadFactory (int index)
{
    const auto& list = amb::factoryPresets();
    if (! juce::isPositiveAndBelow (index, (int) list.size()))
        return;
    if (auto* um = apvts.undoManager)
        um->beginNewTransaction ("Load preset");
    resetToDefaults();
    for (const auto& [id, v] : list[(size_t) index].values)
        setParam (id, v);
    current = list[(size_t) index].name;
}

bool PresetManager::loadUser (const juce::String& name)
{
    const auto file = userPresetFolder().getChildFile (name + extension);
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName ("AmbiencePreset"))
        return false;
    if (auto* um = apvts.undoManager)
        um->beginNewTransaction ("Load preset");
    resetToDefaults();
    for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
        setParam (e->getStringAttribute ("id"), (float) e->getDoubleAttribute ("value"));
    current = name;
    return true;
}

bool PresetManager::saveUser (const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty())
        return false;
    auto folder = userPresetFolder();
    if (! folder.createDirectory())
        return false;
    juce::XmlElement xml ("AmbiencePreset");
    xml.setAttribute ("version", presetFormatVersion);
    xml.setAttribute ("name", clean);
    for (const auto& s : amb::paramSpecs())
        if (auto* p = apvts.getParameter (s.id))
        {
            auto* e = xml.createNewChildElement ("PARAM");
            e->setAttribute ("id", s.id);
            e->setAttribute ("value", (double) p->convertFrom0to1 (p->getValue()));
        }
    if (! xml.writeTo (folder.getChildFile (clean + extension)))
        return false;
    current = clean;
    return true;
}

bool PresetManager::deleteUser (const juce::String& name)
{
    const auto ok = userPresetFolder().getChildFile (name + extension).deleteFile();
    if (ok && current == name)
        current = "Init (Neutral)";
    return ok;
}

int PresetManager::currentFactoryIndex() const
{
    if (currentIsUser())
        return -1;
    return factoryNames().indexOf (current);
}

bool PresetManager::currentIsUser() const { return userNames().contains (current); }

void PresetManager::loadNext (int direction)
{
    const auto factory = factoryNames();
    const auto user = userNames();
    const int total = factory.size() + user.size();
    int idx = currentIsUser() ? factory.size() + user.indexOf (current) : factory.indexOf (current);
    idx = ((idx < 0 ? 0 : idx + direction) % total + total) % total;
    if (idx < factory.size())
        loadFactory (idx);
    else
        loadUser (user[idx - factory.size()]);
}
