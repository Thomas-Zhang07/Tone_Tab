#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <map>

// 在线找谱：在浏览器里打开谱子网站的搜索结果页；同时盯着“下载”文件夹，
// 一有新下载的 Guitar Pro 文件就自动拷进程序的“吉他谱”文件夹并打开。
// （不在程序里直接抓取网站内容——这些网站的服务条款不允许，且多数需要登录才能下载。）

// 系统的“下载”文件夹（Windows 上读 Known Folder，用户改过位置也能找到）
juce::File getSystemDownloadsFolder();

// -----------------------------------------------------------------------------
class DownloadWatcher
{
public:
    void start (const juce::File& folder, double minutes = 20.0);
    void stop()                                  { active = false; }
    bool isActive() const                        { return active; }
    juce::File getFolder() const                 { return folder; }

    // 每秒左右调用一次；发现下载完成的新谱子就返回它
    juce::File poll();

private:
    static bool isTabFile (const juce::File& f);

    bool active = false;
    juce::File folder;
    juce::Time until;
    std::map<juce::String, juce::Time> known;           // 开始时已经存在的文件
    std::map<juce::String, juce::int64> pendingSize;     // 新文件上一次看到的大小（等它写完）
};

// -----------------------------------------------------------------------------
// 搜索框（在“在线找谱”按钮下面弹出）
class TabSearchBox : public juce::Component
{
public:
    struct Site { juce::String name, urlTemplate, tip; };
    static const std::vector<Site>& getSites();

    TabSearchBox (const juce::String& initialQuery, const juce::File& watchFolder);

    std::function<void (const juce::String& url)> onSearch;   // 打开网页并开始监视
    std::function<void()> onChangeFolder;

    void setWatchFolder (const juce::File& f);
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void search (const Site& site);

    juce::Label title, hint, folderLabel;
    juce::TextEditor query;
    juce::OwnedArray<juce::TextButton> siteButtons;
    juce::TextButton folderButton;
};
