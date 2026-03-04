#include "MassDspHUD.h"

#include "Engine/Engine.h"
#include "Subsystems/MassDspManager.h"
#include "Actors/MassDspBuilding.h"
#include "MassDspGameMode.h"
#include "UI/MassDspBuildingWidget.h"

#include "GameFramework/PlayerController.h"
#include "Components/InputComponent.h"
#include "InputCoreTypes.h"
#include "Engine/Canvas.h"

AMassDspHUD::AMassDspHUD()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  生命周期
// ─────────────────────────────────────────────────────────────────────────────

void AMassDspHUD::BeginPlay()
{
    Super::BeginPlay();

    APlayerController* PC = GetOwningPlayerController();
    if (!PC) return;

    // 启用 HUD 输入，UE 会自动创建 InputComponent
    EnableInput(PC);

    if (!InputComponent) return;

    // 数字键 1-3：建筑
    InputComponent->BindKey(EKeys::One, IE_Pressed, this, &AMassDspHUD::OnKey1Pressed);
    InputComponent->BindKey(EKeys::Two, IE_Pressed, this, &AMassDspHUD::OnKey2Pressed);
    InputComponent->BindKey(EKeys::Three, IE_Pressed, this, &AMassDspHUD::OnKey3Pressed);
    // 数字键 4-6：传送带
    InputComponent->BindKey(EKeys::Four, IE_Pressed, this, &AMassDspHUD::OnKey4Pressed);
    InputComponent->BindKey(EKeys::Five, IE_Pressed, this, &AMassDspHUD::OnKey5Pressed);
    InputComponent->BindKey(EKeys::Six, IE_Pressed, this, &AMassDspHUD::OnKey6Pressed);
    // 鼠标点击
    InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &AMassDspHUD::OnLeftMouseButtonPressed);
    InputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &AMassDspHUD::OnRightMouseButtonPressed);
    // 鼠标滚轮：预览建筑旋转
    InputComponent->BindKey(EKeys::MouseScrollUp, IE_Pressed, this, &AMassDspHUD::OnMouseWheelUp);
    InputComponent->BindKey(EKeys::MouseScrollDown, IE_Pressed, this, &AMassDspHUD::OnMouseWheelDown);
    // F 键：打开最近建筑交互界面
    InputComponent->BindKey(EKeys::F, IE_Pressed, this, &AMassDspHUD::OnKeyFPressed);
}

