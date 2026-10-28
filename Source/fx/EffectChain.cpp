#include "EffectChain.h"
#include "BuiltinEffects.h"
#include "NamProcessor.h"
#include "AudioEngine.h"
#include "app/AppPaths.h"
#include "Text.h"

// =============================================================================
EffectSlot::EffectSlot (Kind k, std::unique_ptr<juce::AudioProcessor> p, juce::String id)
    : kind (k), typeId (std::move (id)), processor (std::move (p))
{
}

EffectSlot::~EffectSlot()
{
    if (preparedBlock > 0)
        processor->releaseResources();
}

juce::String EffectSlot::getDisplayName() const
{
    switch (kind)
    {
        case Kind::nam:   return juce::File (typeId).getFileNameWithoutExtension();
        case Kind::ir:    return juce::File (typeId).getFileNameWithoutExtension();
        case Kind::vst3:  return pluginDescription.name;
        case Kind::builtin:
        default:          return processor->getName();
    }
}

juce::String EffectSlot::getKindLabel() const
{
    switch (kind)
    {
        case Kind::nam:   return TR ("NAM Amp", "NAM 箱头");
        case Kind::ir:    return TR ("Cab IR", "箱体 IR");
        case Kind::vst3:  return "VST3";
        case Kind::builtin:
        default:          return typeId.startsWith ("gv_") ? juce::String ("Gervana") : TR ("Built-in", "内置");
    }
}

void EffectSlot::prepare (double sampleRate, int maxBlockSize)
{
    if (preparedBlock > 0)
        processor->releaseResources();

    processor->setRateAndBufferSizeDetails (sampleRate, maxBlockSize);
    processor->prepareToPlay (sampleRate, maxBlockSize);

    numProcChannels = juce::jmax (2, processor->getTotalNumInputChannels(), processor->getTotalNumOutputChannels());
    scratch.setSize (numProcChannels, maxBlockSize, false, true, false);
    midi.ensureSize (512);
    preparedBlock = maxBlockSize;
}

void EffectSlot::process (juce::AudioBuffer<float>& io, int numSamples) noexcept
{
    const float target = bypassed.load (std::memory_order_relaxed) ? 0.0f : 1.0f;

    if (mix <= 0.0f && target <= 0.0f)
        return;                                   // 完全旁通：不占 CPU

    if (numSamples > preparedBlock)
        return;

    for (int ch = 0; ch < numProcChannels; ++ch)
    {
        if (ch < 2)
            scratch.copyFrom (ch, 0, io, ch, 0, numSamples);
        else
            scratch.clear (ch, 0, numSamples);
    }

    juce::AudioBuffer<float> view (scratch.getArrayOfWritePointers(), numProcChannels, numSamples);
    midi.clear();
    processor->processBlock (view, midi);

    if (processor->getTotalNumOutputChannels() == 1)
        scratch.copyFrom (1, 0, scratch, 0, 0, numSamples);

    if (juce::approximatelyEqual (mix, 1.0f) && juce::approximatelyEqual (target, 1.0f))
    {
        for (int ch = 0; ch < 2; ++ch)
            io.copyFrom (ch, 0, scratch, ch, 0, numSamples);
    }
    else
    {
        // 开关时在一个音频块内交叉淡化，避免爆音
        const float step = (target - mix) / (float) numSamples;
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = io.getWritePointer (ch);
            const auto* w = scratch.getReadPointer (ch);
            float m = mix;
            for (int i = 0; i < numSamples; ++i)
            {
                m += step;
                d[i] = d[i] * (1.0f - m) + w[i] * m;
            }
        }
        mix = target;
    }
}

// =============================================================================
EffectChain::EffectChain (AudioEngine& e) : engine (e)
{
    juce::addDefaultFormatsToManager (formatManager);
}

EffectChain::~EffectChain()
{
    // 音频回调先停掉（由 MainComponent 保证），再释放
    slots.clear();
}

