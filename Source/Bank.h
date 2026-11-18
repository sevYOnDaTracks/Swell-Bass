#pragma once
#include <juce_audio_formats/juce_audio_formats.h>

// Banques de sons : banque d'usine intégrée au plugin + dossier « Mes samples » de l'utilisateur.
namespace swell::bass
{
enum Category { Sub = 0, Reese, Eight08, Pluck, numCategories };

// Touche où chaque sample sonne à sa hauteur d'origine : C5 (note MIDI 60, convention FL Studio : do central = C5).
inline constexpr int kRootKey = 60;

inline const char* categoryName (int c)
{
    static const char* n[] { "SUB BASS", "REESE BASS", "808", "PLUCK BASS" };
    return n[juce::jlimit (0, numCategories - 1, c)];
}

struct Entry
{
    juce::String name;      // nom affiché (« Fondation »)
    int category = Sub;
    float rootNote = kRootKey; // touche (avec cents) où le sample sonne à sa hauteur d'origine
    bool user = false;      // vient du dossier de l'utilisateur
    juce::String resource;  // nom BinaryData (usine)
    juce::File file;        // fichier (utilisateur)
};

struct Sample
{
    juce::AudioBuffer<float> audio; // stéréo
    double sampleRate = 44100.0;
    float rootNote = kRootKey;
};

class Bank
{
public:
    Bank();
    void rescan();                                     // relit le dossier utilisateur
    const juce::Array<Entry>& entries (int category) const { return lists[(size_t) juce::jlimit (0, numCategories - 1, category)]; }
    std::shared_ptr<Sample> load (int category, int index) const;

    static juce::File userFolder();                    // ~/Music/Swell Audio/Swell Essential Bass/User
    static void setUserFolder (const juce::File&);
    // Favoris : communs à toutes les instances et tous les projets (settings.xml)
    bool isFavorite (int category, const juce::String& name) const { return favorites.contains (favKey (category, name)); }
    void toggleFavorite (int category, const juce::String& name);

    static float parseRootNote (const juce::String& fileStem, float fallback = kRootKey); // « Nom_C1 » -> 60, « Nom_E1+15 » -> 64,15

private:
    void addFactory();
    static juce::String favKey (int c, const juce::String& n) { return juce::String (categoryName (c)) + "/" + n; }
    juce::StringArray favorites;
    std::array<juce::Array<Entry>, numCategories> lists;
    mutable juce::AudioFormatManager formats;
};
} // namespace swell::bass
