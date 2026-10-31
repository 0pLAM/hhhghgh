#include "YtDlpClient.h"
#include "Util.h"

namespace YtDlpClient
{
namespace
{
    constexpr int kMaxMinutes = 15;

   #if JUCE_WINDOWS
    constexpr const char* kPathSeparator = ";";
   #else
    constexpr const char* kPathSeparator = ":";
   #endif

    juce::String exeName (const juce::String& base)
    {
       #if JUCE_WINDOWS
        return base + ".exe";
       #else
        return base;
       #endif
    }

    juce::StringArray split (const juce::String& s, const juce::String& sep)
    {
        juce::StringArray out;
        int start = 0;

        for (;;)
        {
            const int i = s.indexOf (start, sep);

            if (i < 0)
            {
                out.add (s.substring (start));
                break;
            }

            out.add (s.substring (start, i));
            start = i + sep.length();
        }

        return out;
    }

    // Startet einen Prozess und gibt jede Ausgabezeile (stdout + stderr) an onLine.
    // false = konnte nicht gestartet werden oder wurde abgebrochen.
    bool runProcess (const juce::StringArray& args,
                     const std::function<void (const juce::String&)>& onLine,
                     const CancelFn& cancel)
    {
        juce::ChildProcess child;

        if (! child.start (args, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
            return false;

        std::string pending;
        char buffer[2048];

        auto flushLines = [&] (bool everything)
        {
            for (;;)
            {
                const auto pos = pending.find ('\n');

                if (pos == std::string::npos)
                    break;

                std::string bytes = pending.substr (0, pos);
                pending.erase (0, pos + 1);

                if (! bytes.empty() && bytes.back() == '\r')
                    bytes.pop_back();

                onLine (juce::String::fromUTF8 (bytes.data(), (int) bytes.size()));
            }

            if (everything && ! pending.empty())
            {
                onLine (juce::String::fromUTF8 (pending.data(), (int) pending.size()));
                pending.clear();
            }
        };

        for (;;)
        {
            if (cancel && cancel())
            {
                child.kill();
                return false;
            }

            const int n = child.readProcessOutput (buffer, (int) sizeof (buffer));

            if (n <= 0)
                break;

            pending.append (buffer, (size_t) n);
            flushLines (false);
        }

        flushLines (true);
        child.waitForProcessToFinish (5000);

        return ! (cancel && cancel());
    }

    // ffmpeg- und Deno-Pfade mitgeben, falls sie nicht im PATH liegen
    void addToolArgs (juce::StringArray& args, const juce::File& ffmpeg)
    {
        if (ffmpeg.existsAsFile())
        {
            args.add ("--ffmpeg-location");
            args.add (ffmpeg.getParentDirectory().getFullPathName());
        }

        const auto deno = findTool ("deno");

        if (deno.existsAsFile())
        {
            args.add ("--js-runtimes");
            args.add ("deno:" + deno.getFullPathName());
        }
    }

    juce::String shortenError (juce::String e)
    {
        e = e.fromFirstOccurrenceOf ("ERROR:", false, false).trim();
        e = e.replace ("[youtube] ", "");

        if (e.containsIgnoreCase ("not a bot") || e.containsIgnoreCase ("Sign in to confirm"))
            return T ("YouTube verlangt gerade eine Anmeldung (Bot-Check). Sp\xc3\xa4ter nochmal versuchen.");

        if (e.containsIgnoreCase ("Video unavailable") || e.containsIgnoreCase ("Private video"))
            return T ("Video ist nicht verf\xc3\xbcgbar.");

        if (e.containsIgnoreCase ("Requested format is not available"))
            return T ("Kein Audioformat gefunden. yt-dlp und Deno aktualisieren (setup-windows.ps1 erneut ausf\xc3\xbchren).");

        if (e.length() > 140)
            e = e.substring (0, 140) + "...";

        return e.isEmpty() ? T ("Download fehlgeschlagen") : e;
    }
}

juce::File getToolsFolder()
{
    auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);

   #if JUCE_MAC
    base = base.getChildFile ("Application Support");
   #endif

    return base.getChildFile ("BeatDrop").getChildFile ("bin");
}

juce::File getCacheFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("BeatDrop");
}

juce::File findTool (const juce::String& baseName)
{
    const auto name = exeName (baseName);

    const auto local = getToolsFolder().getChildFile (name);

    if (local.existsAsFile())
        return local;

    juce::StringArray dirs;
    dirs.addTokens (juce::SystemStats::getEnvironmentVariable ("PATH", {}), kPathSeparator, "\"");

   #if ! JUCE_WINDOWS
    dirs.add ("/opt/homebrew/bin");
    dirs.add ("/usr/local/bin");
    dirs.add ("/usr/bin");
   #endif

    for (const auto& d : dirs)
    {
        if (d.isEmpty())
            continue;

        const auto f = juce::File (d).getChildFile (name);

        if (f.existsAsFile())
            return f;
    }

    return {};
}

bool looksLikeUrl (const juce::String& text)
{
    const auto s = text.trim().toLowerCase();

    return s.startsWith ("http://") || s.startsWith ("https://") || s.startsWith ("www.")
        || s.startsWith ("youtu.be/") || s.startsWith ("youtube.com/") || s.startsWith ("m.youtube.com/")
        || s.startsWith ("music.youtube.com/");
}

juce::Array<SearchHit> search (const juce::String& query, int maxResults, juce::String& error, const CancelFn& cancel)
{
    juce::Array<SearchHit> hits;

    const auto ytdlp = findTool ("yt-dlp");

    if (! ytdlp.existsAsFile())
    {
        error = T ("yt-dlp fehlt - bitte setup-windows.ps1 ausf\xc3\xbchren");
        return hits;
    }

    juce::StringArray args { ytdlp.getFullPathName(),
                             "--ignore-config", "--no-warnings", "--encoding", "utf-8",
                             "--flat-playlist", "--skip-download", "--ignore-errors",
                             "--print", "BDROP|||%(id)s|||%(title)s|||%(uploader)s|||%(duration)s",
                             "ytsearch" + juce::String (maxResults) + ":" + query.trim() };

    juce::String lastError;

    const bool ok = runProcess (args, [&] (const juce::String& line)
    {
        if (line.startsWith ("BDROP|||"))
        {
            const auto parts = split (line, "|||");

            if (parts.size() < 5 || parts[1].isEmpty())
                return;

            SearchHit h;
            h.id      = parts[1];
            h.title   = parts[2];
            h.channel = parts[3] == "NA" ? juce::String() : parts[3];

            const double secs = parts[4].getDoubleValue();
            h.duration = secs > 0.0 ? formatTime (secs) : juce::String();

            hits.add (h);
        }
        else if (line.startsWith ("ERROR"))
        {
            lastError = line;
        }
    }, cancel);

    if (! ok)
    {
        if (! (cancel && cancel()))
            error = T ("Suche konnte nicht gestartet werden");

        return {};
    }

    if (hits.isEmpty())
        error = lastError.isNotEmpty() ? shortenError (lastError) : T ("Nichts gefunden");

    return hits;
}

DownloadResult download (const juce::String& url, const ProgressFn& progress, const CancelFn& cancel)
{
    DownloadResult r;

    const auto ytdlp  = findTool ("yt-dlp");
    const auto ffmpeg = findTool ("ffmpeg");

    if (! ytdlp.existsAsFile())
    {
        r.error = T ("yt-dlp fehlt - bitte setup-windows.ps1 ausf\xc3\xbchren");
        r.setupProblem = true;
        return r;
    }

    if (! ffmpeg.existsAsFile())
    {
        r.error = T ("ffmpeg fehlt - bitte setup-windows.ps1 ausf\xc3\xbchren");
        r.setupProblem = true;
        return r;
    }

    auto target = url.trim();

    if (! target.startsWithIgnoreCase ("http"))
        target = "https://" + target;

    const auto cache = getCacheFolder();
    cache.createDirectory();

    const auto startTime = juce::Time::getCurrentTime();

    juce::StringArray args { ytdlp.getFullPathName(),
                             "--ignore-config", "--no-warnings", "--encoding", "utf-8",
                             "--no-playlist",
                             "--match-filters", "!is_live & duration<=?" + juce::String (kMaxMinutes * 60),
                             "-f", "bestaudio/best",
                             "-x", "--audio-format", "wav",
                             "--windows-filenames", "--trim-filenames", "90",
                             "-P", cache.getFullPathName(),
                             "-o", "%(title)s [%(id)s].%(ext)s",
                             "--no-simulate", "--progress", "--newline",
                             "--progress-template", "download:BDPROG|||%(progress._percent_str)s",
                             "--print", "after_move:BDFILE|||%(filepath)s" };

    addToolArgs (args, ffmpeg);
    args.add ("--");
    args.add (target);

    juce::String lastError;
    bool filtered = false;

    const bool ok = runProcess (args, [&] (const juce::String& line)
    {
        if (line.startsWith ("BDPROG|||"))
        {
            const auto v = line.fromFirstOccurrenceOf ("|||", false, false).retainCharacters ("0123456789.");

            if (progress && v.isNotEmpty())
                progress (juce::jlimit (0.0f, 1.0f, v.getFloatValue() / 100.0f));
        }
        else if (line.startsWith ("BDFILE|||"))
        {
            r.file = juce::File (line.fromFirstOccurrenceOf ("|||", false, false).trim());
        }
        else if (line.containsIgnoreCase ("does not pass filter"))
        {
            filtered = true;
        }
        else if (line.startsWith ("ERROR"))
        {
            lastError = line;
        }
    }, cancel);

    if (! ok)
    {
        if (! (cancel && cancel()))
            r.error = T ("Download konnte nicht gestartet werden");

        return r;
    }

    if (! r.file.existsAsFile())
    {
        // Fallback: neueste frisch geschriebene WAV im Cache
        juce::File newest;
        juce::Time newestTime;

        for (const auto& f : cache.findChildFiles (juce::File::findFiles, false, "*.wav"))
        {
            const auto t = f.getLastModificationTime();

            if (t >= startTime - juce::RelativeTime::seconds (3) && (newest == juce::File() || t > newestTime))
            {
                newest = f;
                newestTime = t;
            }
        }

        r.file = newest;
    }

    if (! r.file.existsAsFile())
    {
        if (filtered)
            r.error = T ("Video ist ein Livestream oder l\xc3\xa4nger als ") + juce::String (kMaxMinutes) + " Min.";
        else
            r.error = shortenError (lastError);

        r.setupProblem = lastError.containsIgnoreCase ("ffprobe") || lastError.containsIgnoreCase ("ffmpeg not found");
    }

    return r;
}

void selfUpdate (const CancelFn& cancel)
{
    const auto ytdlp = findTool ("yt-dlp");

    if (ytdlp.existsAsFile())
        runProcess ({ ytdlp.getFullPathName(), "--ignore-config", "-U" }, [] (const juce::String&) {}, cancel);
}

} // namespace YtDlpClient
