#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Util.h"

namespace
{
    class JobThread final : public juce::Thread
    {
    public:
        explicit JobThread (std::function<void()> f)
            : juce::Thread ("BeatDrop job"), fn (std::move (f))
        {
            startThread();
        }

        ~JobThread() override { stopThread (20000); }

        void run() override { fn(); }

    private:
        std::function<void()> fn;
    };

    constexpr int kNumPeaks = 1024;
}

//==============================================================================
BeatDropProcessor::BeatDropProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    addParameter (level = new juce::AudioParameterFloat (
        juce::ParameterID { "level", 1 }, "Level",
        juce::NormalisableRange<float> (-24.0f, 6.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));
}

BeatDropProcessor::~BeatDropProcessor()
{
    ++generation;   // laufende Jobs sehen "abgebrochen" und beenden yt-dlp

    const juce::ScopedLock sl (workersLock);
    workers.clear();   // JobThread::~JobThread wartet auf das Ende
}

bool BeatDropProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

juce::AudioProcessorEditor* BeatDropProcessor::createEditor()
{
    return new BeatDropEditor (*this);
}

//==============================================================================
void BeatDropProcessor::prepareToPlay (double sampleRate, int)
{
    hostRate.store (sampleRate);

    std::shared_ptr<const Beat> current;
    double currentRate = 0.0;

    {
        const juce::ScopedLock sl (stateLock);
        current = beat;
        currentRate = playRate;
    }

    if (current != nullptr && std::abs (currentRate - sampleRate) > 0.5)
    {
        auto play = makePlayAudio (*current, sampleRate);

        {
            const juce::ScopedLock sl (stateLock);
            playRate = sampleRate;
        }

        swapPlayAudio (std::move (play));
    }

    lastGain = juce::Decibels::decibelsToGain (level->get());
    wasActive = false;
}

void BeatDropProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    const int n = buffer.getNumSamples();
    const double sr = hostRate.load();

    // Transport des Hosts (FL Studio liefert Position und Play-Status)
    bool hostPlaying = false;
    juce::int64 hostPos = 0;

    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            hostPlaying = pos->getIsPlaying();

            if (auto s = pos->getTimeInSamples())
                hostPos = *s;
            else if (auto t = pos->getTimeInSeconds())
                hostPos = (juce::int64) (*t * sr);
        }
    }

    hostPlayingFlag.store (hostPlaying);

    const double seek = seekRequest.exchange (-1.0);

    if (seek >= 0.0)
        freePos = seek * sr;

    const juce::SpinLock::ScopedTryLockType lock (audioLock);

    if (! lock.isLocked() || playAudio == nullptr)
    {
        wasActive = false;
        return;
    }

    const int total = playAudio->getNumSamples();

    bool active = false;
    juce::int64 start = 0;

    if (hostPlaying)
    {
        // Host laeuft: Beat sitzt sample-genau auf der Timeline (Position 0 = Beat-Anfang)
        start = hostPos;
        freePos = (double) (hostPos + n);
        active = true;
    }
    else if (userPlaying.load())
    {
        // Vorhoeren im Plugin selbst
        start = (juce::int64) freePos;
        freePos += (double) n;
        active = true;

        if (start >= total)
        {
            userPlaying.store (false);
            freePos = 0.0;
            active = false;
        }
    }

    const int fade = juce::jmin (n, 128);

    auto render = [&] (int destStart, int count, juce::int64 srcStart, float gainFrom, float gainTo)
    {
        if (count <= 0)
            return;

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const float* src = playAudio->getReadPointer (juce::jmin (ch, playAudio->getNumChannels() - 1));
            float* dst = buffer.getWritePointer (ch);

            for (int i = 0; i < count; ++i)
            {
                const juce::int64 s = srcStart + i;

                if (s < 0 || s >= total)
                    continue;

                const float g = gainFrom + (gainTo - gainFrom) * ((float) i / (float) count);
                dst[destStart + i] = src[s] * g;
            }
        }
    };

    if (active)
    {
        if (wasActive && start == expectedNext)
        {
            render (0, n, start, 1.0f, 1.0f);
        }
        else
        {
            // Start oder Sprung: kurz einblenden, damit es nicht knackt
            render (0, fade, start, 0.0f, 1.0f);
            render (fade, n - fade, start + fade, 1.0f, 1.0f);
        }

        expectedNext = start + n;
    }
    else if (wasActive)
    {
        // Stopp: kurz ausblenden
        render (0, fade, expectedNext, 1.0f, 0.0f);
    }

    wasActive = active;

    const float target = juce::Decibels::decibelsToGain (level->get());
    buffer.applyGainRamp (0, n, lastGain, target);
    lastGain = target;

    uiPositionSeconds.store ((hostPlaying ? (double) hostPos : freePos) / sr);
}

