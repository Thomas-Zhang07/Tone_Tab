#include "MainComponent.h"
#include "Text.h"
#include "AppInfo.h"
#include "app/AppPaths.h"

namespace
{
    const juce::Colour bgColour    { 0xff151515 };
    const juce::Colour panelColour { 0xff222222 };
    const juce::Colour mutedText   { 0xff9a9a9a };

    void styleLabel (juce::Label& l, float size, juce::Colour c = juce::Colours::white)
    {
        l.setFont (juce::FontOptions (size));
        l.setColour (juce::Label::textColourId, c);
    }

    void styleGainSlider (juce::Slider& s, double min, double max, double value)
    {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 22);
        s.setRange (min, max, 0.5);
        s.setTextValueSuffix (" dB");
        s.setDoubleClickReturnValue (true, 0.0);
        s.setValue (value, juce::dontSendNotification);
    }

    juce::File getAppDataFolder()
    {
        return AppPaths::userDataDir();
    }
}

MainComponent::MainComponent()
    : inputMeter (TR ("Input", "输入")), outputMeter (TR ("Output", "输出"))
{
    // 这台电脑第一次运行：如果压缩包里带了“默认设置”（打包的人当时的音色、鼓机、练习设置），就用它
    {
        const auto userFile = AppPaths::settingsFile();
        const auto bundled = AppPaths::defaultSettingsDir().getChildFile ("settings.xml");
        if (! userFile.existsAsFile() && bundled.existsAsFile())
        {
            getAppDataFolder().createDirectory();
            bundled.copyFileTo (userFile);
        }
    }

    juce::PropertiesFile::Options opts;
    opts.applicationName     = "settings";
    opts.folderName          = "Tone_Tab";
    opts.filenameSuffix      = ".xml";
    opts.osxLibrarySubFolder = "Application Support";
    appProperties.setStorageParameters (opts);
    auto* settings = appProperties.getUserSettings();

    const auto language = settings->getValue ("ui.language", "en");
    i18n::Localisation::get().setLanguage (language == "zh" ? i18n::Language::chinese
                                                           : i18n::Language::english);

    if (const auto root = settings->getValue ("library.root"); root.isNotEmpty())
        AppPaths::setLibraryRoot (juce::File (root));
    if (const auto rec = settings->getValue ("recording.root"); rec.isNotEmpty())
        AppPaths::setRecordingsRoot (juce::File (rec));

    // One-time, non-destructive import from the old application.
    if (! settings->getBoolValue ("legacyImportDone", false))
    {
        const auto legacyPresets = AppPaths::legacyDocumentsDir().getChildFile (T ("音色预设"));
        if (legacyPresets.isDirectory())
        {
            auto destination = AppPaths::presetsDir();
            destination.createDirectory();
            for (auto& f : legacyPresets.findChildFiles (juce::File::findFiles, false, "*.xml"))
            {
                const auto target = destination.getChildFile (f.getFileName());
                if (! target.existsAsFile())
                    f.copyFileTo (target);
            }
        }

        const auto legacyLog = AppPaths::legacyUserDataDir().getChildFile (T ("练习记录.json"));
        if (legacyLog.existsAsFile() && ! AppPaths::practiceLogFile().existsAsFile())
            legacyLog.copyFileTo (AppPaths::practiceLogFile());

        settings->setValue ("legacyImportDone", true);
        settings->saveIfNeeded();
    }

    // ---- 顶栏 ---------------------------------------------------------------
    titleLabel.setText (T (AppInfo::name), juce::dontSendNotification);
    styleLabel (titleLabel, 21.0f);
    titleLabel.setFont (juce::FontOptions (21.0f, juce::Font::bold));
    addAndMakeVisible (titleLabel);
    authorLabel.setText (TR ("Made by: ", "制作者：") + T (AppInfo::author), juce::dontSendNotification);
    styleLabel (authorLabel, 12.5f, mutedText);
    addAndMakeVisible (authorLabel);
    styleLabel (statusLabel, 14.0f, mutedText);
    addAndMakeVisible (statusLabel);
    styleLabel (latencyLabel, 14.0f, juce::Colour (0xff5dcaa5));
    latencyLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (latencyLabel);
    settingsButton.setButtonText (TR ("Audio Settings…", "音频设置…"));
    settingsButton.onClick = [this] { openAudioSettings(); };
    addAndMakeVisible (settingsButton);

    languageBox.addItem ("EN", 1);
    languageBox.addItem (T ("中文"), 2);
    languageBox.setSelectedId (language == "zh" ? 2 : 1, juce::dontSendNotification);
    languageBox.onChange = [this]
    {
        const bool zh = languageBox.getSelectedId() == 2;
        if (auto* s = appProperties.getUserSettings())
        {
            s->setValue ("ui.language", zh ? "zh" : "en");
            s->saveIfNeeded();
        }
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
                                                TR ("Language", "语言"),
                                                TR ("Restart Tone_Tab to apply the language.",
                                                    "重启 Tone_Tab 后语言生效。"));
    };
    addAndMakeVisible (languageBox);

    // ---- 左栏：输入输出 -------------------------------------------------------
    channelLabel.setText (TR ("Guitar input", "吉他输入通道"), juce::dontSendNotification);
    styleLabel (channelLabel, 14.0f, mutedText);
    addAndMakeVisible (channelLabel);
    channelBox.onChange = [this]
    {
        const int i = channelBox.getSelectedItemIndex();
        if (juce::isPositiveAndBelow (i, channelForComboId.size()))
        {
            engine.setGuitarChannel (channelForComboId[i]);
            saveSettings();
        }
    };
    addAndMakeVisible (channelBox);

    inputGainLabel.setText (TR ("Input gain", "输入增益"), juce::dontSendNotification);
    styleLabel (inputGainLabel, 14.0f, mutedText);
    addAndMakeVisible (inputGainLabel);
    styleGainSlider (inputGainSlider, -24.0, 24.0, settings->getDoubleValue ("inputGainDb", 0.0));
    inputGainSlider.onValueChange = [this] { engine.setInputGainDb ((float) inputGainSlider.getValue()); };
    addAndMakeVisible (inputGainSlider);

    outputGainLabel.setText (TR ("Guitar level", "吉他音量"), juce::dontSendNotification);
    styleLabel (outputGainLabel, 14.0f, mutedText);
    addAndMakeVisible (outputGainLabel);
    styleGainSlider (outputGainSlider, -60.0, 6.0, settings->getDoubleValue ("outputGainDb", -6.0));
    outputGainSlider.setDoubleClickReturnValue (true, -6.0);
    outputGainSlider.onValueChange = [this] { engine.setOutputGainDb ((float) outputGainSlider.getValue()); };
    addAndMakeVisible (outputGainSlider);

    monitorToggle.setButtonText (TR ("Monitor guitar", "监听吉他"));
    monitorToggle.setToggleState (settings->getBoolValue ("monitoring", true), juce::dontSendNotification);
    monitorToggle.onClick = [this] { engine.setMonitoring (monitorToggle.getToggleState()); };
    addAndMakeVisible (monitorToggle);

    styleLabel (hintLabel, 13.0f, juce::Colour (0xffef9f27));
    hintLabel.setJustificationType (juce::Justification::topLeft);
    hintLabel.setMinimumHorizontalScale (1.0f);
    addAndMakeVisible (hintLabel);

    addAndMakeVisible (inputMeter);
    addAndMakeVisible (outputMeter);
    addAndMakeVisible (tuner);

    engine.setInputGainDb ((float) inputGainSlider.getValue());
    engine.setOutputGainDb ((float) outputGainSlider.getValue());
    engine.setMonitoring (monitorToggle.getToggleState());
    engine.setGuitarChannel (settings->getIntValue ("guitarChannel", 0));

    // ---- 声卡 ---------------------------------------------------------------
    std::unique_ptr<juce::XmlElement> savedDevice (settings->getXmlValue ("audioDevice"));
    const auto err = deviceManager.initialise (2, 2, savedDevice.get(), true);
    if (savedDevice == nullptr)
    {
        for (auto* type : deviceManager.getAvailableDeviceTypes())
        {
            if (type->getTypeName() == "ASIO")
            {
                type->scanForDevices();
                if (type->getDeviceNames().size() > 0)
                    deviceManager.setCurrentAudioDeviceType ("ASIO", true);
                break;
            }
        }
    }
    if (err.isNotEmpty())
        hintLabel.setText (TR ("Could not open audio device: ", "打开声卡失败：") + err, juce::dontSendNotification);

    deviceManager.addAudioCallback (&engine);
    deviceManager.addChangeListener (this);

    // ---- 效果器链 / 预设 / 鼓机 -----------------------------------------------
    if (auto xml = settings->getXmlValue ("knownPlugins"))
        chain.getKnownPlugins().recreateFromXml (*xml);
    chain.getKnownPlugins().addChangeListener (this);

    chainLabel.setText (TR ("Effect Chain", "效果器链"), juce::dontSendNotification);
    chainLabel.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    addAndMakeVisible (chainLabel);

    presetBar = std::make_unique<PresetBar> (chain, *settings);
    addAndMakeVisible (*presetBar);

    pedalBoard = std::make_unique<PedalBoard> (chain, [this] { openPluginManager(); });
    pedalBoard->onOpenPedalEditor = [this] (EffectSlotPtr s) { showPedalEditor (std::move (s)); };
    pedalBoard->onSlotGone = [this] (EffectSlot* s)
    {
        if (pedalOverlay != nullptr && pedalOverlay->getSlot() == s)
            closePedalEditor();
    };
    addAndMakeVisible (*pedalBoard);

    drumPanel = std::make_unique<DrumPanel> (engine.getDrums(), *settings);
    addAndMakeVisible (*drumPanel);

    loopPanel = std::make_unique<LoopPanel> (engine.getLooper(), engine.getDrums(), *settings);
    addChildComponent (*loopPanel);

    // ---- 练习谱面 -----------------------------------------------------------
    practicePanel = std::make_unique<PracticePanel> (engine.getTabPlayer(), engine.getDrums(), engine.getJudge(),
                                                     engine.getRecorder(), practiceLog, *settings);
    engine.getJudge().startAnalysis();
    addChildComponent (*practicePanel);

    recordingPanel = std::make_unique<RecordingPanel> (engine, chain, *settings);
    addChildComponent (*recordingPanel);

    toneViewButton.setButtonText (TR ("Tone", "音色"));
    practiceViewButton.setButtonText (TR ("Practice", "练习谱面"));
    drumsViewButton.setButtonText (TR ("Drums", "鼓机"));
    recordViewButton.setButtonText (TR ("Record", "录音"));
    statsViewButton.setButtonText (TR ("Stats", "练习记录"));
    loopViewButton.setButtonText (TR ("Loop", "循环"));
    statsView = std::make_unique<StatsView> (practiceLog);
    statsView->onOpenSong = [this] (const juce::File& f)
    {
        setView (View::practice);
        if (f.existsAsFile())
            practicePanel->loadFile (f);
    };
    addChildComponent (*statsView);
    for (auto* b : { &toneViewButton, &practiceViewButton, &drumsViewButton, &recordViewButton,
                     &statsViewButton, &loopViewButton })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (1001);
        b->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2f6fdb));
        addAndMakeVisible (*b);
    }
    toneViewButton.setConnectedEdges (juce::Button::ConnectedOnRight);
    practiceViewButton.setConnectedEdges (juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    drumsViewButton.setConnectedEdges (juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    recordViewButton.setConnectedEdges (juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    statsViewButton.setConnectedEdges (juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    loopViewButton.setConnectedEdges (juce::Button::ConnectedOnLeft);
    statsViewButton.onClick = [this] { if (statsViewButton.getToggleState()) setView (View::stats); };
    toneViewButton.onClick = [this] { if (toneViewButton.getToggleState()) setView (View::tone); };
    practiceViewButton.onClick = [this] { if (practiceViewButton.getToggleState()) setView (View::practice); };
    drumsViewButton.onClick = [this] { if (drumsViewButton.getToggleState()) setView (View::drums); };
    recordViewButton.onClick = [this] { if (recordViewButton.getToggleState()) setView (View::record); };
    loopViewButton.onClick = [this] { if (loopViewButton.getToggleState()) setView (View::loop); };

    // 恢复上次的链路；第一次运行加载默认音色
    if (auto xml = settings->getXmlValue ("currentChain"))
    {
        juce::StringArray problems;
        chain.loadFromXml (*xml, problems);
        if (! problems.isEmpty())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                    TR ("Some effects failed to load", "部分效果器没有加载"), problems.joinIntoString ("\n"));
    }
    else
    {
        presetBar->loadPreset (presetBar->getCurrentPresetName().isNotEmpty() ? presetBar->getCurrentPresetName() : T ("清音"));
    }
    chain.addChangeListener (this);

    {
        const auto v = settings->getValue ("view");
        setView (v == "practice" ? View::practice
                 : v == "drums" ? View::drums
                 : v == "record" ? View::record
                 : v == "stats" ? View::stats
                 : v == "loop" ? View::loop : View::tone);
    }

    tunerBuffer.resize (4096);
    refreshInputChannelList();
    refreshStatus();

    setWantsKeyboardFocus (true);
    setSize (1240, 800);
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    stopTimer();
    engine.getJudge().stopAnalysis();
    saveSettings();

    chain.removeChangeListener (this);
    chain.getKnownPlugins().removeChangeListener (this);
    deviceManager.removeChangeListener (this);

    // 先关插件窗口和放大踏板，再停音频，最后释放效果器
    pedalOverlay = nullptr;
    pedalBoard->onSlotGone = nullptr;
    pedalBoard->closeAllEditors();
    deviceManager.removeAudioCallback (&engine);
    deviceManager.closeAudioDevice();

    // 插件实例要在插件格式管理器（属于 chain）之前销毁
    chain.clear();
    engine.releaseAllChains();

    recordingPanel = nullptr;
    practicePanel = nullptr;
    statsView = nullptr;
    loopPanel = nullptr;
    drumPanel = nullptr;
    pedalBoard = nullptr;
    presetBar = nullptr;
}

void MainComponent::saveSettings()
{
    auto* s = appProperties.getUserSettings();
    if (auto xml = deviceManager.createStateXml())
        s->setValue ("audioDevice", xml.get());
    s->setValue ("guitarChannel", engine.getGuitarChannel());
    s->setValue ("inputGainDb", inputGainSlider.getValue());
    s->setValue ("outputGainDb", outputGainSlider.getValue());
    s->setValue ("monitoring", monitorToggle.getToggleState());
    s->setValue ("view", view == View::practice ? "practice"
                          : view == View::drums ? "drums"
                          : view == View::record ? "record"
                          : view == View::stats ? "stats"
                          : view == View::loop ? "loop" : "tone");
    practiceLog.saveIfNeeded();
    if (auto xml = chain.toXml())
        s->setValue ("currentChain", xml.get());
    s->saveIfNeeded();
}

void MainComponent::openAudioSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent (deviceManager, 1, 16, 2, 16, false, false, false, false);
    selector->setSize (560, 520);

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (selector);
    o.dialogTitle = TR ("Audio Settings", "音频设置");
    o.dialogBackgroundColour = panelColour;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.launchAsync();
}

void MainComponent::openPluginManager()
{
    auto folder = getAppDataFolder();
    folder.createDirectory();

    auto* list = new juce::PluginListComponent (chain.getFormatManager(), chain.getKnownPlugins(),
                                                folder.getChildFile ("RecentlyCrashedPluginsList"),
                                                appProperties.getUserSettings(), true);
    list->setSize (760, 520);

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (list);
    o.dialogTitle = TR ("VST3 Plug-ins (use Scan under Options, bottom left)", "VST3 插件（点左下角 Options 里的 Scan 扫描）");
    o.dialogBackgroundColour = panelColour;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.launchAsync();
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &chain)
    {
        chainDirty = true;
        return;
    }

    if (source == &chain.getKnownPlugins())
    {
        if (auto xml = chain.getKnownPlugins().createXml())
            appProperties.getUserSettings()->setValue ("knownPlugins", xml.get());
        appProperties.getUserSettings()->saveIfNeeded();
        return;
    }

    // 设备、采样率、缓冲区或启用的通道变了
    refreshInputChannelList();
    refreshStatus();
    saveSettings();
}

