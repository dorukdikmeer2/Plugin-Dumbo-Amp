#include "Presets/PresetManager.h"

namespace {
juce::File& overrideDir() { static juce::File f; return f; }
const juce::Identifier kNameProp("presetName");
constexpr const char* kExt = ".ddpreset";
}

juce::File PresetManager::presetDirectory()
{
    if (overrideDir() != juce::File()) return overrideDir();
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
               .getChildFile("DorukAudio").getChildFile("DumbDockAmp").getChildFile("Presets");
}

void PresetManager::setDirectoryOverride(const juce::File& dir) { overrideDir() = dir; }

juce::String PresetManager::cleanName(const juce::String& raw)
{
    auto n = juce::File::createLegalFileName(raw.trim()).trim();
    return n.substring(0, 64);
}

juce::File PresetManager::fileFor(const juce::String& name) const
{
    return presetDirectory().getChildFile(cleanName(name) + kExt);
}

juce::StringArray PresetManager::userPresetNames() const
{
    juce::StringArray names;
    for (const auto& f : presetDirectory().findChildFiles(juce::File::findFiles, false, juce::String("*") + kExt))
        names.add(f.getFileNameWithoutExtension());
    names.sortNatural();
    return names;
}

juce::String PresetManager::currentName() const
{
    const auto n = apvts.state.getProperty(kNameProp).toString();
    return n.isEmpty() ? juce::String(kDefaultName) : n;
}

bool PresetManager::saveUser(const juce::String& rawName)
{
    const auto name = cleanName(rawName);
    if (name.isEmpty() || name.equalsIgnoreCase(kDefaultName)) return false;          // "Default" is reserved
    presetDirectory().createDirectory();
    apvts.state.setProperty(kNameProp, name, nullptr);
    if (auto xml = apvts.copyState().createXml())
        return xml->writeTo(fileFor(name));
    return false;
}

bool PresetManager::loadUser(const juce::String& name)
{
    const auto f = fileFor(name);
    if (!f.existsAsFile()) return false;
    std::unique_ptr<juce::XmlElement> xml(juce::XmlDocument::parse(f));
    if (xml == nullptr || !xml->hasTagName(apvts.state.getType())) return false;
    auto vt = juce::ValueTree::fromXml(*xml);
    if (!vt.isValid()) return false;
    vt.setProperty(kNameProp, cleanName(name), nullptr);
    apvts.replaceState(vt);
    return true;
}

bool PresetManager::removeUser(const juce::String& name)
{
    const auto f = fileFor(name);
    const bool ok = f.existsAsFile() && f.deleteFile();
    if (ok && currentName() == cleanName(name)) apvts.state.setProperty(kNameProp, kDefaultName, nullptr);
    return ok;
}

void PresetManager::loadDefault()
{
    for (auto* p : apvts.processor.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(p)) r->setValueNotifyingHost(r->getDefaultValue());
    apvts.state.setProperty(kNameProp, kDefaultName, nullptr);
}