//==============================================================================
void BeatDropProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::XmlElement xml ("BeatDrop");
    xml.setAttribute ("level", (double) level->get());

    if (auto b = getBeat())
        xml.setAttribute ("file", b->file.getFullPathName());

    copyXmlToBinary (xml, dest);
}

void BeatDropProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        if (! xml->hasTagName ("BeatDrop"))
            return;

        level->setValueNotifyingHost (level->convertTo0to1 ((float) xml->getDoubleAttribute ("level", 0.0)));

        const juce::File f (xml->getStringAttribute ("file"));

        if (f.existsAsFile())
            loadLocalFile (f);
    }
}

//==============================================================================
JobStatus BeatDropProcessor::getStatus() const
{
    const juce::ScopedLock sl (stateLock);
    return status;
}

juce::Array<SearchHit> BeatDropProcessor::getResults() const
{
    const juce::ScopedLock sl (stateLock);
    return results;
}

std::shared_ptr<const Beat> BeatDropProcessor::getBeat() const
{
    const juce::ScopedLock sl (stateLock);
    return beat;
}

void BeatDropProcessor::setStatus (int gen, JobStatus::Kind kind, float progress, const juce::String& message)
{
    const juce::ScopedLock sl (stateLock);

    if (generation.load() != gen)
        return;

    status.kind = kind;
    status.progress = progress;
    status.message = message;
}

void BeatDropProcessor::clearResults()
{
    {
        const juce::ScopedLock sl (stateLock);
        results.clear();
    }

    ++resultsVersion;
}

void BeatDropProcessor::launch (std::function<void()> job)
{
    const juce::ScopedLock sl (workersLock);

    workers.erase (std::remove_if (workers.begin(), workers.end(),
                                   [] (const std::unique_ptr<juce::Thread>& w) { return ! w->isThreadRunning(); }),
                   workers.end());

    workers.push_back (std::make_unique<JobThread> (std::move (job)));
}

//==============================================================================
void BeatDropProcessor::startSearch (const juce::String& query)
{
    clearResults();

    const int gen = ++generation;
    setStatus (gen, JobStatus::Kind::searching, 0.0f, T ("Suche ..."));

    launch ([this, gen, query]
    {
        auto cancel = [this, gen] { return ! isCurrent (gen); };

        juce::String error;
        auto hits = YtDlpClient::search (query, 6, error, cancel);

        if (cancel())
            return;

        if (hits.isEmpty())
        {
            setStatus (gen, JobStatus::Kind::failed, 0.0f, error.isNotEmpty() ? error : T ("Nichts gefunden"));
            return;
        }

        {
            const juce::ScopedLock sl (stateLock);
            results = hits;
        }

        ++resultsVersion;
        setStatus (gen, JobStatus::Kind::idle, 0.0f, T ("Beat anklicken zum Laden"));
    });
}

