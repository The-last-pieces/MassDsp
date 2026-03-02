#pragma once

#include "CoreMinimal.h"

#include "GameFramework/HUD.h"
#include "Subsystems/MassDspManager.h"
#include "MassDspHUD.generated.h"

UCLASS()
class MASSDSP_API AMassDspHUD : public AHUD
{
    GENERATED_BODY()

public:
    AMassDspHUD();

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void DrawHUD() override;

private:
    // ─── 帧时间历史 ────────────────────────────────────────────────────
    TArray<float> FrameTimeHistory;
    int32 MaxHistorySize = 300;
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

    // ─── 建造系统交互 ─────────────────────────────────────────────────

    /** 当前鼠标在世界中射线检测的落点（每帧 Tick 更新） */
    FVector CachedHitLocation = FVector::ZeroVector;

    /** 当前预览建筑的旋转（可扩展为按 R 键旋转） */
    FRotator CurrentBuildingRotation = FRotator::ZeroRotator;

    /** 每帧更新预览位置 */
    void UpdateBuildPreview(float DeltaSeconds);

    /** 将鼠标位置转换为世界坐标（优先射线检测，失败时投影到 Z=0 平面） */
    bool GetMouseWorldHitLocation(FVector& OutHitLocation) const;

    /** 在 HUD 上绘制当前建造模式提示 */
    void DrawBuildSystemHint();

    // 数字键 1-6：切换建造模式
    void OnKey1Pressed(); // 矿机
    void OnKey2Pressed(); // 合成台
    void OnKey3Pressed(); // 仓库
    void OnKey4Pressed(); // 低速传送带
    void OnKey5Pressed(); // 中速传送带
    void OnKey6Pressed(); // 高速传送带

    // 鼠标左键：确认放置 / 选择槽口
    void OnLeftMouseButtonPressed();
    // 鼠标右键：取消
    void OnRightMouseButtonPressed();
};
