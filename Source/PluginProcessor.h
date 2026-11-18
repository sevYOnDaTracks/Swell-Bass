#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "Bank.h"
#include "Licence/SwellLicence.h"

namespace swell::bass
{
// Moteur mono : une basse ne joue qu'une note à la fois (pile de notes, legato, glide).
class BassVoice
{
public:
    struct Settings
    {
        float attack = 0.002f, decay = -1.f, release = 0.08f; // secondes ; decay < 0 : le sample suit sa propre chute
        float drive = 0, tone = 1, glide = 0.08f;
        float detune = 0, width = 0, punch = 0, snap = 0, gain = 1;
        int category = Sub, mode = 0; // 0 Mono, 1 Legato, 2 Glide
        bool sustain = true;          // tenue en boucle tant que la note est tenue
    };

    void prepare (double sr) { hostRate = sr; reset(); }
    void reset();
    void noteOn (int note, float velocity, const Settings&);
    void noteOff (int note, const Settings&);
    void allOff() { held.clear(); if (stage != Idle) stage = Release; }
    void render (juce::AudioBuffer<float>& out, int start, int num, const Sample* sample, const Settings&);
    int currentNote() const noexcept { return held.isEmpty() ? -1 : held.getLast(); }

private:
    enum Stage { Idle, Attack, Decay, Release };
    void trigger (int note, bool retrigger, bool slide);
    double hostRate = 44100;
    juce::Array<int> held;
    Stage stage = Idle;
    double posA = 0, posB = 0;      // deux lectures pour la Reese (désaccord gauche / droite)
    float env = 0, vel = 1;
    float pitch = 24, target = 24;  // demi-tons
    float glideFrom = 24, glideT = 1;
    double since = 0;               // secondes depuis l'attaque (punch, snap)
    float lp[2][2] {};              // filtre passe-bas 2 pôles par canal
    double lastRate = 1, lastRateB = 1; // vitesses de lecture du dernier échantillon (lectures A et B)
    bool tailOn = false;            // note précédente en train de s'éteindre (anti-clic)
    double tailPos = 0, tailPosB = 0, tailRate = 1, tailRateB = 1;
    float tailGain = 0;
};

class Processor final : public juce::AudioProcessor, private juce::Timer
{
public:
    Processor();
    ~Processor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Swell Bass"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState params;
    juce::MidiKeyboardState keyboard;
    Bank bank;

    int category() const;
    int sampleIndex() const;
    void selectSample (int category, int index);  // thread des messages
    void rescanBank();
    std::shared_ptr<Sample> displayedSample() const { return uiSample; }
    juce::String sampleName() const;

    std::atomic<int> playingNote { -1 };
    std::atomic<float> level { 0 };

private:
    void timerCallback() override;
    void loadIfNeeded();
    BassVoice::Settings readSettings() const;

    juce::SharedResourcePointer<swell::Licence> licence;
    BassVoice voice;
    std::shared_ptr<Sample> current, pending, uiSample;
    juce::SpinLock swapLock;
    bool fresh = false;   // protégé par swapLock : un nouveau sample attend le thread audio
    int loadedCat = -1, loadedIdx = -1;
    juce::String restoreName; // nom du sample à retrouver après rechargement d'un projet
    std::atomic<float>* p[16] {};
};
} // namespace swell::bass
