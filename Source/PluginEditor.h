#pragma once
#include "PluginProcessor.h"
#include "ResizableEditor.h"

namespace swell::bass
{
namespace col
{
    const juce::Colour bg { 0xff07060c }, panel { 0xff12101c }, box { 0xff0a0912 }, line { 0xff2a2540 },
                       ink { 0xffece8ff }, muted { 0xff8a84a8 };
    inline juce::Colour accent (int category)
    {
        static const juce::Colour c[] { juce::Colour (0xff3ee0ff), juce::Colour (0xffb45cff), juce::Colour (0xffff4d6d), juce::Colour (0xffffc93e) };
        return c[juce::jlimit (0, 3, category)];
    }
}

class Look final : public juce::LookAndFeel_V4
{
public:
    Look();
    juce::Colour accent = col::accent (0);
    juce::Typeface::Ptr bebas;
    juce::Font display (float h) const { return juce::Font (juce::FontOptions (bebas).withHeight (h)); }
    void drawRotarySlider (juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool, bool) override;
};

class Knob final : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState&, const juce::String& id, const juce::String& label);
    void resized() override;
    void paint (juce::Graphics&) override;
    juce::Slider slider;
private:
    juce::String label;
    juce::AudioProcessorValueTreeState::SliderAttachment attach;
    juce::RangedAudioParameter* param;
};

class SampleList final : public juce::ListBoxModel, public juce::Component
{
public:
    explicit SampleList (Processor&);
    void refresh();
    int getNumRows() override;
    void paintListBoxItem (int, juce::Graphics&, int, int, bool) override;
    void listBoxItemClicked (int, const juce::MouseEvent&) override;
    void resized() override { list.setBounds (getLocalBounds()); }
    void setFavoritesOnly (bool f) { favOnly = f; refresh(); }
    bool favoritesOnly() const noexcept { return favOnly; }
    juce::ListBox list;
private:
    void rebuildRows();
    Processor& proc;
    std::vector<int> rows;   // lignes affichées -> index réels dans la catégorie
    bool favOnly = false;
};

class WaveView final : public juce::Component
{
public:
    explicit WaveView (Processor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void update();
private:
    Processor& proc;
    std::shared_ptr<Sample> shown;
    juce::Path wave;
    int shownWidth = 0;
};

class Settings final : public juce::Component, private juce::ChangeListener
{
public:
    explicit Settings (Processor&);
    ~Settings() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    std::function<void (float)> onZoom;
    std::function<void()> onBankChanged;
    float zoom = 1.f;
private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
    void refresh();
    juce::Rectangle<int> card() const;
    Processor& proc;
    juce::SharedResourcePointer<swell::Licence> licence;
    juce::TextButton close { "X" }, deactivate { juce::String::fromUTF8 ("Désactiver") }, change { "Changer" }, open { "Ouvrir" }, rescan { "Rescanner" };
    juce::OwnedArray<juce::TextButton> zooms;
    std::unique_ptr<juce::FileChooser> chooser;
};

class Editor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit Editor (Processor&);
    ~Editor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void syncCategory();
    void step (int delta);
    juce::Rectangle<int> tabBounds (int i) const;

    Processor& proc;
    Look look;
    juce::Image logo;
    juce::TextButton prev { juce::String::fromUTF8 ("\xe2\x97\x80") }, next { juce::String::fromUTF8 ("\xe2\x96\xb6") },
                     gear { juce::String::fromUTF8 ("\xe2\x9a\x99") };
    SampleList samples;
    WaveView wave;
    juce::OwnedArray<Knob> knobs;
    juce::TextButton modes[3] { juce::TextButton ("Mono"), juce::TextButton ("Legato"), juce::TextButton ("Glide") };
    juce::TextButton favFilter { juce::String::fromUTF8 ("\xe2\x98\x85 Favoris") };
    juce::TextButton sustain { juce::String::fromUTF8 ("TENUE \xe2\x88\x9e") };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> sustainAttach;
    juce::MidiKeyboardComponent keys;
    Settings settings;
    int shownCat = -1, shownIdx = -1, shownCount = -1;
    float shownLevel = 0;
    int shownNote = -2;
};
} // namespace swell::bass