void MainComponent::showPedalEditor (EffectSlotPtr slot)
{
    pedalOverlay = nullptr;

    PedalEditorOverlay::Callbacks cb;
    auto* raw = slot.get();
    cb.toggleBypass = [this, raw] { pedalBoard->toggleBypassFor (raw); };
    cb.replaceFile  = [this, raw] { pedalBoard->replaceFileFor (raw); };
    cb.close        = [this] { closePedalEditor(); };

    pedalOverlay = std::make_unique<PedalEditorOverlay> (std::move (slot), std::move (cb));
    addAndMakeVisible (*pedalOverlay);
    pedalOverlay->setBounds (getLocalBounds());
    pedalOverlay->grabKeyboardFocus();
}

void MainComponent::closePedalEditor()
{
    if (pedalOverlay == nullptr)
        return;

    // 可能是在放大踏板自己的按钮回调里关闭的，所以放到下一轮消息再删除
    pedalOverlay->setVisible (false);
    auto* raw = pedalOverlay.release();
    juce::MessageManager::callAsync ([raw] { delete raw; });
    grabKeyboardFocus();
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey && pedalOverlay != nullptr)
    {
        closePedalEditor();
        return true;
    }

    // Windows repeats key-down events while SPACE is held. Without this guard,
    // holding the key a little too long would record, close and overdub in one
    // press. keyStateChanged clears the flag on key-up.
    if (key == juce::KeyPress::spaceKey && spaceHeld
        && juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::spaceKey))
        return true;

    if (view == View::loop)
    {
        if (key == juce::KeyPress::spaceKey)
        {
            spaceHeld = true;
            loopPanel->toggleLoop();
            return true;
        }
        if (key == juce::KeyPress::escapeKey)
        {
            loopPanel->stopLoop();
            return true;
        }
        return false;
    }

    if (view == View::practice)
    {
        if (key == juce::KeyPress::spaceKey)
            spaceHeld = true;
        return practicePanel->keyPressed (key);
    }

    if (view == View::record)
    {
        if (key == juce::KeyPress::spaceKey)
        {
            spaceHeld = true;
            recordingPanel->togglePlayPause();
            return true;
        }
        return false;
    }

    if (view == View::stats)
        return false;

    if (key == juce::KeyPress::spaceKey)
    {
        spaceHeld = true;

        if (! engine.getDrums().isPlaying())
            engine.getTabPlayer().setPlaying (false);   // 鼓机和谱子不同时放
        drumPanel->togglePlay();
        return true;
    }
    return false;
}