void BeatDropProcessor::startDownload (const juce::String& url)
{
    clearResults();

    const int gen = ++generation;
    setStatus (gen, JobStatus::Kind::downloading, 0.0f, T ("Beat wird geladen ..."));

    launch ([this, gen, url] { downloadJob (gen, url); });
}

void BeatDropProcessor::loadLocalFile (const juce::File& file)
{
    const int gen = ++generation;
    setStatus (gen, JobStatus::Kind::loading, 1.0f, T ("Audio wird vorbereitet ..."));

    launch ([this, gen, file] { loadFileInJob (gen, file, false); });
}

void BeatDropProcessor::downloadJob (int gen, juce::String url)
{
    auto cancel = [this, gen] { return ! isCurrent (gen); };
    auto progress = [this, gen] (float p) { setStatus (gen, JobStatus::Kind::downloading, p, T ("Beat wird geladen ...")); };

    auto result = YtDlpClient::download (url, progress, cancel);

    if (cancel())
        return;

    // YouTube aendert sich staendig: einmal pro Sitzung yt-dlp aktualisieren und neu versuchen
    if (! result.file.existsAsFile() && ! result.setupProblem && ! updateTried.exchange (true))
    {
        setStatus (gen, JobStatus::Kind::downloading, 0.0f, T ("yt-dlp wird aktualisiert ..."));
        YtDlpClient::selfUpdate (cancel);

        if (cancel())
            return;

        setStatus (gen, JobStatus::Kind::downloading, 0.0f, T ("Beat wird geladen ..."));
        result = YtDlpClient::download (url, progress, cancel);

        if (cancel())
            return;
    }

    if (! result.file.existsAsFile())
    {
        setStatus (gen, JobStatus::Kind::failed, 0.0f,
                   result.error.isNotEmpty() ? result.error : T ("Download fehlgeschlagen"));
        return;
    }

    loadFileInJob (gen, result.file, true);
}

void BeatDropProcessor::loadFileInJob (int gen, const juce::File& file, bool autoplay)
{
    setStatus (gen, JobStatus::Kind::loading, 1.0f, T ("Audio wird vorbereitet ..."));

    auto b = decodeBeat (file);

    if (b == nullptr)
    {
        setStatus (gen, JobStatus::Kind::failed, 0.0f, T ("Audiodatei konnte nicht gelesen werden"));
        return;
    }

    const double rate = hostRate.load();
    auto play = makePlayAudio (*b, rate);

    if (! isCurrent (gen))
        return;

    juce::MessageManager::callAsync ([weak = juce::WeakReference<BeatDropProcessor> (this), gen, b, play, rate, autoplay]
    {
        if (auto* self = weak.get())
            self->finishLoad (gen, b, play, rate, autoplay);
    });
}

void BeatDropProcessor::finishLoad (int gen, std::shared_ptr<Beat> newBeat,
                                    std::shared_ptr<juce::AudioBuffer<float>> play, double rate, bool autoplay)
{
    if (! isCurrent (gen))
        return;

    // Host-Samplerate hat sich waehrend des Ladens geaendert
    if (std::abs (rate - hostRate.load()) > 0.5)
    {
        rate = hostRate.load();
        play = makePlayAudio (*newBeat, rate);
    }

    {
        const juce::ScopedLock sl (stateLock);
        beat = newBeat;
        playRate = rate;
        status.kind = JobStatus::Kind::ready;
        status.progress = 1.0f;
        status.message = newBeat->title;
    }

    swapPlayAudio (std::move (play));

    seekRequest.store (0.0);
    userPlaying.store (autoplay && ! hostPlayingFlag.load());
}

void BeatDropProcessor::swapPlayAudio (std::shared_ptr<juce::AudioBuffer<float>> newPlay)
{
    {
        const juce::SpinLock::ScopedLockType sl (audioLock);
        playAudio.swap (newPlay);
    }

    // newPlay haelt jetzt den alten Puffer und wird hier ausserhalb des Locks freigegeben
}

