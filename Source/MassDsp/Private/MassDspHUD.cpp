#include "MassDspHUD.h"

#include "Engine/Engine.h"
#include "Subsystems/MassDspManager.h"

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
        UpdateGameStats();
        TimeSinceLastUpdate = 0.0f;
    }

    // 使用 Canvas 绘制（Shipping 下可见）
    const float CurrentFPS = 1.0f / DeltaTime;
    const FString FpsText = FString::Printf(
        TEXT("Current: %.1f FPS | Avg: %.1f FPS | 1%% Low: %.1f FPS"),
        CurrentFPS, AverageFPS, OnePercentLowFPS
    );
    const FString GameText = FString::Printf(TEXT("Buildings: %d | Belts: %d | Belt Items: %d"), CachedBuildingCount, CachedBeltCount, CachedBeltItemCount);

    constexpr float PosX = 10.0f;
    constexpr float PosY = 10.0f;
    constexpr float LineStep = 20.0f;

    auto DrawLine = [&](const FString& Text, float Y)
    {
        DrawText(Text, FLinearColor::Black, PosX + 1.0f, Y + 1.0f, GEngine->GetSmallFont(), 1.5f);
        DrawText(Text, FLinearColor::Yellow, PosX, Y, GEngine->GetSmallFont(), 1.5f);
    };

    DrawLine(FpsText, PosY);
    DrawLine(GameText, PosY + LineStep);
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

void AMassDspHUD::UpdateGameStats()
{
    UMassDspManager* Manager = GetWorld() ? GetWorld()->GetSubsystem<UMassDspManager>() : nullptr;
    if (!Manager) return;

    CachedBuildingCount = Manager->BuildingEntityCount;
    CachedBeltCount = Manager->BeltEntityRegistry.Num();

    int32 TotalItems = 0;
    for (const auto& Pair : Manager->BeltEntityRegistry)
    {
        TotalItems += Pair.Value.ItemCache.Num();
    }
    CachedBeltItemCount = TotalItems;
}