void AMassDspHUD::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    UpdateBuildPreview(DeltaSeconds);

    // 若关闭按钮在 Widget 内部触发了 CloseWidget()，RemoveFromParent 后
    // HUD 的指针并不会自动清零，这里每帧检测一次并修正。
    if (CurrentBuildingWidget && !CurrentBuildingWidget->IsInViewport())
    {
        CurrentBuildingWidget = nullptr;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  建造预览更新（每帧）
// ─────────────────────────────────────────────────────────────────────────────

bool AMassDspHUD::GetMouseWorldHitLocation(FVector& OutHitLocation) const
{
    APlayerController* PC = GetOwningPlayerController();
    if (!PC) return false;

    // 从屏幕中心投射射线，而非跟随鼠标光标，保证交互点始终在屏幕正中
    int32 ViewW = 0, ViewH = 0;
    PC->GetViewportSize(ViewW, ViewH);
    const float CenterX = ViewW * 0.5f;
    const float CenterY = ViewH * 0.5f;

    FVector WorldLoc, WorldDir;
    if (!PC->DeprojectScreenPositionToWorld(CenterX, CenterY, WorldLoc, WorldDir)) return false;

    // 射线检测（与场景可见几何体交叉）
    FHitResult Hit;
    const FVector TraceEnd = WorldLoc + WorldDir * 100000.f;
    if (GetWorld()->LineTraceSingleByChannel(Hit, WorldLoc, TraceEnd, ECC_Visibility))
    {
        OutHitLocation = Hit.Location;
        return true;
    }

    // 回退：投影到 Z=0 地面平面
    if (FMath::Abs(WorldDir.Z) > SMALL_NUMBER)
    {
        const float T = -WorldLoc.Z / WorldDir.Z;
        if (T > 0.f) OutHitLocation = WorldLoc + WorldDir * T;
        else OutHitLocation = WorldLoc;
    }
    else
    {
        OutHitLocation = WorldLoc;
    }
    return true;
}

void AMassDspHUD::UpdateBuildPreview(float /*DeltaSeconds*/)
{
    UMassDspManager* Manager = GetWorld() ? GetWorld()->GetSubsystem<UMassDspManager>() : nullptr;
    if (!Manager || Manager->GetCurrentPlaceMode() == EBuildPlaceMode::None) return;

    if (!GetMouseWorldHitLocation(CachedHitLocation)) return;

    if (Manager->IsPreviewingBuilding())
    {
        Manager->UpdateBuildingPreviewTransform(FTransform(CurrentBuildingRotation, CachedHitLocation));
        return;
    }

    if (!Manager->IsPreviewingBelt()) return;

    // ── 传送带两阶段均做就近吸附 ──────────────────────────────────────
    // Phase 1（未选起点）：吸附最近 Output 槽，供视觉高亮与左键选中
    // Phase 2（已选起点）：吸附最近 Input 槽，优先让终点落到槽口上
    constexpr float SnapRadius = 200.f;
    const EBuildingSlotType TargetSlotType = Manager->BeltHasStartSlot()
                                                 ? EBuildingSlotType::Input
                                                 : EBuildingSlotType::Output;

    FMassEntityHandle DummyEntity;
    int32 DummySlotIndex;
    FVector SnappedPos;
    FQuat SnappedRot = FQuat::Identity;
    float SnappedExtend = 0.f;

    bBeltHoverSnapped = Manager->FindNearestBuildingSlot(
        CachedHitLocation, TargetSlotType, SnapRadius,
        DummyEntity, DummySlotIndex, SnappedPos, SnappedRot, SnappedExtend);

    BeltHoverSnapLocation = bBeltHoverSnapped ? SnappedPos : CachedHitLocation;
    BeltHoverSnapRotation = bBeltHoverSnapped ? SnappedRot : FQuat::Identity;
    BeltHoverSnapExtend = bBeltHoverSnapped ? SnappedExtend : 0.f;

    // Phase 2：每帧用（已吸附的）终点坐标+旋转重建预览网格
    if (Manager->BeltHasStartSlot())
    {
        Manager->UpdateBeltPreviewEndPoint(BeltHoverSnapLocation, BeltHoverSnapRotation, BeltHoverSnapExtend);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  数字键处理
// ─────────────────────────────────────────────────────────────────────────────

void AMassDspHUD::OnKey1Pressed()
{
    // 矿机
    UMassDspManager* Manager = GetWorld()->GetSubsystem<UMassDspManager>();
    if (!Manager) return;
    GetMouseWorldHitLocation(CachedHitLocation);
    Manager->BeginPreviewBuilding(EBuildingType::Miner, FTransform(CurrentBuildingRotation, CachedHitLocation));
}

void AMassDspHUD::OnKey2Pressed()
{
    // 合成台
    UMassDspManager* Manager = GetWorld()->GetSubsystem<UMassDspManager>();
    if (!Manager) return;
    GetMouseWorldHitLocation(CachedHitLocation);
    Manager->BeginPreviewBuilding(EBuildingType::Assembler, FTransform(CurrentBuildingRotation, CachedHitLocation));
}

void AMassDspHUD::OnKey3Pressed()
{
    // 仓库
    UMassDspManager* Manager = GetWorld()->GetSubsystem<UMassDspManager>();
    if (!Manager) return;
    GetMouseWorldHitLocation(CachedHitLocation);
    Manager->BeginPreviewBuilding(EBuildingType::Storage, FTransform(CurrentBuildingRotation, CachedHitLocation));
}

void AMassDspHUD::OnKey4Pressed()
{
    // 低速传送带（Normal）
    UMassDspManager* Manager = GetWorld()->GetSubsystem<UMassDspManager>();
    if (Manager) Manager->BeginPreviewBelt(EBeltType::Normal);
}

void AMassDspHUD::OnKey5Pressed()
{
    // 中速传送带（Fast）
    UMassDspManager* Manager = GetWorld()->GetSubsystem<UMassDspManager>();
    if (Manager) Manager->BeginPreviewBelt(EBeltType::Fast);
}

void AMassDspHUD::OnKey6Pressed()
{
    // 高速传送带（Express）
    UMassDspManager* Manager = GetWorld()->GetSubsystem<UMassDspManager>();
    if (Manager) Manager->BeginPreviewBelt(EBeltType::Express);
}

// ─────────────────────────────────────────────────────────────────────────────
//  鼠标点击处理
// ─────────────────────────────────────────────────────────────────────────────

void AMassDspHUD::OnLeftMouseButtonPressed()
{
    UMassDspManager* Manager = GetWorld() ? GetWorld()->GetSubsystem<UMassDspManager>() : nullptr;
    if (!Manager) return;

    GetMouseWorldHitLocation(CachedHitLocation);

    switch (Manager->GetCurrentPlaceMode())
    {
    case EBuildPlaceMode::Building:
        {
            // 左键确认建筑放置
            FMassEntityHandle NewEntity = Manager->ConfirmPreviewBuilding();
            if (NewEntity.IsValid())
            {
                UE_LOG(LogTemp, Log, TEXT("HUD: 建筑放置成功 [Entity=%d:%d]"),
                       NewEntity.Index, NewEntity.SerialNumber);
            }
            break;
        }
    case EBuildPlaceMode::Belt:
        {
            // 使用每帧 Tick 中已吸附的槽口坐标（而非裸鼠标位置），保证精确落点
            const FVector SelectPos = bBeltHoverSnapped ? BeltHoverSnapLocation : CachedHitLocation;
            const bool bBothSelected = Manager->SelectBeltSlot(SelectPos);
            if (bBothSelected)
            {
                FBeltHandle Handle = Manager->ConfirmPreviewBelt();
                if (Handle.IsValid())
                {
                    UE_LOG(LogTemp, Log, TEXT("HUD: 传送带连接成功 [Handle=%d]"), Handle.Index);
                }
            }
            break;
        }
    default:
        break;
    }
}

void AMassDspHUD::OnRightMouseButtonPressed()
{
    UMassDspManager* Manager = GetWorld() ? GetWorld()->GetSubsystem<UMassDspManager>() : nullptr;
    if (Manager) Manager->CancelAnyPreview();
}

// ─────────────────────────────────────────────────────────────────────────────
//  HUD 绘制
// ─────────────────────────────────────────────────────────────────────────────

void AMassDspHUD::DrawHUD()
{
    Super::DrawHUD();

    if (!Canvas) return;

    const float DeltaTime = GetWorld()->GetDeltaSeconds();
    if (DeltaTime <= 0.0f) return;

    // 记录帧时间
    FrameTimeHistory.Add(DeltaTime);
    if (FrameTimeHistory.Num() > MaxHistorySize)
        FrameTimeHistory.RemoveAt(0);

    // 定期更新统计
    TimeSinceLastUpdate += DeltaTime;
    if (TimeSinceLastUpdate >= StatUpdateInterval)
    {
        UpdateFrameStats();
        UpdateGameStats();
        TimeSinceLastUpdate = 0.0f;
    }

    // ── 性能与游戏统计 ──
    const float CurrentFPS = 1.0f / DeltaTime;
    const FString FpsText = FString::Printf(
        TEXT("Current: %.1f FPS | Avg: %.1f FPS | 1%% Low: %.1f FPS"),
        CurrentFPS, AverageFPS, OnePercentLowFPS);
    const FString GameText = FString::Printf(
        TEXT("Buildings: %d | Belts: %d | Belt Items: %d"),
        CachedBuildingCount, CachedBeltCount, CachedBeltItemCount);

    constexpr float PosX = 10.0f;
    constexpr float PosY = 10.0f;
    constexpr float LineStep = 20.0f;

    auto DrawLineText = [&](const FString& Text, float Y)
    {
        DrawText(Text, FLinearColor::Black, PosX + 1.0f, Y + 1.0f, GEngine->GetSmallFont(), 1.5f);
        DrawText(Text, FLinearColor::Yellow, PosX, Y, GEngine->GetSmallFont(), 1.5f);
    };

    DrawLineText(FpsText, PosY);
    DrawLineText(GameText, PosY + LineStep);

    // ── 建造模式提示 ──
    DrawBuildSystemHint();

    // ── 可交互建筑提示（未开 UI 时显示） ──
    DrawInteractionHint();

    // ── 预览建筑槽口指示圈 ──
    DrawBuildingPreviewSlots();

    // ── 传送带吸附指示圈 ──
    DrawBeltSnapIndicator();

    // ── [DEBUG] 视锥检测范围红框 ──
#if WITH_EDITOR
    {
        const float W = Canvas->SizeX;
        const float H = Canvas->SizeY;
        const float L = W / 2.f - W / 10.f; // 左
        const float R = W / 2.f + W / 10.f; // 右
        const float T = H / 2.f - H / 10.f; // 上
        const float B = H / 2.f + H / 10.f; // 下
        const FLinearColor DbgColor(1.f, 0.1f, 0.1f, 0.7f);
        constexpr float Th = 1.5f;
        DrawLine(L, T, R, T, DbgColor, Th);
        DrawLine(R, T, R, B, DbgColor, Th);
        DrawLine(R, B, L, B, DbgColor, Th);
        DrawLine(L, B, L, T, DbgColor, Th);
    }
#endif
}

void AMassDspHUD::DrawBuildSystemHint()
{
    UMassDspManager* Manager = GetWorld() ? GetWorld()->GetSubsystem<UMassDspManager>() : nullptr;
    if (!Manager) return;

    const EBuildPlaceMode Mode = Manager->GetCurrentPlaceMode();
    if (Mode == EBuildPlaceMode::None) return;

    // 组建提示文本
    FString ModeText;
    FLinearColor HintColor = FLinearColor::White;

    if (Mode == EBuildPlaceMode::Building)
    {
        static const TMap<EBuildingType, FString> BuildingNames =
        {
            {EBuildingType::Miner, TEXT("矿机")},
            {EBuildingType::Assembler, TEXT("合成台")},
            {EBuildingType::Storage, TEXT("仓库")},
        };
        const FString BuildingName = BuildingNames.FindRef(Manager->GetPreviewBuildingType());
        ModeText = FString::Printf(TEXT("[建造 %s] 左键确认放置  右键取消"), *BuildingName);
        HintColor = FLinearColor(0.4f, 1.f, 0.4f);
    }
    else if (Mode == EBuildPlaceMode::Belt)
    {
        static const TMap<EBeltType, FString> BeltNames =
        {
            {EBeltType::Normal, TEXT("低速带")},
            {EBeltType::Fast, TEXT("中速带")},
            {EBeltType::Express, TEXT("高速带")},
        };
        const FString BeltName = BeltNames.FindRef(Manager->GetPreviewBeltType());

        if (!Manager->BeltHasStartSlot())
        {
            ModeText = FString::Printf(TEXT("[%s] 左键点击 Output 槽口选择起点  右键取消"), *BeltName);
            HintColor = FLinearColor(0.4f, 0.8f, 1.f);
        }
        else
        {
            ModeText = FString::Printf(TEXT("[%s] 起点已选  左键点击 Input 槽口完成连接  右键取消"), *BeltName);
            HintColor = FLinearColor(1.f, 0.8f, 0.3f);
        }
    }

    const float CanvasW = Canvas->SizeX;
    const float CanvasH = Canvas->SizeY;

    // 居中底部显示
    const float TextScale = 1.6f;
    const float TextY = CanvasH - 60.f;
    float TextW = 0.f, TextH = 0.f;
    GetTextSize(ModeText, TextW, TextH, GEngine->GetSmallFont(), TextScale);
    const float TextX = (CanvasW - TextW) * 0.5f;

    DrawText(ModeText, FLinearColor::Black, TextX + 1.f, TextY + 1.f, GEngine->GetSmallFont(), TextScale);
    DrawText(ModeText, HintColor, TextX, TextY, GEngine->GetSmallFont(), TextScale);

    // 操作速查（右下角）
    const FString CheatSheet =
        TEXT("1矿机  2合成台  3仓库  |  4低速带  5中速带  6高速带");
    float CsW = 0.f, CsH = 0.f;
    GetTextSize(CheatSheet, CsW, CsH, GEngine->GetSmallFont(), 1.3f);
    const float CsX = CanvasW - CsW - 10.f;
    const float CsY = CanvasH - 30.f;
    DrawText(CheatSheet, FLinearColor::Black, CsX + 1.f, CsY + 1.f, GEngine->GetSmallFont(), 1.3f);
    DrawText(CheatSheet, FLinearColor(0.8f, 0.8f, 0.8f), CsX, CsY, GEngine->GetSmallFont(), 1.3f);
}

// ─────────────────────────────────────────────────────────────────────────────
//  可交互建筑 HUD 提示
// ─────────────────────────────────────────────────────────────────────────────

void AMassDspHUD::DrawInteractionHint()
{
    // 已打开交互 UI 时跳过
    if (CurrentBuildingWidget) return;
    if (!Canvas) return;

    UMassDspManager* Manager = GetWorld() ? GetWorld()->GetSubsystem<UMassDspManager>() : nullptr;
    if (!Manager) return;

    APlayerController* PC = GetOwningPlayerController();
    if (!PC || !PC->GetPawn()) return;

    const FVector PlayerLoc = PC->GetPawn()->GetActorLocation();

    FMassEntityHandle NearestEntity;
    EBuildingType NearestType = EBuildingType::None;
    FVector NearestLoc;

    if (!Manager->FindNearestBuilding(PlayerLoc, BuildingInteractRadius, NearestEntity, NearestType, NearestLoc,
                                      [this](const FVector& Loc) { return IsBuildingInViewCone(Loc); }))
        return;

    static const TMap<EBuildingType, FString> BuildingNames =
    {
        {EBuildingType::Miner, TEXT("矿机")},
        {EBuildingType::Storage, TEXT("仓库")},
        {EBuildingType::Assembler, TEXT("合成台")},
    };

    const FString BuildingName = BuildingNames.FindRef(NearestType);
    const float DistM = FVector::Dist(PlayerLoc, NearestLoc) / 100.f;
    const FString HintText = FString::Printf(TEXT("[F]  %s  (%.1f m)"), *BuildingName, DistM);

    constexpr float Scale = 1.6f;
    constexpr float PadX = 18.f;
    constexpr float PadY = 8.f;

    float TW = 0.f, TH = 0.f;
    GetTextSize(HintText, TW, TH, GEngine->GetSmallFont(), Scale);

    // 将建筑世界坐标投影到屏幕，文字水平居中、显示在投影点正上方
    FVector2D BuildingScreenPos;
    if (!PC->ProjectWorldLocationToScreen(NearestLoc, BuildingScreenPos, true))
        return;

    constexpr float OffsetY = 48.f; // 提示框底边距建筑投影点的像素距离
    const float TX = BuildingScreenPos.X - TW * 0.5f;
    const float TY = BuildingScreenPos.Y - TH - PadY * 2.f - OffsetY;

    // 半透明背景
    DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.50f),
             TX - PadX, TY - PadY, TW + PadX * 2.f, TH + PadY * 2.f);

    // 文字阴影 + 主色
    DrawText(HintText, FLinearColor::Black, TX + 1.f, TY + 1.f, GEngine->GetSmallFont(), Scale);
    DrawText(HintText, FLinearColor(1.f, 0.95f, 0.3f), TX, TY, GEngine->GetSmallFont(), Scale);
}

// ─────────────────────────────────────────────────────────────────────────────
//  视锥检测辅助
// ─────────────────────────────────────────────────────────────────────────────

bool AMassDspHUD::IsBuildingInViewCone(const FVector& WorldLoc) const
{
    APlayerController* PC = GetOwningPlayerController();
    if (!PC) return false;

    int32 ViewW = 0, ViewH = 0;
    PC->GetViewportSize(ViewW, ViewH);
    if (ViewW <= 0 || ViewH <= 0) return false;

    FVector2D ScreenPos;
    if (!PC->ProjectWorldLocationToScreen(WorldLoc, ScreenPos, /*bPlayerViewportRelative=*/true))
        return false;

    // 中央 2/3 区域：各轴偏中心不超过屏幕尺寸的 1/3
    const float HalfW = ViewW * 0.5f;
    const float HalfH = ViewH * 0.5f;
    const float LimitX = ViewW / 5; // = ViewW / 3
    const float LimitY = ViewH / 5; // = ViewH / 3

    return FMath::Abs(ScreenPos.X - HalfW) < LimitX
        && FMath::Abs(ScreenPos.Y - HalfH) < LimitY;
}

// ─────────────────────────────────────────────────────────────────────────────
//  鼠标滚轮：旋转预览建筑
// ─────────────────────────────────────────────────────────────────────────────

void AMassDspHUD::OnMouseWheelUp()
{
    UMassDspManager* Manager = GetWorld() ? GetWorld()->GetSubsystem<UMassDspManager>() : nullptr;
    if (!Manager || !Manager->IsPreviewingBuilding()) return;
    CurrentBuildingRotation.Yaw += BuildingRotationStep;
    Manager->UpdateBuildingPreviewTransform(FTransform(CurrentBuildingRotation, CachedHitLocation));
}

void AMassDspHUD::OnMouseWheelDown()
{
    UMassDspManager* Manager = GetWorld() ? GetWorld()->GetSubsystem<UMassDspManager>() : nullptr;
    if (!Manager || !Manager->IsPreviewingBuilding()) return;
    CurrentBuildingRotation.Yaw -= BuildingRotationStep;
    Manager->UpdateBuildingPreviewTransform(FTransform(CurrentBuildingRotation, CachedHitLocation));
}

// ─────────────────────────────────────────────────────────────────────────────
//  建筑交互界面（F 键）
// ─────────────────────────────────────────────────────────────────────────────

void AMassDspHUD::OnKeyFPressed()
{
    // 如果当前已有交互界面打开，先关闭它（切换逻辑）
    if (CurrentBuildingWidget)
    {
        CurrentBuildingWidget->CloseWidget();
        CurrentBuildingWidget = nullptr;
        return;
    }

    UMassDspManager* Manager = GetWorld() ? GetWorld()->GetSubsystem<UMassDspManager>() : nullptr;
    if (!Manager) return;

    APlayerController* PC = GetOwningPlayerController();
    if (!PC || !PC->GetPawn()) return;

    // 以玩家 Pawn 当前位置为中心搜索最近的建筑
    const FVector PlayerLoc = PC->GetPawn()->GetActorLocation();

    FMassEntityHandle NearestEntity;
    EBuildingType NearestType = EBuildingType::None;
    FVector NearestLoc;

    if (!Manager->FindNearestBuilding(PlayerLoc, BuildingInteractRadius, NearestEntity, NearestType, NearestLoc,
                                      [this](const FVector& Loc) { return IsBuildingInViewCone(Loc); }))
        return;

    // 通过 GameMode 拿 GameConfig
    AMassDspGameMode* GM = Cast<AMassDspGameMode>(GetWorld()->GetAuthGameMode());
    if (!GM || !GM->GameConfig) return;

    const FBuildingTypeConfig* BuildingCfg = GM->GameConfig->GetBuildingConfig(NearestType);
    if (!BuildingCfg || !BuildingCfg->InteractionWidgetClass) return;

    // 创建并添加到视口
    UMassDspBuildingWidget* BuildingWidget = CreateWidget<UMassDspBuildingWidget>(PC, BuildingCfg->InteractionWidgetClass);
    if (!BuildingWidget) return;

    // 传入目标实体，在 AddToViewport 前完成初始化（避免 NativeConstruct 时数据为空）
    BuildingWidget->InitWidget(NearestEntity, NearestType);
    BuildingWidget->AddToViewport();
    CurrentBuildingWidget = BuildingWidget;

    // 切换到 UI 输入模式，同时保留游戏输入（鼠标可操作 UI）
    FInputModeGameAndUI UIMode;
    UIMode.SetWidgetToFocus(CurrentBuildingWidget->TakeWidget());
    UIMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    PC->SetInputMode(UIMode);
    PC->bShowMouseCursor = true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  预览建筑槽口指示圈
// ─────────────────────────────────────────────────────────────────────────────

void AMassDspHUD::DrawBuildingPreviewSlots()
{
    UMassDspManager* Manager = GetWorld() ? GetWorld()->GetSubsystem<UMassDspManager>() : nullptr;
    if (!Manager || !Manager->IsPreviewingBuilding()) return;
    if (!Canvas) return;

    APlayerController* PC = GetOwningPlayerController();
    if (!PC) return;

    // 从 CDO 读取槽口定义
    TSubclassOf<AMassDspBuilding> BuildingClass =
        Manager->GetBuildingClassForType(Manager->GetPreviewBuildingType());
    if (!BuildingClass) return;

    const AMassDspBuilding* CDO = GetDefault<AMassDspBuilding>(BuildingClass);
    if (!CDO || CDO->Slots.IsEmpty()) return;

    // 当前预览 Transform（与 UpdateBuildPreview 保持同步）
    const FTransform PreviewTransform(CurrentBuildingRotation, CachedHitLocation);

    // 与 DrawBeltSnapIndicator 相同的屏幕圆圈绘制 lambda
    auto DrawWorldRing = [&](FVector WorldPos, FLinearColor Color, float Radius, float Thickness = 2.0f)
    {
        FVector2D ScreenPos;
        if (!PC->ProjectWorldLocationToScreen(WorldPos, ScreenPos, true)) return;

        constexpr int32 Segs = 16;
        for (int32 i = 0; i < Segs; ++i)
        {
            const float A0 = (i / (float)Segs) * 2.f * UE_PI;
            const float A1 = ((i + 1) / (float)Segs) * 2.f * UE_PI;
            DrawLine(
                ScreenPos.X + FMath::Cos(A0) * Radius,
                ScreenPos.Y + FMath::Sin(A0) * Radius,
                ScreenPos.X + FMath::Cos(A1) * Radius,
                ScreenPos.Y + FMath::Sin(A1) * Radius,
                Color, Thickness);
        }
    };

    for (const FBuildingSlotDef& SlotDef : CDO->Slots)
    {
        // 本地变换叠加预览变换 → 世界坐标
        const FTransform WorldSlotTransform = SlotDef.LocalTransform * PreviewTransform;
        const FVector SlotWorldPos = WorldSlotTransform.GetLocation();

        // 与 DrawBeltSnapIndicator 颜色约定保持一致
        const FLinearColor Color = (SlotDef.SlotType == EBuildingSlotType::Output)
                                       ? FLinearColor(0.1f, 1.0f, 0.3f) // Output → 绿
                                       : FLinearColor(0.2f, 0.6f, 1.0f); // Input  → 蓝
        DrawWorldRing(SlotWorldPos, Color, 12.f, 2.f);
    }
}

void AMassDspHUD::DrawBeltSnapIndicator()
{
    UMassDspManager* Manager = GetWorld() ? GetWorld()->GetSubsystem<UMassDspManager>() : nullptr;
    if (!Manager || !Manager->IsPreviewingBelt()) return;
    if (!Canvas) return;

    APlayerController* PC = GetOwningPlayerController();
    if (!PC) return;

    // ── 辅助：将世界坐标投影到屏幕并画圆圈 ──────────────────────────────
    auto DrawWorldRing = [&](FVector WorldPos, FLinearColor Color, float Radius, float Thickness = 2.0f)
    {
        FVector2D ScreenPos;
        if (!PC->ProjectWorldLocationToScreen(WorldPos, ScreenPos, true)) return;

        constexpr int32 Segs = 16;
        for (int32 i = 0; i < Segs; ++i)
        {
            const float A0 = (i / (float)Segs) * 2.f * UE_PI;
            const float A1 = ((i + 1) / (float)Segs) * 2.f * UE_PI;
            DrawLine(
                ScreenPos.X + FMath::Cos(A0) * Radius,
                ScreenPos.Y + FMath::Sin(A0) * Radius,
                ScreenPos.X + FMath::Cos(A1) * Radius,
                ScreenPos.Y + FMath::Sin(A1) * Radius,
                Color, Thickness);
        }
    };

    const bool bHasStart = Manager->BeltHasStartSlot();

    // ── 范围高亮：收集附近所有槽口并绘制半透明小圆 ────────────────────────
    {
        TArray<FVector> NearOutputLocs, NearInputLocs;
        const FVector SearchCenter = bHasStart ? Manager->GetBeltStartSlotLocation() : CachedHitLocation;
        Manager->GetNearbySlotsForHighlight(SearchCenter, UMassDspManager::SlotHighlightRadius,
                                            NearOutputLocs, NearInputLocs);

        // Phase 1 → 高亮 Output 槽（黄绿色暗圈）
        // Phase 2 → 高亮 Input  槽（淡蓝色暗圈），同时保留起点附近的 Output 高亮
        if (!bHasStart)
        {
            for (const FVector& Loc : NearOutputLocs)
                DrawWorldRing(Loc, FLinearColor(0.5f, 0.9f, 0.3f, 0.5f), 10.f, 1.5f);
        }
        else
        {
            for (const FVector& Loc : NearInputLocs)
                DrawWorldRing(Loc, FLinearColor(0.3f, 0.6f, 1.0f, 0.5f), 10.f, 1.5f);
        }
    }

    // ── 精确吸附指示圈（覆盖在范围高亮之上）─────────────────────────────
    if (!bHasStart)
    {
        // Phase 1：最近 Output 槽 → 绿色大圈（或灰色表示附近无槽）
        const FLinearColor RingColor = bBeltHoverSnapped
                                           ? FLinearColor(0.1f, 1.0f, 0.3f)
                                           : FLinearColor(0.5f, 0.5f, 0.5f);
        DrawWorldRing(BeltHoverSnapLocation, RingColor, 14.f, 2.5f);
    }
    else
    {
        // 起点：金色大圈（已锁定）
        DrawWorldRing(Manager->GetBeltStartSlotLocation(), FLinearColor(1.f, 0.8f, 0.1f), 18.f, 3.0f);

        // 终点：蓝色（有效吸附）或灰色（无槽/超距）
        const bool bValid = Manager->IsPreviewBeltValid();
        FLinearColor EndColor;
        if (!bValid)
            EndColor = FLinearColor(1.f, 0.15f, 0.15f); // 超出最大距离 → 红
        else if (bBeltHoverSnapped)
            EndColor = FLinearColor(0.2f, 0.6f, 1.0f); // 有效 Input 槽 → 蓝
        else
            EndColor = FLinearColor(0.5f, 0.5f, 0.5f); // 无吸附 → 灰
        DrawWorldRing(BeltHoverSnapLocation, EndColor, 14.f, 2.5f);

        // 距离超限时在屏幕中间偏上显示红字警告
        if (!bValid && Canvas)
        {
            const float Dist = FVector::Dist(Manager->GetBeltStartSlotLocation(), BeltHoverSnapLocation);
            const FString WarnText =
                Dist > UMassDspManager::MaxBeltLength
                    ? FString::Printf(TEXT("距离过远！%.0f m / 最大 %.0f m"), Dist / 100.f, UMassDspManager::MaxBeltLength / 100.f)
                    : FString::Printf(TEXT("距离过近！%.0f m / 最小 %.0f m"), Dist / 100.f, UMassDspManager::MinBeltLength / 100.f);
            float TW = 0.f, TH = 0.f;
            GetTextSize(WarnText, TW, TH, GEngine->GetSmallFont(), 1.5f);
            const float TX = (Canvas->SizeX - TW) * 0.5f;
            const float TY = Canvas->SizeY * 0.35f;
            DrawText(WarnText, FLinearColor::Black, TX + 1.f, TY + 1.f, GEngine->GetSmallFont(), 1.5f);
            DrawText(WarnText, FLinearColor(1.f, 0.2f, 0.2f), TX, TY, GEngine->GetSmallFont(), 1.5f);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  统计数据更新
// ─────────────────────────────────────────────────────────────────────────────

void AMassDspHUD::UpdateFrameStats()
{
    if (FrameTimeHistory.Num() < 10) return;

    float TotalFrameTime = 0.0f;
    for (const float FrameTime : FrameTimeHistory)
        TotalFrameTime += FrameTime;
    AverageFPS = static_cast<float>(FrameTimeHistory.Num()) / TotalFrameTime;

    TArray<float> SortedFrameTimes = FrameTimeHistory;
    SortedFrameTimes.Sort([](float A, float B) { return A > B; });

    const int32 OnePercentCount = FMath::Max(1, FMath::CeilToInt(SortedFrameTimes.Num() * 0.01f));
    float OnePercentSum = 0.0f;
    for (int32 i = 0; i < OnePercentCount; ++i)
        OnePercentSum += SortedFrameTimes[i];
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
        TotalItems += Pair.Value.ItemCache.Num();
    CachedBeltItemCount = TotalItems;
}