EffectSlotPtr EffectChain::finishCreate (EffectSlotPtr slot)
{
    slot->prepare (engine.getSampleRate(), engine.getBlockSize());
    return slot;
}

EffectSlotPtr EffectChain::createBuiltin (const juce::String& id)
{
    auto fx = createBuiltinEffect (id);
    if (fx == nullptr)
        return nullptr;
    return finishCreate (std::make_shared<EffectSlot> (EffectSlot::Kind::builtin, std::move (fx), id));
}

EffectSlotPtr EffectChain::createNam (const juce::File& file, juce::String& error)
{
    auto p = std::make_unique<NamProcessor>();
    if (! p->loadModel (file, error))
        return nullptr;
    return finishCreate (std::make_shared<EffectSlot> (EffectSlot::Kind::nam, std::move (p), file.getFullPathName()));
}

EffectSlotPtr EffectChain::createIr (const juce::File& file, juce::String& error)
{
    auto p = std::make_unique<IrProcessor>();
    if (! p->loadIr (file, error))
        return nullptr;
    return finishCreate (std::make_shared<EffectSlot> (EffectSlot::Kind::ir, std::move (p), file.getFullPathName()));
}

EffectSlotPtr EffectChain::createPlugin (const juce::PluginDescription& desc, juce::String& error)
{
    auto instance = formatManager.createPluginInstance (desc, engine.getSampleRate(), engine.getBlockSize(), error);
    if (instance == nullptr)
    {
        if (error.isEmpty())
            error = TR ("Could not load plug-in: ", "无法加载插件：") + desc.name;
        return nullptr;
    }

    auto slot = std::make_shared<EffectSlot> (EffectSlot::Kind::vst3, std::move (instance), desc.createIdentifierString());
    slot->pluginDescription = desc;
    return finishCreate (slot);
}

// ---- 编辑 --------------------------------------------------------------------
void EffectChain::insert (EffectSlotPtr slot, int index)
{
    if (slot == nullptr)
        return;
    if (index < 0 || index > (int) slots.size())
        index = (int) slots.size();
    slots.insert (slots.begin() + index, std::move (slot));
    commit();
}

void EffectChain::remove (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) slots.size()))
        return;
    if (onSlotRemoved)
        onSlotRemoved (slots[(size_t) index].get());
    slots.erase (slots.begin() + index);
    commit();
}

void EffectChain::move (int from, int to)
{
    const int n = (int) slots.size();
    if (! juce::isPositiveAndBelow (from, n) || from == to)
        return;
    to = juce::jlimit (0, n - 1, to);
    auto s = slots[(size_t) from];
    slots.erase (slots.begin() + from);
    slots.insert (slots.begin() + to, s);
    commit();
}

void EffectChain::replace (int index, EffectSlotPtr slot)
{
    if (slot == nullptr || ! juce::isPositiveAndBelow (index, (int) slots.size()))
        return;
    slot->bypassed.store (slots[(size_t) index]->bypassed.load());
    if (onSlotRemoved)
        onSlotRemoved (slots[(size_t) index].get());
    slots[(size_t) index] = std::move (slot);
    commit();
}

void EffectChain::setBypassed (int index, bool b)
{
    if (juce::isPositiveAndBelow (index, (int) slots.size()))
    {
        slots[(size_t) index]->bypassed.store (b);
        sendChangeMessage();   // 开关不需要新快照，音频线程直接读原子变量
    }
}

void EffectChain::clear()
{
    if (onSlotRemoved)
        for (auto& s : slots)
            onSlotRemoved (s.get());
    slots.clear();
    commit();
}

void EffectChain::commit()
{
    auto snap = std::make_unique<ChainSnapshot>();
    snap->slots = slots;
    for (auto& s : slots)
        if (s != nullptr)
            snap->latencySamples += s->getLatencySamples();
    engine.publishChain (std::move (snap));
    sendChangeMessage();
}

void EffectChain::collectGarbage()
{
    engine.collectGarbage();
}

