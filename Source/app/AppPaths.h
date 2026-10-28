#pragma once

#include <juce_core/juce_core.h>

// Central filesystem layout. Every path the app owns is ASCII and derived here;
// no UI or DSP code should build a path by hand.
namespace AppPaths
{
    // Folder containing Tone_Tab.exe (standalone) or the plugin module (plugin).
    juce::File moduleDir();

    // %APPDATA%/Tone_Tab
    juce::File userDataDir();

    // Documents/Tone_Tab
    juce::File documentsDir();

    // Explicit library root (set from the library.root setting).
    void setLibraryRoot (const juce::File& root);
    void clearLibraryRoot();

    // <app>/assets, the repository's assets/ during development, or an override.
    juce::File libraryDir();
    juce::File modelsDir();      // libraryDir()/nam
    juce::File irDir();          // libraryDir()/ir
    juce::File gervanaDir();     // libraryDir()/gervana

    // User data.
    juce::File presetsDir();     // documentsDir()/presets

    // Recordings folder (default documentsDir()/recordings, or an override
    // chosen in the Record view / recording.root setting).
    void setRecordingsRoot (const juce::File& root);
    void clearRecordingsRoot();
    juce::File recordingsDir();
    juce::File tabsDir();        // <app>/scores when writable, else documentsDir()/scores
    juce::File settingsFile();   // userDataDir()/settings.xml
    juce::File practiceLogFile();// userDataDir()/practice_log.json

    // Shipped defaults (next to the executable, or the repo during development).
    juce::File defaultSettingsDir();  // <app>/defaults
    juce::File defaultPresetsDir();   // <app>/presets

    // One-time import sources. Read-only; never written to.
    juce::File legacyUserDataDir();   // %APPDATA%/LianqinHost
    juce::File legacyDocumentsDir();  // Documents/LianqinHost

    // @app/... portable paths.
    juce::String makePortable (const juce::File& file);
    juce::File resolvePortable (const juce::String& path);
}
