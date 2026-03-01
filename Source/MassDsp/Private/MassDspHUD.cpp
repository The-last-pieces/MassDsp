#include "MassDspHUD.h"

#include "Engine/Engine.h"

AMassDspHUD::AMassDspHUD()
{
}

void AMassDspHUD::DrawHUD()
{
    Super::DrawHUD();

    if (!Canvas) return;

    const float DeltaTime = GetWorld()->GetDeltaSeconds();
    if (DeltaTime <= 0.0f) return;

    // 记录帧时间
    FrameTimeHistory.Add(DeltaTime);
    if (FrameTimeHistory.Num() > MaxHistorySize)
    {
        FrameTimeHistory.RemoveAt(0);
    }

    // 定期更新统计
    TimeSinceLastUpdate += DeltaTime;
    if (TimeSinceLastUpdate >= StatUpdateInterval)
    {
        UpdateFrameStats();
        TimeSinceLastUpdate = 0.0f;
    }

    // 使用 Canvas 绘制（Shipping 下可见）
    const float CurrentFPS = 1.0f / DeltaTime;
    const FString StatsText = FString::Printf(
        TEXT("Current: %.1f FPS | Avg: %.1f FPS | 1%% Low: %.1f FPS"),
        CurrentFPS, AverageFPS, OnePercentLowFPS
    );

    constexpr float PosX = 10.0f;
    constexpr float PosY = 10.0f;

    // 绘制阴影（提升可读性）
    DrawText(StatsText, FLinearColor::Black, PosX + 1.0f, PosY + 1.0f, GEngine->GetSmallFont(), 1.5f);
    DrawText(StatsText, FLinearColor::Yellow, PosX, PosY, GEngine->GetSmallFont(), 1.5f);
}

void AMassDspHUD::UpdateFrameStats()
{
    if (FrameTimeHistory.Num() < 10) return;

    // 计算平均 FPS
    float TotalFrameTime = 0.0f;
    for (const float FrameTime : FrameTimeHistory)
    {
        TotalFrameTime += FrameTime;
    }
    AverageFPS = static_cast<float>(FrameTimeHistory.Num()) / TotalFrameTime;

    // 计算 1% Low FPS
    TArray<float> SortedFrameTimes = FrameTimeHistory;
    SortedFrameTimes.Sort([](float A, float B) { return A > B; }); // 降序

    const int32 OnePercentCount = FMath::Max(1, FMath::CeilToInt(SortedFrameTimes.Num() * 0.01f));
    float OnePercentSum = 0.0f;
    for (int32 i = 0; i < OnePercentCount; ++i)
    {
        OnePercentSum += SortedFrameTimes[i];
    }
    OnePercentLowFPS = static_cast<float>(OnePercentCount) / OnePercentSum;
}
