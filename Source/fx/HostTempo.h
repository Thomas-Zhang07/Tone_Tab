#pragma once

#include <atomic>

// 全局速度（BPM），由鼓机设置；延迟、颤音等“同步鼓机”时读取
struct HostTempo
{
    static inline std::atomic<float> bpm { 100.0f };
};
