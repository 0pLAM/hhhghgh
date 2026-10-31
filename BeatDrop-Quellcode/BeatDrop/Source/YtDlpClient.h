#pragma once

#include <juce_core/juce_core.h>
#include <functional>

struct SearchHit
{
    juce::String id, title, channel, duration;

    juce::String url() const { return "https://www.youtube.com/watch?v=" + id; }
};

struct DownloadResult
{
    juce::File file;
    juce::String error;
    bool setupProblem = false;   // yt-dlp / ffmpeg fehlen -> Retry/Update bringt nichts
};

// Duenner Wrapper um das yt-dlp-Kommandozeilenprogramm.
// Alle Funktionen blockieren und gehoeren auf einen Hintergrund-Thread.
namespace YtDlpClient
{
    using CancelFn   = std::function<bool()>;
    using ProgressFn = std::function<void (float)>;   // 0..1

    juce::File getToolsFolder();    // dort legt setup-windows.ps1 yt-dlp, ffmpeg und deno ab
    juce::File getCacheFolder();    // dort landen die fertigen WAV-Dateien (Musik/BeatDrop)
    juce::File findTool (const juce::String& baseName);

    bool looksLikeUrl (const juce::String& text);

    juce::Array<SearchHit> search (const juce::String& query, int maxResults, juce::String& error, const CancelFn& cancel);
    DownloadResult download (const juce::String& url, const ProgressFn& progress, const CancelFn& cancel);
    void selfUpdate (const CancelFn& cancel);
}