//==============================================================================
std::shared_ptr<Beat> BeatDropProcessor::decodeBeat (const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
        return nullptr;

    const juce::int64 maxLength = (juce::int64) (reader->sampleRate * 60.0 * 20.0);   // harte Grenze: 20 Minuten

    if (reader->lengthInSamples > maxLength)
        return nullptr;

    const int length = (int) reader->lengthInSamples;

    auto audio = std::make_shared<juce::AudioBuffer<float>> (2, length);

    if (! reader->read (audio.get(), 0, length, 0, true, true))
        return nullptr;

    if (reader->numChannels == 1)
        audio->copyFrom (1, 0, *audio, 0, 0, length);

    auto b = std::make_shared<Beat>();
    b->file = file;
    b->sampleRate = reader->sampleRate;
    b->audio = audio;

    // Titel: Dateiname ohne " [videoId]"
    auto name = file.getFileNameWithoutExtension();

    if (name.endsWithChar (']'))
    {
        const int i = name.lastIndexOfChar ('[');

        if (i > 0)
            name = name.substring (0, i).trim();
    }

    b->title = name;

    // Peaks fuer die Wellenform
    b->peaks.assign ((size_t) kNumPeaks, 0.0f);

    const juce::int64 perBin = juce::jmax<juce::int64> (1, (juce::int64) length / kNumPeaks);

    for (int ch = 0; ch < 2; ++ch)
    {
        const float* d = audio->getReadPointer (ch);

        for (int i = 0; i < length; ++i)
        {
            const int bin = (int) juce::jmin<juce::int64> (kNumPeaks - 1, (juce::int64) i / perBin);
            b->peaks[(size_t) bin] = juce::jmax (b->peaks[(size_t) bin], std::abs (d[i]));
        }
    }

    const float maxPeak = *std::max_element (b->peaks.begin(), b->peaks.end());

    if (maxPeak > 0.0001f)
        for (auto& p : b->peaks)
            p /= maxPeak;

    return b;
}

std::shared_ptr<juce::AudioBuffer<float>> BeatDropProcessor::makePlayAudio (const Beat& b, double rate)
{
    if (b.audio == nullptr || rate <= 0.0)
        return nullptr;

    if (std::abs (b.sampleRate - rate) < 0.5)
        return b.audio;

    const double ratio = b.sampleRate / rate;
    const int inLength = b.audio->getNumSamples();
    const int outLength = (int) std::ceil ((double) inLength / ratio);

    auto out = std::make_shared<juce::AudioBuffer<float>> (2, outLength);

    // Eingang mit etwas Null-Padding, damit der Interpolator nie ueber das Ende liest
    std::vector<float> padded ((size_t) inLength + 32, 0.0f);

    // Der Lagrange-Interpolator verzoegert um 2 Samples; die werden vorne abgeschnitten,
    // damit der Beat auch nach dem Resampling sample-genau auf der Timeline liegt.
    constexpr int latency = 2;
    std::vector<float> tmp ((size_t) (outLength + latency));

    for (int ch = 0; ch < 2; ++ch)
    {
        std::copy (b.audio->getReadPointer (ch), b.audio->getReadPointer (ch) + inLength, padded.begin());

        juce::LagrangeInterpolator interpolator;
        interpolator.process (ratio, padded.data(), tmp.data(), outLength + latency);

        std::copy (tmp.begin() + latency, tmp.end(), out->getWritePointer (ch));
    }

    return out;
}

//==============================================================================
void BeatDropProcessor::togglePlay()
{
    userPlaying.store (! userPlaying.load());
}

void BeatDropProcessor::stopAndRewind()
{
    userPlaying.store (false);
    seekRequest.store (0.0);
}

void BeatDropProcessor::seekToFraction (double fraction)
{
    if (auto b = getBeat())
        seekRequest.store (juce::jlimit (0.0, 1.0, fraction) * b->getSeconds());
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BeatDropProcessor();
}
