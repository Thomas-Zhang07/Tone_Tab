#pragma once

#include <juce_core/juce_core.h>
#include <atomic>

namespace i18n
{
    enum class Language { english, chinese };

    class Localisation
    {
    public:
        static Localisation& get();

        Language getLanguage() const noexcept   { return language.load (std::memory_order_relaxed); }
        void setLanguage (Language l) noexcept  { language.store (l, std::memory_order_relaxed); }

        juce::String text (const char* english, const char* chinese) const
        {
            return juce::String::fromUTF8 (getLanguage() == Language::chinese ? chinese : english);
        }

        // For legacy Chinese-only literals (parameter names, choice labels).
        // English mode looks the string up in the translation table; unknown strings
        // fall back to the original so nothing disappears silently.
        juce::String fromChinese (const char* chinese) const;

    private:
        std::atomic<Language> language { Language::english };
    };
}

// Inline bilingual string. English first, Chinese second, so a call site reads
// naturally in the default language and keeps the translation next to it.
inline juce::String TR (const char* english, const char* chinese)
{
    return i18n::Localisation::get().text (english, chinese);
}

// Localise a legacy Chinese-only label (parameter names, choice items).
inline juce::String L (const char* chinese)
{
    return i18n::Localisation::get().fromChinese (chinese);
}