// ---- 预设 --------------------------------------------------------------------
std::unique_ptr<juce::XmlElement> EffectChain::toXml() const
{
    auto xml = std::make_unique<juce::XmlElement> ("Chain");

    for (auto& s : slots)
    {
        auto* e = xml->createNewChildElement ("Slot");
        e->setAttribute ("bypass", s->bypassed.load());

        switch (s->kind)
        {
            case EffectSlot::Kind::builtin: e->setAttribute ("kind", "builtin"); e->setAttribute ("id", s->typeId); break;
            case EffectSlot::Kind::nam:     e->setAttribute ("kind", "nam");     e->setAttribute ("path", makePortablePath (juce::File (s->typeId))); break;
            case EffectSlot::Kind::ir:      e->setAttribute ("kind", "ir");      e->setAttribute ("path", makePortablePath (juce::File (s->typeId))); break;
            case EffectSlot::Kind::vst3:
                e->setAttribute ("kind", "vst3");
                e->addChildElement (s->pluginDescription.createXml().release());
                break;
        }

        juce::MemoryBlock state;
        s->getProcessor().getStateInformation (state);
        e->setAttribute ("state", state.toBase64Encoding());
    }

    return xml;
}

void EffectChain::loadFromXml (const juce::XmlElement& xml, juce::StringArray& problems)
{
    std::vector<EffectSlotPtr> loaded;

    for (auto* e : xml.getChildWithTagNameIterator ("Slot"))
    {
        const auto kind = e->getStringAttribute ("kind");
        EffectSlotPtr slot;
        juce::String error;

        if (kind == "builtin")
        {
            slot = createBuiltin (e->getStringAttribute ("id"));
            if (slot == nullptr)
                error = TR ("Unknown built-in effect: ", "未知的内置效果器：") + e->getStringAttribute ("id");
        }
        else if (kind == "nam")
        {
            slot = createNam (resolvePortablePath (e->getStringAttribute ("path")), error);
        }
        else if (kind == "ir")
        {
            slot = createIr (resolvePortablePath (e->getStringAttribute ("path")), error);
        }
        else if (kind == "vst3")
        {
            juce::PluginDescription desc;
            if (auto* d = e->getChildByName ("PLUGIN"); d != nullptr && desc.loadFromXml (*d))
            {
                // 优先用已扫描列表里的信息（路径可能更新过）
                if (auto known = knownPlugins.getTypeForIdentifierString (desc.createIdentifierString()))
                    desc = *known;
                slot = createPlugin (desc, error);
            }
            else
            {
                error = TR ("Plug-in data is corrupted", "插件信息损坏");
            }
        }

        if (slot == nullptr)
        {
            problems.add (error);
            continue;
        }

        juce::MemoryBlock state;
        if (e->hasAttribute ("state") && state.fromBase64Encoding (e->getStringAttribute ("state")) && state.getSize() > 0)
        {
            slot->getProcessor().setStateInformation (state.getData(), (int) state.getSize());
        }
        else if (auto* params = e->getChildByName ("Params"))
        {
            // 手写的预设可以直接用 <Params 参数id="值" .../>
            juce::AudioProcessor::copyXmlToBinary (*params, state);
            slot->getProcessor().setStateInformation (state.getData(), (int) state.getSize());
        }

        slot->bypassed.store (e->getBoolAttribute ("bypass", false));
        loaded.push_back (slot);
    }

    if (onSlotRemoved)
        for (auto& s : slots)
            onSlotRemoved (s.get());

    slots = std::move (loaded);
    commit();
}

// =============================================================================
juce::File EffectChain::getAppFolder()
{
    return AppPaths::moduleDir();
}

juce::File EffectChain::getToneLibrary()
{
    return AppPaths::libraryDir();
}

juce::String EffectChain::makePortablePath (const juce::File& f)
{
    return AppPaths::makePortable (f);
}

juce::File EffectChain::resolvePortablePath (const juce::String& path)
{
    return AppPaths::resolvePortable (path);
}
