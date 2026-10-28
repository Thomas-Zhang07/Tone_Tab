#include "PracticeLog.h"
#include "app/AppPaths.h"

float PracticeLog::SongEntry::recentAccuracy (int n) const
{
    if (passes.empty())
        return -1.0f;
    float sum = 0.0f;
    int count = 0;
    for (auto it = passes.rbegin(); it != passes.rend() && count < n; ++it, ++count)
        sum += it->accuracy;
    return sum / (float) count;
}

PracticeLog::PracticeLog (const juce::File& f) : file (f)
{
    load();
}

PracticeLog::~PracticeLog()
{
    saveIfNeeded();
}

juce::File PracticeLog::defaultFile()
{
    return AppPaths::practiceLogFile();
}

PracticeLog::SongEntry& PracticeLog::entryFor (const juce::String& path, const juce::String& title)
{
    for (auto& s : songs)
        if (s.path == path)
        {
            if (title.isNotEmpty()) s.title = title;
            return s;
        }
    songs.push_back ({});
    songs.back().path = path;
    songs.back().title = title.isNotEmpty() ? title : juce::File (path).getFileNameWithoutExtension();
    return songs.back();
}

void PracticeLog::addTime (const juce::String& path, const juce::String& title, double seconds)
{
    if (seconds <= 0.0 || path.isEmpty())
        return;
    auto& e = entryFor (path, title);
    const auto now = juce::Time::currentTimeMillis();
    if (now - e.lastTime > 30 * 60 * 1000)      // 隔了半小时以上算新的一次
        ++e.sessions;
    e.lastTime = now;
    e.seconds += seconds;
    daily[dayKey (juce::Time (now))] += seconds;
    dirty = true;
    ++changes;
}

void PracticeLog::addPass (const juce::String& path, const juce::String& title, float accuracy, float speed)
{
    if (path.isEmpty())
        return;
    auto& e = entryFor (path, title);
    e.passes.push_back ({ juce::Time::currentTimeMillis(), juce::jlimit (0.0f, 1.0f, accuracy), speed });
    if (e.passes.size() > 400)
        e.passes.erase (e.passes.begin(), e.passes.begin() + (long) (e.passes.size() - 400));
    // “最好成绩”只算原速及以上
    if (speed >= 0.999f)
        e.best = juce::jmax (e.best, accuracy);
    dirty = true;
    ++changes;
}

double PracticeLog::secondsOnDay (juce::Time day) const
{
    auto it = daily.find (dayKey (day));
    return it != daily.end() ? it->second : 0.0;
}

double PracticeLog::secondsInLastDays (int days) const
{
    double s = 0.0;
    const auto now = juce::Time::getCurrentTime();
    for (int d = 0; d < days; ++d)
        s += secondsOnDay (now - juce::RelativeTime::days (d));
    return s;
}

double PracticeLog::totalSeconds() const
{
    double s = 0.0;
    for (auto& [k, v] : daily) s += v;
    return s;
}

int PracticeLog::streakDays() const
{
    const auto now = juce::Time::getCurrentTime();
    int d = secondsOnDay (now) >= 60.0 ? 0 : 1;     // 今天还没练：从昨天开始数
    int streak = 0;
    for (;; ++d)
    {
        if (secondsOnDay (now - juce::RelativeTime::days (d)) < 60.0)
            break;
        ++streak;
    }
    return streak;
}

// ---- 读写 ------------------------------------------------------------------------
void PracticeLog::load()
{
    const auto v = juce::JSON::parse (file);
    if (! v.isObject())
        return;

    if (auto* d = v["daily"].getDynamicObject())
        for (auto& p : d->getProperties())
            daily[p.name.toString()] = (double) p.value;

    if (auto* arr = v["songs"].getArray())
        for (auto& sv : *arr)
        {
            SongEntry e;
            e.path = sv["path"].toString();
            e.title = sv["title"].toString();
            e.seconds = (double) sv["seconds"];
            e.sessions = (int) sv["sessions"];
            e.lastTime = (juce::int64) sv["last"];
            e.best = sv.hasProperty ("best") ? (float) (double) sv["best"] : -1.0f;
            if (auto* ps = sv["passes"].getArray())
                for (auto& pv : *ps)
                    if (auto* triple = pv.getArray(); triple != nullptr && triple->size() >= 3)
                        e.passes.push_back ({ (juce::int64) (*triple)[0], (float) (double) (*triple)[1], (float) (double) (*triple)[2] });
            if (e.path.isNotEmpty())
                songs.push_back (std::move (e));
        }
}

void PracticeLog::save()
{
    auto* root = new juce::DynamicObject();
    auto* d = new juce::DynamicObject();
    for (auto& [k, v] : daily)
        d->setProperty (k, std::round (v));
    root->setProperty ("daily", juce::var (d));

    juce::Array<juce::var> arr;
    for (auto& e : songs)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("path", e.path);
        o->setProperty ("title", e.title);
        o->setProperty ("seconds", std::round (e.seconds));
        o->setProperty ("sessions", e.sessions);
        o->setProperty ("last", e.lastTime);
        if (e.best >= 0.0f) o->setProperty ("best", e.best);
        juce::Array<juce::var> ps;
        for (auto& p : e.passes)
            ps.add (juce::Array<juce::var> { p.time, std::round (p.accuracy * 1000.0f) / 1000.0f, std::round (p.speed * 100.0f) / 100.0f });
        o->setProperty ("passes", ps);
        arr.add (juce::var (o));
    }
    root->setProperty ("songs", arr);

    file.getParentDirectory().createDirectory();
    file.replaceWithText (juce::JSON::toString (juce::var (root), true));
    dirty = false;
}

void PracticeLog::saveIfNeeded()
{
    if (dirty)
        save();
}
