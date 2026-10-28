#include "AppPaths.h"

namespace
{
    juce::File libraryRootOverride;

    // Look for a folder next to the module, then walk up a few levels so that a
    // development build (build/Tone_Tab_artefacts/Release/Tone_Tab.exe) still
    // finds the repository's assets/, presets/, and defaults/.
    juce::File findNearModule (const juce::String& child)
    {
        auto dir = AppPaths::moduleDir();
        for (int i = 0; i < 6; ++i)
        {
            const auto candidate = dir.getChildFile (child);
            if (candidate.exists())
                return candidate;

            if (dir.isRoot())
                break;
            dir = dir.getParentDirectory();
        }
        return {};
    }
}

namespace AppPaths
{
    juce::File moduleDir()
    {
        return juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                   .getParentDirectory();
    }

    juce::File userDataDir()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Tone_Tab");
    }

    juce::File documentsDir()
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("Tone_Tab");
    }

    void setLibraryRoot (const juce::File& root)   { libraryRootOverride = root; }
    void clearLibraryRoot()                        { libraryRootOverride = {}; }

    juce::File libraryDir()
    {
        if (libraryRootOverride.isDirectory())
            return libraryRootOverride;

        if (const auto found = findNearModule ("assets"); found.isDirectory())
            return found;

        // Not installed yet: use a writable per-user fallback.
        return userDataDir().getChildFile ("assets");
    }

    juce::File modelsDir()  { return libraryDir().getChildFile ("nam"); }
    juce::File irDir()      { return libraryDir().getChildFile ("ir"); }
    juce::File gervanaDir() { return libraryDir().getChildFile ("gervana"); }

    juce::File presetsDir()    { return documentsDir().getChildFile ("presets"); }
    juce::File recordingsDir() { return documentsDir().getChildFile ("recordings"); }

    juce::File tabsDir()
    {
        auto f = moduleDir().getChildFile ("scores");
        if (f.isDirectory() || f.createDirectory().wasOk())
            if (f.hasWriteAccess())
                return f;

        f = documentsDir().getChildFile ("scores");
        f.createDirectory();
        return f;
    }

    juce::File settingsFile()
    {
        return userDataDir().getChildFile ("settings.xml");
    }

    juce::File practiceLogFile()
    {
        return userDataDir().getChildFile ("practice_log.json");
    }

    juce::File defaultSettingsDir()
    {
        if (const auto found = findNearModule ("defaults"); found.isDirectory())
            return found;
        return moduleDir().getChildFile ("defaults");
    }

    juce::File defaultPresetsDir()
    {
        if (const auto found = findNearModule ("presets"); found.isDirectory())
            return found;
        return moduleDir().getChildFile ("presets");
    }

    juce::File legacyUserDataDir()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("LianqinHost");
    }

    juce::File legacyDocumentsDir()
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("LianqinHost");
    }

    juce::String makePortable (const juce::File& file)
    {
        if (file.isAChildOf (moduleDir()))
            return "@app/" + file.getRelativePathFrom (moduleDir()).replaceCharacter ('\\', '/');

        const auto lib = libraryDir();
        if (file.isAChildOf (lib))
            return "@app/assets/" + file.getRelativePathFrom (lib).replaceCharacter ('\\', '/');

        return file.getFullPathName();
    }

    juce::File resolvePortable (const juce::String& path)
    {
        auto relative = path;
        if (relative.startsWith ("@app/"))
            relative = relative.substring (5);

        // Presets written by LianqinHost use the old Chinese folder names.
        relative = relative.replace ("音色库/NAM/", "assets/nam/community/")
                           .replace ("音色库/IR/", "assets/ir/")
                           .replace ("音色库/Gervana/", "assets/gervana/");

        juce::File file;
        if (juce::File::isAbsolutePath (path))
            file = juce::File (path);
        else if (relative.startsWith ("assets/"))
            file = libraryDir().getParentDirectory().getChildFile (relative);
        else if (relative.startsWith ("nam/") || relative.startsWith ("ir/")
                 || relative.startsWith ("gervana/"))
            file = libraryDir().getChildFile (relative);
        else
            file = moduleDir().getChildFile (relative);

        if (file.existsAsFile())
            return file;

        // Missing file: search the library by literal file name (not a wildcard, so
        // names with commas, brackets, or spaces still match).
        const auto name = file.getFileName().isNotEmpty() ? file.getFileName()
                                                          : juce::File (path).getFileName();
        if (name.isNotEmpty() && libraryDir().isDirectory())
            for (auto& candidate : libraryDir().findChildFiles (juce::File::findFiles, true))
                if (candidate.getFileName().equalsIgnoreCase (name))
                    return candidate;

        return file;
    }
}
