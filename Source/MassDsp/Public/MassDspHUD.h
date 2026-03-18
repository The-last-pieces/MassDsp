#pragma once

#include "CoreMinimal.h"
#include "Containers/Deque.h"
#include "GameFramework/HUD.h"
#include "UI/MassDspHotbarWidget.h"
#include "MassDspHUD.generated.h"

struct FMassDspAsyncSaveLoadResult;
class UMassDspInventoryWidget;
class UMassDspSystemStatsWidget;
class UMassDspTechTreeWidget;

struct FSaveDebugMessageEntry
{
    int32 MessageId = INDEX_NONE;
    int64 Sequence = 0;
    FString Message;
    FColor Color = FColor::White;
    double ExpireAtSeconds = 0.0;
};

struct FSaveDebugExpiryEntry
{
    int32 MessageId = INDEX_NONE;
    int64 Sequence = 0;
    double ExpireAtSeconds = 0.0;
};

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
    float CameraShiftSpeedMultiplier = 10.f;
    bool bUseSurfaceTraceForHeight = true;
    float CurrentCameraSpeed = 500.f;

    void UpdateCameraMovement(float DeltaSeconds);
    float GetCameraHeight() const;
    float GetAdaptiveCameraSpeed(float Height) const;

private:
    void HandleAsyncSaveFinished(const FMassDspAsyncSaveLoadResult& Result);
    void HandleAsyncLoadFinished(const FMassDspAsyncSaveLoadResult& Result);
    void HandleInteractKey();
    void HandleQuickSaveKey();
    void HandleQuickLoadKey();
    void ToggleInventoryWidget();
    void ToggleSystemStatsWidget();
    void ToggleTechTreeWidget();
    void ShowSaveDebugMessage(const FString& Message, const FColor& Color, float DurationSeconds = 5.0f);
    void TickSaveDebugMessages();
    void CompactSaveDebugMessageOrder();
    void DrawSaveDebugMessages(float StartY);

    //  Canvas 绘制（只负责绘制，业务状态读取 HotbarWidget）
    void DrawPersistentFps();
    float DrawBoundKeyHints(float StartY);
    void DrawBuildSystemHint();
    void DrawDemolishTargetHint();
    void DrawBuildingPreviewSlots();
    void DrawBeltSnapIndicator();
    void DrawInteractionHint();
    bool GetScreenCenterWorldRay(FVector& OutOrigin, FVector& OutDirection) const;

private:
    TMap<int32, FSaveDebugMessageEntry> SaveDebugMessagesById;
    TDeque<int32> SaveDebugMessageOrder;
    TArray<FSaveDebugExpiryEntry> SaveDebugExpiryHeap;
    int32 NextSaveDebugMessageId = 1;
    int64 NextSaveDebugSequence = 1;
    bool bSaveDebugOrderDirty = false;
};