bool MainComponent::keyStateChanged (bool)
{
    if (! juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::spaceKey))
        spaceHeld = false;
    return false;
}

void MainComponent::focusLost (FocusChangeType)
{
    // If focus moves while SPACE is held the key-up can be missed; drop the
    // repeat guard rather than swallowing the next press.
    spaceHeld = false;
}

void MainComponent::setView (View v)
{
    view = v;
    spaceHeld = false;
    const bool tone = v == View::tone;
    toneViewButton.setToggleState (tone, juce::dontSendNotification);
    practiceViewButton.setToggleState (v == View::practice, juce::dontSendNotification);
    drumsViewButton.setToggleState (v == View::drums, juce::dontSendNotification);
    recordViewButton.setToggleState (v == View::record, juce::dontSendNotification);
    statsViewButton.setToggleState (v == View::stats, juce::dontSendNotification);
    loopViewButton.setToggleState (v == View::loop, juce::dontSendNotification);
    for (auto* c : std::initializer_list<juce::Component*> { &chainLabel, presetBar.get(), pedalBoard.get() })
        c->setVisible (tone);
    drumPanel->setVisible (v == View::drums);
    practicePanel->setVisible (v == View::practice);
    recordingPanel->setVisible (v == View::record);
    statsView->setVisible (v == View::stats);
    loopPanel->setVisible (v == View::loop);

    const bool showRail = v == View::tone || v == View::practice;
    for (auto* c : std::initializer_list<juce::Component*> { &channelLabel, &channelBox, &inputGainLabel,
                                                              &inputGainSlider, &outputGainLabel, &outputGainSlider,
                                                              &monitorToggle, &hintLabel, &inputMeter, &outputMeter, &tuner })
        c->setVisible (showRail);
    if (v == View::stats)
        statsView->refresh();
    if (! tone)
        closePedalEditor();
    resized();
    repaint();
    if (v == View::loop)
        loopPanel->grabKeyboardFocus();
    else
        grabKeyboardFocus();
}

