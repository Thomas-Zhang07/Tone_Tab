#include "TabSearch.h"
#include "Text.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <shlobj.h>
 #include <knownfolders.h>
 #pragma comment (lib, "shell32.lib")
 #pragma comment (lib, "ole32.lib")
#endif

juce::File getSystemDownloadsFolder()
{
   #if JUCE_WINDOWS
    PWSTR path = nullptr;
    if (SUCCEEDED (SHGetKnownFolderPath (FOLDERID_Downloads, 0, nullptr, &path)) && path != nullptr)
    {
        const juce::File f { juce::String (path) };
        CoTaskMemFree (path);
        if (f.isDirectory())
            return f;
    }
   #endif
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Downloads");
}

// =============================================================================
bool DownloadWatcher::isTabFile (const juce::File& f)
{
    return f.hasFileExtension ("gp3;gp4;gp5;gpx;gp");
}

void DownloadWatcher::start (const juce::File& f, double minutes)
{
    folder = f;
    known.clear();
    pendingSize.clear();
    for (auto& c : folder.findChildFiles (juce::File::findFiles, false))
        if (isTabFile (c))
            known[c.getFullPathName()] = c.getLastModificationTime();
    until = juce::Time::getCurrentTime() + juce::RelativeTime::minutes (minutes);
    active = folder.isDirectory();
}

juce::File DownloadWatcher::poll()
{
    if (! active)
        return {};
    if (juce::Time::getCurrentTime() > until)
    {
        active = false;
        return {};
    }

    for (auto& c : folder.findChildFiles (juce::File::findFiles, false))
    {
        if (! isTabFile (c))
            continue;
        const auto path = c.getFullPathName();
        const auto mtime = c.getLastModificationTime();
        if (auto it = known.find (path); it != known.end() && it->second == mtime)
            continue;     // 旧文件，没变

        // 新文件（或被重新下载覆盖）：大小连续两次一样才算下载完
        const auto size = c.getSize();
        auto& last = pendingSize[path];
        if (size > 0 && last == size)
        {
            known[path] = mtime;
            pendingSize.erase (path);
            return c;
        }
        last = size;
    }
    return {};
}

// =============================================================================
const std::vector<TabSearchBox::Site>& TabSearchBox::getSites()
{
    // {q} 会换成搜索词（已做 URL 编码）
    static const std::vector<Site> sites {
        { TR ("Jitashe", "吉他社"),          "https://www.bing.com/search?q=site%3Ajitashe.org+{q}+GP",
          TR ("Chinese site with many free GP tabs (registration required)", "中文站，很多 GP 谱可以免费下载（要注册登录）") },
        { "Songsterr",           "https://www.songsterr.com/?pattern={q}",
          TR ("Largest tab library with previews; GP downloads require membership", "谱子最全，带试听；下载 GP 文件需要会员") },
        { "Ultimate Guitar",     "https://www.ultimate-guitar.com/search.php?search_type=title&value={q}",
          TR ("Look for “Guitar Pro” tabs in the results", "在结果里找 “Guitar Pro” 类型的谱子") },
        { TR ("Bing GP Search", "必应搜 GP 文件"),  "https://www.bing.com/search?q={q}+guitar+pro+gp5+OR+gpx+OR+gp+%E4%B8%8B%E8%BD%BD",
          TR ("Web-wide search; more results, mixed quality", "全网搜，结果多但质量参差") },
    };
    return sites;
}

TabSearchBox::TabSearchBox (const juce::String& initialQuery, const juce::File& watchFolder)
{
    title.setText (TR ("Find Tabs Online", "在线找谱"), juce::dontSendNotification);
    title.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    addAndMakeVisible (title);

    query.setText (initialQuery, false);
    query.setTextToShowWhenEmpty (TR ("Song or artist, e.g. Jay Chou", "歌名或歌手，例如：晴天 周杰伦"), juce::Colours::grey);
    query.setFont (juce::FontOptions (15.0f));
    query.onReturnKey = [this] { search (getSites().front()); };
    addAndMakeVisible (query);

    for (auto& s : getSites())
    {
        auto* b = siteButtons.add (new juce::TextButton (s.name));
        b->setTooltip (s.tip);
        b->onClick = [this, &s] { search (s); };
        addAndMakeVisible (b);
    }

    hint.setText (TR ("Search results open in your browser. After you download a .gp / .gp5 / .gpx file, the app moves it into the scores folder and opens it (waits up to 20 minutes).",
                      "会在浏览器里打开搜索结果。在网页上下载 .gp / .gp5 / .gpx 文件后，程序会自动把它放进“吉他谱”文件夹并打开（等待 20 分钟）。"),
                  juce::dontSendNotification);
    hint.setFont (juce::FontOptions (12.5f));
    hint.setColour (juce::Label::textColourId, juce::Colour (0xffaaaaaa));
    hint.setJustificationType (juce::Justification::topLeft);
    hint.setMinimumHorizontalScale (1.0f);     // 不压扁，换行显示
    addAndMakeVisible (hint);

    folderLabel.setFont (juce::FontOptions (12.0f));
    folderLabel.setColour (juce::Label::textColourId, juce::Colour (0xff888888));
    folderLabel.setMinimumHorizontalScale (0.6f);
    addAndMakeVisible (folderLabel);
    folderButton.setButtonText (TR ("Change Folder…", "换文件夹…"));
    folderButton.setTooltip (TR ("If your browser saves downloads elsewhere, select that folder here", "浏览器把文件下载到别的地方时，在这里改成那个文件夹"));
    folderButton.onClick = [this] { if (onChangeFolder) onChangeFolder(); };
    addAndMakeVisible (folderButton);

    setWatchFolder (watchFolder);
    setSize (460, 214);
}

void TabSearchBox::setWatchFolder (const juce::File& f)
{
    folderLabel.setText (TR ("Watching downloads: ", "监视下载文件夹：") + f.getFullPathName(), juce::dontSendNotification);
}

void TabSearchBox::search (const Site& site)
{
    const auto q = query.getText().trim();
    if (q.isEmpty())
    {
        query.grabKeyboardFocus();
        return;
    }
    const auto url = site.urlTemplate.replace ("{q}", juce::URL::addEscapeChars (q, true));
    if (onSearch)
        onSearch (url);
}

void TabSearchBox::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff262626));
}

void TabSearchBox::resized()
{
    auto r = getLocalBounds().reduced (12);
    title.setBounds (r.removeFromTop (22));
    r.removeFromTop (6);
    query.setBounds (r.removeFromTop (30));
    r.removeFromTop (8);

    auto row = r.removeFromTop (28);
    const int w = (row.getWidth() - 6 * (siteButtons.size() - 1)) / juce::jmax (1, siteButtons.size());
    for (auto* b : siteButtons)
    {
        b->setBounds (row.removeFromLeft (w));
        row.removeFromLeft (6);
    }
    r.removeFromTop (8);

    auto bottom = r.removeFromBottom (24);
    folderButton.setBounds (bottom.removeFromRight (80));
    folderLabel.setBounds (bottom);
    hint.setBounds (r);
}
