// Outil hors ligne : capture de l'interface et rendu audio d'une note par catégorie.
//   BassTool <dossier de sortie>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

using namespace swell::bass;

static void setP (Processor& p, const char* id, float v)
{
    auto* prm = p.params.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    swell::Licence::unlockForTesting();
    const juce::File out (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    out.createDirectory();

    Processor proc;
    const double sr = 48000;
    proc.setPlayConfigDetails (0, 2, sr, 512);
    proc.prepareToPlay (sr, 512);

    juce::WavAudioFormat wav;
    for (int c = 0; c < numCategories; ++c)
    {
        proc.selectSample (c, 0);
        const bool neutral = std::getenv ("BASS_NEUTRAL") != nullptr;
        setP (proc, "punch", neutral ? 0.f : (c == Eight08 ? 0.4f : 0.f));
        if (neutral) { setP (proc, "snap", 0.f); setP (proc, "detune", 0.f); }
        proc.prepareToPlay (sr, 512);
        // rendu : C5 tenu 1,5 s puis relâché, puis D5
        juce::AudioBuffer<float> all (2, (int) (sr * 3));
        all.clear();
        juce::AudioBuffer<float> block (2, 512);
        for (int pos = 0; pos < all.getNumSamples(); pos += 512)
        {
            juce::MidiBuffer m;
            auto at = [&] (double t) { return (int) (t * sr) >= pos && (int) (t * sr) < pos + 512; };
            if (at (0.01)) m.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), (int) (0.01 * sr) - pos);
            if (at (1.5))  m.addEvent (juce::MidiMessage::noteOff (1, 60), (int) (1.5 * sr) - pos);
            if (at (1.8))  m.addEvent (juce::MidiMessage::noteOn (1, 62, 0.9f), (int) (1.8 * sr) - pos);
            if (at (2.7))  m.addEvent (juce::MidiMessage::noteOff (1, 62), (int) (2.7 * sr) - pos);
            proc.processBlock (block, m);
            const int n = juce::jmin (512, all.getNumSamples() - pos);
            for (int ch = 0; ch < 2; ++ch) all.copyFrom (ch, pos, block, ch, 0, n);
        }
        const auto f = out.getChildFile (juce::String (categoryName (c)).replace (" ", "_") + ".wav");
        f.deleteFile();
        if (auto w = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (new juce::FileOutputStream (f), sr, 2, 24, {}, 0)))
            w->writeFromAudioSampleBuffer (all, 0, all.getNumSamples());
        std::printf ("%-11s %-14s crête %.2f\n", categoryName (c), proc.sampleName().toRawUTF8(), all.getMagnitude (0, all.getNumSamples()));
    }

    if (std::getenv ("BASS_NULL") != nullptr)
    {
        // Transparence : chaque sample joué sur sa note d'origine, à 44,1 kHz, sans tenue ; on écrit le rendu brut.
        auto dir = out.getChildFile ("null"); dir.createDirectory();
        const double sr44 = 44100;
        proc.setPlayConfigDetails (0, 2, sr44, 512);
        setP (proc, "punch", 0.f); setP (proc, "snap", 0.f); setP (proc, "detune", 0.f);
        setP (proc, "tone", 1.f); setP (proc, "drive", 0.f); setP (proc, "volume", 0.f); setP (proc, "decay", 10.f);
        for (int c = 0; c < numCategories; ++c)
            for (int idx = 0; idx < proc.bank.entries (c).size(); ++idx)
            {
                proc.selectSample (c, idx);
                proc.prepareToPlay (sr44, 512);
                const int root = juce::roundToInt (proc.bank.entries (c)[idx].rootNote);
                juce::AudioBuffer<float> all (2, (int) (sr44 * 4)), block (2, 512);
                all.clear();
                for (int pos = 0; pos < all.getNumSamples(); pos += 512)
                {
                    juce::MidiBuffer m;
                    if (pos == 0) m.addEvent (juce::MidiMessage::noteOn (1, root, 1.0f), 0);
                    proc.processBlock (block, m);
                    const int n = juce::jmin (512, all.getNumSamples() - pos);
                    for (int ch = 0; ch < 2; ++ch) all.copyFrom (ch, pos, block, ch, 0, n);
                }
                auto f = dir.getChildFile (juce::String (c) + "__" + proc.sampleName() + ".wav");
                f.deleteFile();
                if (auto w = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (new juce::FileOutputStream (f), sr44, 2, 32, {}, 0)))
                    w->writeFromAudioSampleBuffer (all, 0, all.getNumSamples());
            }
        return 0;
    }

    if (std::getenv ("BASS_HOLD") != nullptr)
    {
        // « vouuuu » : chaque sample tenu 6 s en mode Glide avec slides liés, puis relâché.
        auto dir = out.getChildFile ("hold"); dir.createDirectory();
        setP (proc, "mode", 2.f); setP (proc, "glide", 0.25f);
        setP (proc, "punch", 0.f); setP (proc, "snap", 0.f);
        for (int c = 0; c < numCategories; ++c)
            for (int idx = 0; idx < proc.bank.entries (c).size(); ++idx)
            {
                proc.selectSample (c, idx);
                proc.prepareToPlay (sr, 512);
                juce::AudioBuffer<float> all (2, (int) (sr * 7)), block (2, 512);
                all.clear();
                for (int pos = 0; pos < all.getNumSamples(); pos += 512)
                {
                    juce::MidiBuffer m;
                    auto ev = [&] (double t, const juce::MidiMessage& msg) { const int s0 = (int) (t * sr); if (s0 >= pos && s0 < pos + 512) m.addEvent (msg, s0 - pos); };
                    ev (0.01, juce::MidiMessage::noteOn (1, 60, 0.9f));
                    ev (1.5, juce::MidiMessage::noteOn (1, 67, 0.9f)); ev (1.6, juce::MidiMessage::noteOff (1, 60));
                    ev (3.0, juce::MidiMessage::noteOn (1, 58, 0.9f)); ev (3.1, juce::MidiMessage::noteOff (1, 67));
                    ev (6.0, juce::MidiMessage::noteOff (1, 58));
                    proc.processBlock (block, m);
                    const int n = juce::jmin (512, all.getNumSamples() - pos);
                    for (int ch = 0; ch < 2; ++ch) all.copyFrom (ch, pos, block, ch, 0, n);
                }
                auto f = dir.getChildFile (juce::String (c) + "__" + proc.sampleName() + ".wav");
                f.deleteFile();
                if (auto w = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (new juce::FileOutputStream (f), sr, 2, 24, {}, 0)))
                    w->writeFromAudioSampleBuffer (all, 0, all.getNumSamples());
            }
        return 0;
    }

    if (std::getenv ("BASS_SLIDE") != nullptr)
    {
        // Slides et notes rapides dans les 3 modes, sur un sample par catégorie. Notes à 0,5 s d'écart,
        // chacune lâchée 0,1 s après le début de la suivante (notes liées) sauf la dernière série (détachées).
        auto dir = out.getChildFile ("slide"); dir.createDirectory();
        setP (proc, "glide", 0.08f);
        const int seq[] { 60, 67, 63, 72, 60, 65, 58, 60 };
        for (int c = 0; c < numCategories; ++c)
            for (int mode = 0; mode < 3; ++mode)
            {
                proc.selectSample (c, 0);
                setP (proc, "mode", (float) mode);
                proc.prepareToPlay (sr, 512);
                juce::AudioBuffer<float> all (2, (int) (sr * 4.6)), block (2, 512);
                all.clear();
                for (int pos = 0; pos < all.getNumSamples(); pos += 512)
                {
                    juce::MidiBuffer m;
                    auto ev = [&] (double t, const juce::MidiMessage& msg) { const int s0 = (int) (t * sr); if (s0 >= pos && s0 < pos + 512) m.addEvent (msg, s0 - pos); };
                    for (int k = 0; k < 8; ++k)
                    {
                        const double t = 0.05 + 0.5 * k;
                        ev (t, juce::MidiMessage::noteOn (1, seq[k], 0.9f));
                        ev (k < 4 ? t + 0.6 : t + 0.3, juce::MidiMessage::noteOff (1, seq[k])); // 4 liées puis 4 détachées
                    }
                    proc.processBlock (block, m);
                    const int n = juce::jmin (512, all.getNumSamples() - pos);
                    for (int ch = 0; ch < 2; ++ch) all.copyFrom (ch, pos, block, ch, 0, n);
                }
                auto f = dir.getChildFile (juce::String (c) + "_mode" + juce::String (mode) + ".wav");
                f.deleteFile();
                if (auto w = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (new juce::FileOutputStream (f), sr, 2, 24, {}, 0)))
                    w->writeFromAudioSampleBuffer (all, 0, all.getNumSamples());
            }
        return 0;
    }

    if (std::getenv ("BASS_ALL") != nullptr)
    {
        setP (proc, "punch", 0.f); setP (proc, "snap", 0.f); setP (proc, "detune", 0.f);
        auto dir = out.getChildFile ("tous"); dir.createDirectory();
        for (int c = 0; c < numCategories; ++c)
            for (int idx = 0; idx < proc.bank.entries (c).size(); ++idx)
            {
                proc.selectSample (c, idx);
                proc.prepareToPlay (sr, 512);
                juce::AudioBuffer<float> all (2, (int) (sr * 3)), block (2, 512);
                all.clear();
                for (int pos = 0; pos < all.getNumSamples(); pos += 512)
                {
                    juce::MidiBuffer m;
                    auto ev = [&] (double t, const juce::MidiMessage& msg) { const int s0 = (int) (t * sr); if (s0 >= pos && s0 < pos + 512) m.addEvent (msg, s0 - pos); };
                    ev (0.01, juce::MidiMessage::noteOn (1, 60, 0.9f)); ev (1.4, juce::MidiMessage::noteOff (1, 60));
                    ev (1.5, juce::MidiMessage::noteOn (1, 62, 0.9f)); ev (2.9, juce::MidiMessage::noteOff (1, 62));
                    proc.processBlock (block, m);
                    const int n = juce::jmin (512, all.getNumSamples() - pos);
                    for (int ch = 0; ch < 2; ++ch) all.copyFrom (ch, pos, block, ch, 0, n);
                }
                auto f = dir.getChildFile (juce::String (c) + "__" + proc.sampleName() + ".wav");
                f.deleteFile();
                if (auto w = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (new juce::FileOutputStream (f), sr, 2, 24, {}, 0)))
                    w->writeFromAudioSampleBuffer (all, 0, all.getNumSamples());
            }
        return 0;
    }

    // captures de l'interface, une par catégorie
    for (int c = 0; c < numCategories; ++c)
    {
        proc.selectSample (c, c == Eight08 ? 4 : 0);
        Editor ed (proc);
        ed.setSize (1000, 660);
        for (int i = 0; i < 3; ++i) { juce::MessageManager::getInstance()->runDispatchLoopUntil (40); }
        auto img = ed.createComponentSnapshot (ed.getLocalBounds(), true, 1.5f);
        juce::FileOutputStream os (out.getChildFile ("ui_" + juce::String (c) + ".png"));
        os.setPosition (0); os.truncate();
        juce::PNGImageFormat().writeImageToStream (img, os);
    }
    {
        Editor ed (proc);
        ed.setSize (1000, 660);
        if (auto* s = ed.findChildWithID ("settings")) s->setVisible (true);
        for (auto* ch : ed.getChildren()) if (dynamic_cast<Settings*> (ch)) { ch->setVisible (true); ch->toFront (false); }
        juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
        auto img = ed.createComponentSnapshot (ed.getLocalBounds(), true, 1.5f);
        juce::FileOutputStream os (out.getChildFile ("ui_settings.png"));
        os.setPosition (0); os.truncate();
        juce::PNGImageFormat().writeImageToStream (img, os);
    }
    return 0;
}