bool MainComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("gp3;gp4;gp5;gpx;gp"))
            return true;
    return false;
}

void MainComponent::filesDropped (const juce::StringArray& files, int, int)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("gp3;gp4;gp5;gpx;gp"))
        {
            setView (View::practice);
            practicePanel->loadFile (juce::File (f));
            return;
        }
}

void MainComponent::refreshInputChannelList()
{
    channelBox.clear (juce::dontSendNotification);
    channelForComboId.clear();

    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
    {
        channelBox.setTextWhenNothingSelected (TR ("No audio device open", "没有打开的声卡"));
        return;
    }

    const auto names = device->getInputChannelNames();
    const auto active = device->getActiveInputChannels();
    const int wanted = engine.getGuitarChannel();
    int selectIndex = -1;

    for (int ch = 0; ch < names.size(); ++ch)
    {
        if (! active[ch])
            continue;
        channelForComboId.add (ch);
        channelBox.addItem (juce::String (ch + 1) + "  " + names[ch], channelForComboId.size());
        if (ch == wanted)
            selectIndex = channelForComboId.size() - 1;
    }

    if (channelForComboId.isEmpty())
    {
        channelBox.setTextWhenNothingSelected (TR ("No input channels enabled", "没有启用的输入通道"));
        return;
    }

    if (selectIndex < 0)
    {
        selectIndex = 0;
        engine.setGuitarChannel (channelForComboId[0]);
    }
    channelBox.setSelectedItemIndex (selectIndex, juce::dontSendNotification);
}

