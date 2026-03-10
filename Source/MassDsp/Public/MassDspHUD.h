#pragma once

#include "CoreMinimal.h"

#include "GameFramework/HUD.h"
#include "UI/MassDspBuildingWidget.h"
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

    /** 绘制槽口吸附指示圈 */
    void DrawBeltSnapIndicator();

    // ─── 传送带吸附状态（每帧 Tick 更新）──────────────────────────────

    /** 当前帧吸附到的世界坐标（吸附成功时 = 槽口位置；否则 = 鼠标射线落点） */
    FVector BeltHoverSnapLocation = FVector::ZeroVector;

    /** 当前帧是否成功吸附到有效槽口 */
    bool bBeltHoverSnapped = false;

    /** 当前帧吸附槽口的世界旋转（吸附失败时为 Identity） */
    FQuat BeltHoverSnapRotation = FQuat::Identity;

    /** 当前帧吸附槽口的延伸距离（cm）；吸附失败时为 0 */
    float BeltHoverSnapExtend = 0.f;

    // 数字键 1-6：切换建造模式
    void OnKey1Pressed(); // 矿机
    void OnKey2Pressed(); // 合成台
    void OnKey3Pressed(); // 仓库
    void OnKey4Pressed(); // 低速传送带
    void OnKey5Pressed(); // 中速传送带
    void OnKey6Pressed(); // 高速传送带

    bool bPress8, bPress9;

    void OnKey8Pressed(); // TestCase1
    void OnKey9Pressed(); // TestCase2

    // 鼠标左键：确认放置 / 选择槽口
    void OnLeftMouseButtonPressed();
    // 鼠标右键：取消
    void OnRightMouseButtonPressed();
    // 鼠标滚轮：预览建筑绕 Z 轴旋转
    void OnMouseWheelUp();
    void OnMouseWheelDown();

    // F 键：打开 / 关闭最近建筑的交互界面
    void OnKeyFPressed();

    /** 预览建筑时绘制所有槽口指示圈 */
    void DrawBuildingPreviewSlots();

    /** 每次滚轮的旋转步进（度） */
    static constexpr float BuildingRotationStep = 15.f;

    // ─── 建筑交互 UI ──────────────────────────────────────────────────

    /** 按 F 时搜索最近建筑的交互半径（cm） */
    static constexpr float BuildingInteractRadius = 4000.f;

    /** 绘制可交互建筑的 HUD 提示（每帧在 DrawHUD 中调用） */
    void DrawInteractionHint();

    /**
     * 视锥检测：将世界坐标投影到屏幕，判断是否落在屏幕中央 2/3 区域内。
     * 结合半径检测一同使用，避免身后的建筑触发提示/交互。
     */
    bool IsBuildingInViewCone(const FVector& WorldLoc) const;

    /** 当前已打开的建筑交互 Widget（同时只存在一个） */
    UPROPERTY()
    TObjectPtr<UMassDspBuildingWidget> CurrentBuildingWidget;

    // ─── 高度自适应镜头移动速度 ──────────────────────────────────────

    /** 速度系数：TargetSpeed = CameraHeight × SpeedFactor */
    float CameraSpeedFactor = 2.0f;

    /** 最低移动速度（cm/s），防止高度极低时镜头无法移动 */
    float CameraMinSpeed = 300.f;

    /** 最高移动速度上限（cm/s） */
    float CameraMaxSpeed = 100000.f;

    /** 速度平滑插值速率（越大越跟手，越小过渡越柔和） */
    float CameraSpeedSmoothRate = 8.f;

    /**
     * true：向下射线打地面，用「离地高度」计算速度（地形起伏时更准确）；
     * false：直接用 Pawn 的 Z 坐标。
     */
    bool bUseSurfaceTraceForHeight = true;

    /** 当前平滑后的移动速度（运行时内部状态，不需要配置） */
    float CurrentCameraSpeed = 500.f;

    /** 每帧根据高度自适应速度驱动 WASD 镜头移动 */
    void UpdateCameraMovement(float DeltaSeconds);

    /** 获取相机离地高度（cm） */
    float GetCameraHeight() const;

    /** 根据高度计算目标移动速度（cm/s） */
    float GetAdaptiveCameraSpeed(float Height) const;
};
