#pragma once

#include "CoreMinimal.h"

#include "GameFramework/HUD.h"
#include "MassDspHUD.generated.h"

UCLASS()
class MASSDSP_API AMassDspHUD : public AHUD
{
    GENERATED_BODY()

public:
    AMassDspHUD();

protected:
    virtual void DrawHUD() override;

private:
    // 帧时间历史
    TArray<float> FrameTimeHistory;
    int32 MaxHistorySize = 300; // 保留约5秒数据(60fps)
    float StatUpdateInterval = 1.0f;
    float TimeSinceLastUpdate = 0.0f;

    float AverageFPS = 0.0f;
    float OnePercentLowFPS = 0.0f;

    // 游戏统计缓存（每秒刷新）
    int32 CachedBuildingCount = 0;
    int32 CachedBeltCount = 0;
    int32 CachedBeltItemCount = 0;

    void UpdateFrameStats();
    void UpdateGameStats();
};