void MainComponent::refreshStatus()
{
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
    {
        statusLabel.setText (TR ("No audio device open. Click “Audio Settings…” in the top right to choose one.", "没有打开声卡，点右上角的“音频设置…”选择设备"), juce::dontSendNotification);
        latencyLabel.setText ({}, juce::dontSendNotification);
        return;
    }

    const double sr = device->getCurrentSampleRate();
    const int block = device->getCurrentBufferSizeSamples();
    const int latencySamples = device->getInputLatencyInSamples() + device->getOutputLatencyInSamples();
    const double latencyMs = sr > 0 ? 1000.0 * latencySamples / sr : 0.0;
    const double cpu = deviceManager.getCpuUsage() * 100.0;

    statusLabel.setText (device->getTypeName() + "  |  " + device->getName() + "  |  "
                             + juce::String (sr / 1000.0, 1) + " kHz  |  "
                             + juce::String (block) + TR (" samples  |  CPU ", " 采样缓冲  |  CPU ") + juce::String (juce::roundToInt (cpu)) + "%",
                         juce::dontSendNotification);

    latencyLabel.setText (TR ("Round-trip latency ", "往返延迟 ") + juce::String (latencyMs, 1) + " ms", juce::dontSendNotification);
    if (practicePanel != nullptr)
        practicePanel->setDeviceLatencyMs ((float) latencyMs);
    latencyLabel.setColour (juce::Label::textColourId, latencyMs <= 12.0 ? juce::Colour (0xff5dcaa5) : juce::Colour (0xffef9f27));

    juce::String hint;
    if (device->getTypeName() != "ASIO")
        hint = TR ("Not using an ASIO driver; latency will be higher.", "当前不是 ASIO 驱动，延迟会比较大。");
    else if (latencyMs > 12.0)
        hint = TR ("High latency: set the buffer to 64 or 128 samples.", "延迟偏大：把缓冲区调到 64 或 128。");
    if (cpu > 70.0)
        hint = TR ("High CPU: increase the buffer size or disable some effects.", "CPU 占用偏高：缓冲区调大一档，或关掉几个效果器。");
    if (channelForComboId.isEmpty())
        hint = TR ("No input enabled: tick the guitar input in “Audio Settings”.", "没有启用的输入通道：在“音频设置”里勾选吉他那一路。");

    hintLabel.setText (hint, juce::dontSendNotification);
}

