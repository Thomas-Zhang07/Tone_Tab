#include <juce_gui_basics/juce_gui_basics.h>
#include <cstdio>
#include <string>
#include "MainComponent.h"
#include "AppInfo.h"
#include "Text.h"

#if defined(TONE_TESTS_EMBEDDED)
int runOfflineTests (int argc, char* argv[]);
#endif

namespace
{
    class HostLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        HostLookAndFeel() : juce::LookAndFeel_V4 (juce::LookAndFeel_V4::getDarkColourScheme())
        {
           #if JUCE_WINDOWS
            // 微软雅黑，保证中文显示正常
            setDefaultSansSerifTypefaceName ("Microsoft YaHei UI");
           #endif
            setColour (juce::ResizableWindow::backgroundColourId, juce::Colour (0xff151515));
            setColour (juce::Slider::thumbColourId, juce::Colour (0xff5dcaa5));
            setColour (juce::Slider::trackColourId, juce::Colour (0xff1d9e75));
        }
    };
}

class LianqinApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String& commandLine) override
    {
        // Keep the designed 1240x800 layout usable on small/high-DPI screens:
        // don't let Windows scale it beyond the available display area.
        if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        {
            const float fit = juce::jmin (display->userArea.getWidth() / 1240.0f,
                                          display->userArea.getHeight() / 800.0f);
            juce::Desktop::getInstance().setGlobalScaleFactor (juce::jlimit (0.75f, 1.0f, fit));
        }

        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

       #if defined(TONE_TESTS_EMBEDDED)
        if (commandLine.contains ("--run-tests"))
        {
            auto outDir = juce::File::getCurrentWorkingDirectory().getChildFile ("build").getChildFile ("test-out");
            outDir.createDirectory();
            const auto logFile = outDir.getChildFile ("embedded-tests.log");
            ::freopen (logFile.getFullPathName().toRawUTF8(), "w", stdout);
            ::freopen (logFile.getFullPathName().toRawUTF8(), "a", stderr);

            std::string modelDir = "D:\\NAM\\models";
            std::string outPath = outDir.getFullPathName().toStdString();
            char* args[] = { const_cast<char*> ("Tone_Tab"),
                             const_cast<char*> (modelDir.c_str()),
                             const_cast<char*> (outPath.c_str()), nullptr };
            setApplicationReturnValue (runOfflineTests (3, args));
            quit();
            return;
        }
       #endif

        mainWindow = std::make_unique<MainWindow> (T (AppInfo::name) + TR ("  —  Made by: ", "  —  制作者：") + T (AppInfo::author));
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override { quit(); }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name, juce::Colour (0xff151515), DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);
            setResizable (true, true);
            setResizeLimits (1100, 720, 2400, 1600);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

private:
    HostLookAndFeel lookAndFeel;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (LianqinApplication)
