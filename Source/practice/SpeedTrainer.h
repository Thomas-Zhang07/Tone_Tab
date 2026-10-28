#pragma once

#include <juce_core/juce_core.h>

// 速度阶梯：A-B 循环每过一遍记一次；连续过够几遍（开着判定时还要命中率够高）就提速一档，到目标速度为止。
struct SpeedTrainer
{
    bool enabled = false;
    float start = 0.6f;          // 起始速度（1 = 原速）
    float step = 0.05f;          // 每档提多少
    float target = 1.0f;         // 目标速度
    float needAccuracy = 0.9f;   // 开着判定时，一遍的命中率要达到多少才算“过”
    int passesPerStep = 2;       // 连续过几遍提一档

    float current = 0.6f;
    int goodPasses = 0;

    void restart()               { current = start; goodPasses = 0; }
    bool finished() const        { return current >= target - 1.0e-4f; }

    // 一遍结束。accuracy < 0 表示没开判定（只数遍数）。返回 true = 速度变了
    bool onPass (float accuracy)
    {
        if (! enabled || finished())
            return false;
        if (accuracy >= 0.0f && accuracy < needAccuracy)
        {
            goodPasses = 0;              // 没达标：这一档重新数
            return false;
        }
        if (++goodPasses < juce::jmax (1, passesPerStep))
            return false;
        goodPasses = 0;
        current = juce::jmin (target, current + step);
        return true;
    }
};
