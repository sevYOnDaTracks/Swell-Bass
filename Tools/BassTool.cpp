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
        // rendu : C1 tenu 1,5 s puis relâché, puis G1
        juce::AudioBuffer<float> all (2, (int) (sr * 3));
        all.clear();
        juce::AudioBuffer<float> block (2, 512);
        for (int pos = 0; pos < all.getNumSamples(); pos += 512)
        {
            juce::MidiBuffer m;
            auto at = [&] (double t) { return (int) (t * sr) >= pos && (int) (t * sr) < pos + 512; };
            if (at (0.01)) m.addEvent (juce::MidiMessage::noteOn (1, 24, 0.9f), (int) (0.01 * sr) - pos);
            if (at (1.5))  m.addEvent (juce::MidiMessage::noteOff (1, 24), (int) (1.5 * sr) - pos);
            if (at (1.8))  m.addEvent (juce::MidiMessage::noteOn (1, 31, 0.9f), (int) (1.8 * sr) - pos);
            if (at (2.7))  m.addEvent (juce::MidiMessage::noteOff (1, 31), (int) (2.7 * sr) - pos);
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
