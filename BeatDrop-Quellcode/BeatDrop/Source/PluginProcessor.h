#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "YtDlpClient.h"

// Ein geladener Beat: Originaldaten + Wellenform-Peaks fuer die Anzeige
struct Beat
{
    juce::File file;
    juce::String title;
    std::shared_ptr<juce::AudioBuffer<float>> audio;   // in der Original-Samplerate der Datei
    double sampleRate = 44100.0;
    std::vector<float> peaks;                           // 0..1, feste Aufloesung

    double getSeconds() const noexcept
    {
        return (audio != nullptr && sampleRate > 0.0) ? (double) audio->getNumSamples() / sampleRate : 0.0;
    }
};

struct JobStatus
{
    enum class Kind { idle, searching, downloading, loading, ready, failed };

    Kind kind = Kind::idle;
    float progress = 0.0f;
    juce::String message;

    bool isBusy() const noexcept
    {
        return kind == Kind::searching || kind == Kind::downloading || kind == Kind::loading;
    }
};

class BeatDropProcessor final : public juce::AudioProcessor
{
public:
    BeatDropProcessor();
    ~BeatDropProcessor() override;

    // ---- AudioProcessor ----
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "BeatDrop"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- API fuer den Editor (Message-Thread) ----
    void startSearch (const juce::String& query);
    void startDownload (const juce::String& url);
    void loadLocalFile (const juce::File&);
    void clearResults();

    JobStatus getStatus() const;
    juce::Array<SearchHit> getResults() const;
    int getResultsVersion() const noexcept { return resultsVersion.load(); }
    std::shared_ptr<const Beat> getBeat() const;

    void togglePlay();
    void stopAndRewind();
    void seekToFraction (double fraction);

    bool isUserPlaying() const noexcept { return userPlaying.load(); }
    bool isHostPlaying() const noexcept { return hostPlayingFlag.load(); }
    double getPositionSeconds() const noexcept { return uiPositionSeconds.load(); }

    juce::AudioParameterFloat* level = nullptr;

private:
    void launch (std::function<void()> job);
    void setStatus (int gen, JobStatus::Kind kind, float progress, const juce::String& message);
    bool isCurrent (int gen) const noexcept { return generation.load() == gen; }

    void downloadJob (int gen, juce::String url);
    void loadFileInJob (int gen, const juce::File& file, bool autoplay);
    void finishLoad (int gen, std::shared_ptr<Beat> beat, std::shared_ptr<juce::AudioBuffer<float>> play,
                     double playRate, bool autoplay);
    void swapPlayAudio (std::shared_ptr<juce::AudioBuffer<float>> newPlay);

    static std::shared_ptr<Beat> decodeBeat (const juce::File& file);
    static std::shared_ptr<juce::AudioBuffer<float>> makePlayAudio (const Beat& beat, double rate);

    // Zustand, der zwischen UI und Worker-Threads geteilt wird
    mutable juce::CriticalSection stateLock;
    JobStatus status;
    juce::Array<SearchHit> results;
    std::shared_ptr<const Beat> beat;
    double playRate = 0.0;
    std::atomic<int> resultsVersion { 0 };

    // Hintergrund-Jobs (yt-dlp laeuft in eigenen Threads, nie im Audio-Thread)
    std::atomic<int> generation { 0 };
    std::atomic<bool> updateTried { false };
    juce::CriticalSection workersLock;
    std::vector<std::unique_ptr<juce::Thread>> workers;

    // Audio-Thread
    juce::SpinLock audioLock;
    std::shared_ptr<juce::AudioBuffer<float>> playAudio;   // in der Host-Samplerate
    std::atomic<double> hostRate { 44100.0 };
    std::atomic<bool> userPlaying { false };
    std::atomic<bool> hostPlayingFlag { false };
    std::atomic<double> seekRequest { -1.0 };
    std::atomic<double> uiPositionSeconds { 0.0 };
    double freePos = 0.0;
    juce::int64 expectedNext = 0;
    bool wasActive = false;
    float lastGain = 1.0f;

    JUCE_DECLARE_WEAK_REFERENCEABLE (BeatDropProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BeatDropProcessor)
};
