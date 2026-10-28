#pragma once

#include <juce_core/juce_core.h>
#include "i18n/Localisation.h"

// UTF-8 literal -> juce::String. Use this for content (XML, file names, data),
// never for user-facing text; use TR() for anything the user reads.
inline juce::String T (const char* utf8) { return juce::String::fromUTF8 (utf8); }
