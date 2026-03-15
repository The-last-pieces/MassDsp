#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "UI/MassDspHotbarWidget.h"
#include "MassDspHUD.generated.h"

class UMassDspInventoryWidget;
class UMassDspSystemStatsWidget;
class UMassDspTechTreeWidget;

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

public:
    //  帧时间历史（DrawHUD 每帧追加）
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

    //  底部热键栏 
    UPROPERTY()
    TObjectPtr<UMassDspHotbarWidget> HotbarWidget;

    UPROPERTY()
    TObjectPtr<UMassDspInventoryWidget> InventoryWidget;

    UPROPERTY()
    TObjectPtr<UMassDspSystemStatsWidget> SystemStatsWidget;

    UPROPERTY()
    TObjectPtr<UMassDspTechTreeWidget> TechTreeWidget;

    //  高度自适应镜头移动 
    float CameraSpeedFactor = 2.0f;
    float CameraMinSpeed = 300.f;
    float CameraMaxSpeed = 100000.f;
    float CameraSpeedSmoothRate = 8.f;
    bool bUseSurfaceTraceForHeight = true;
    float CurrentCameraSpeed = 500.f;

    void UpdateCameraMovement(float DeltaSeconds);
    float GetCameraHeight() const;
    float GetAdaptiveCameraSpeed(float Height) const;

private:
    void HandleInteractKey();
    void ToggleInventoryWidget();
    void ToggleSystemStatsWidget();
    void ToggleTechTreeWidget();

    //  Canvas 绘制（只负责绘制，业务状态读取 HotbarWidget）
    void DrawPersistentFps();
    void DrawBuildSystemHint();
    void DrawBuildingPreviewSlots();
    void DrawBeltSnapIndicator();
    void DrawInteractionHint();
};
