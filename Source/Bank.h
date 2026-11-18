#pragma once
#include <juce_audio_formats/juce_audio_formats.h>

// Banques de sons : banque d'usine intégrée au plugin + dossier « Mes samples » de l'utilisateur.
namespace swell::bass
{
enum Category { Sub = 0, Reese, Eight08, Pluck, numCategories };

inline const char* categoryName (int c)
{
    static const char* n[] { "SUB BASS", "REESE BASS", "808", "PLUCK BASS" };
    return n[juce::jlimit (0, numCategories - 1, c)];
}

struct Entry
{
    juce::String name;      // nom affiché (« Fondation »)
    int category = Sub;
    int rootNote = 24;      // note MIDI de base (C1 = 24)
    bool user = false;      // vient du dossier de l'utilisateur
    juce::String resource;  // nom BinaryData (usine)
    juce::File file;        // fichier (utilisateur)
};

struct Sample
{
    juce::AudioBuffer<float> audio; // stéréo
    double sampleRate = 44100.0;
    int rootNote = 24;
};

class Bank
{
public:
    Bank();
    void rescan();                                     // relit le dossier utilisateur
    const juce::Array<Entry>& entries (int category) const { return lists[(size_t) juce::jlimit (0, numCategories - 1, category)]; }
    std::shared_ptr<Sample> load (int category, int index) const;

    static juce::File userFolder();                    // ~/Music/Swell Audio/Swell Bass/User
    static void setUserFolder (const juce::File&);
    static int parseRootNote (const juce::String& fileStem, int fallback = 24); // « Nom_C1 » -> 24

private:
    void addFactory();
    std::array<juce::Array<Entry>, numCategories> lists;
    mutable juce::AudioFormatManager formats;
};
} // namespace swell::bass
