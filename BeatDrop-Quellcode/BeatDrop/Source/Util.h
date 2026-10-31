#pragma once

#include <juce_core/juce_core.h>

// Quelltext ist UTF-8, juce::String braucht fuer Umlaute explizit fromUTF8.
inline juce::String T (const char* utf8)
{
    return juce::String::fromUTF8 (utf8);
}

inline juce::String formatTime (double seconds)
{
    const int total = juce::jmax (0, (int) seconds);
    return juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
}