void MainComponent::timerCallback()
{
    inputMeter.push (engine.takeInputPeak(), engine.takeInputClipped());
    outputMeter.push (engine.takeOutputPeak(), engine.takeOutputClipped());

    const double sr = engine.copyRecentDrySamples (tunerBuffer.data(), (int) tunerBuffer.size());
    if (sr > 0.0)
    {
        const int len = sr > 50000.0 ? 4096 : 2048;
        tuner.update (pitchDetector.detect (tunerBuffer.data() + (tunerBuffer.size() - (size_t) len), len, sr));
    }

    chain.collectGarbage();
    engine.getTabPlayer().releaseRetiredSongs();

    if (++frameCounter % 15 == 0)
        refreshStatus();

    if (chainDirty && frameCounter % 30 == 0)
    {
        chainDirty = false;
        if (auto xml = chain.toXml())
            appProperties.getUserSettings()->setValue ("currentChain", xml.get());
    }
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (bgColour);
    g.setColour (panelColour);
    if (view == View::tone || view == View::practice)
        g.fillRoundedRectangle (leftPanel.toFloat(), 10.0f);
    if (view == View::tone)
        g.fillRoundedRectangle (fxPanel.toFloat(), 10.0f);
    else if (view == View::drums)
        g.fillRoundedRectangle (practiceArea.toFloat(), 10.0f);
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (16);

    // 顶栏
    auto header = area.removeFromTop (34);
    settingsButton.setBounds (header.removeFromRight (106).reduced (0, 1));
    header.removeFromRight (6);
    languageBox.setBounds (header.removeFromRight (58).reduced (0, 3));
    header.removeFromRight (10);
    latencyLabel.setBounds (header.removeFromRight (148));
    titleLabel.setBounds (header.removeFromLeft (120));
    authorLabel.setBounds (header.removeFromLeft (124));
    toneViewButton.setBounds (header.removeFromLeft (68).reduced (0, 1));
    practiceViewButton.setBounds (header.removeFromLeft (82).reduced (0, 1));
    drumsViewButton.setBounds (header.removeFromLeft (70).reduced (0, 1));
    recordViewButton.setBounds (header.removeFromLeft (74).reduced (0, 1));
    statsViewButton.setBounds (header.removeFromLeft (64).reduced (0, 1));
    loopViewButton.setBounds (header.removeFromLeft (64).reduced (0, 1));
    header.removeFromLeft (10);
    statusLabel.setBounds (header);
    area.removeFromTop (12);

    // Left input rail: tone and practice only.
    leftPanel = {};
    if (view == View::tone || view == View::practice)
    {
        leftPanel = area.removeFromLeft (310);
        area.removeFromLeft (14);
        auto p = leftPanel.reduced (16);
        auto rowWithLabel = [&p] (juce::Label& l, juce::Component& c)
        {
            l.setBounds (p.removeFromTop (20));
            c.setBounds (p.removeFromTop (30));
            p.removeFromTop (10);
        };
        rowWithLabel (channelLabel, channelBox);
        rowWithLabel (inputGainLabel, inputGainSlider);
        rowWithLabel (outputGainLabel, outputGainSlider);
        monitorToggle.setBounds (p.removeFromTop (26));
        p.removeFromTop (8);
        hintLabel.setBounds (p.removeFromBottom (40));
        p.removeFromBottom (8);

        auto meters = p.removeFromRight (100);
        inputMeter.setBounds (meters.removeFromLeft (44));
        meters.removeFromLeft (12);
        outputMeter.setBounds (meters.removeFromLeft (44));
        p.removeFromRight (12);
        tuner.setBounds (p);
    }

    // 右侧：练习谱面 / 效果器链 + 鼓机
    practiceArea = area;
    practicePanel->setBounds (practiceArea);
    recordingPanel->setBounds (practiceArea);
    statsView->setBounds (practiceArea);
    drumPanel->setBounds (practiceArea);
    loopPanel->setBounds (practiceArea);
    fxPanel = area;

    {
        auto p = fxPanel.reduced (16);
        auto top = p.removeFromTop (32);
        chainLabel.setBounds (top.removeFromLeft (100));
        presetBar->setBounds (top);
        p.removeFromTop (10);
        pedalBoard->setBounds (p);
    }

    if (pedalOverlay != nullptr)
        pedalOverlay->setBounds (getLocalBounds());
}
