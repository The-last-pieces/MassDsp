#include "MassDspHUD.h"

#include "Engine/Engine.h"
#include "MassDspGameMode.h"
#include "Subsystems/MassDspManager.h"
#include "Subsystems/MassDspDebugStatsSubsystem.h"
#include "Actors/MassDspBuilding.h"
#include "UI/MassDspInventoryWidget.h"
#include "UI/MassDspSystemStatsWidget.h"
#include "UI/MassDspTechTreeWidget.h"

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

    EnableInput(PC);
    if (!InputComponent) return;

    // 创建热键栏 Widget：从 GameConfig.HotbarWidgetClass 读取蓝图类（需在 GameMode 中手动指定 BP_Hotbar）
    AMassDspGameMode* Gm = GetWorld() ? Cast<AMassDspGameMode>(GetWorld()->GetAuthGameMode()) : nullptr;
    const TSubclassOf<UMassDspHotbarWidget> HotbarClass =
        (Gm && Gm->GameConfig && Gm->GameConfig->HotbarWidgetClass)
            ? Gm->GameConfig->HotbarWidgetClass
            : nullptr; // fallback: C++ 基类
    if (!HotbarClass) return;
    HotbarWidget = CreateWidget<UMassDspHotbarWidget>(PC, HotbarClass);
    if (HotbarWidget)
        HotbarWidget->AddToViewport(0);
    if (!HotbarWidget) return;

    // 数字键 1-9：直接绑定到 HotbarWidget 各槽位（无中间层）
    InputComponent->BindKey(EKeys::One, IE_Pressed, HotbarWidget.Get(), &UMassDspHotbarWidget::OnSlot0Clicked);
    InputComponent->BindKey(EKeys::Two, IE_Pressed, HotbarWidget.Get(), &UMassDspHotbarWidget::OnSlot1Clicked);
    InputComponent->BindKey(EKeys::Three, IE_Pressed, HotbarWidget.Get(), &UMassDspHotbarWidget::OnSlot2Clicked);
    InputComponent->BindKey(EKeys::Four, IE_Pressed, HotbarWidget.Get(), &UMassDspHotbarWidget::OnSlot3Clicked);
    InputComponent->BindKey(EKeys::Five, IE_Pressed, HotbarWidget.Get(), &UMassDspHotbarWidget::OnSlot4Clicked);
    InputComponent->BindKey(EKeys::Six, IE_Pressed, HotbarWidget.Get(), &UMassDspHotbarWidget::OnSlot5Clicked);
    InputComponent->BindKey(EKeys::Seven, IE_Pressed, HotbarWidget.Get(), &UMassDspHotbarWidget::OnSlot6Clicked);
    InputComponent->BindKey(EKeys::Eight, IE_Pressed, HotbarWidget.Get(), &UMassDspHotbarWidget::OnSlot7Clicked);
    InputComponent->BindKey(EKeys::Nine, IE_Pressed, HotbarWidget.Get(), &UMassDspHotbarWidget::OnSlot8Clicked);

    // 鼠标、滚轮、F 键：交互逻辑全部在 HotbarWidget 中
    InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, HotbarWidget.Get(), &UMassDspHotbarWidget::OnMouseLeftClick);
    InputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, HotbarWidget.Get(), &UMassDspHotbarWidget::OnMouseRightClick);
    InputComponent->BindKey(EKeys::MouseScrollUp, IE_Pressed, HotbarWidget.Get(), &UMassDspHotbarWidget::OnScrollUp);
    InputComponent->BindKey(EKeys::MouseScrollDown, IE_Pressed, HotbarWidget.Get(), &UMassDspHotbarWidget::OnScrollDown);
    InputComponent->BindKey(EKeys::F, IE_Pressed, this, &AMassDspHUD::HandleInteractKey);
    InputComponent->BindKey(EKeys::I, IE_Pressed, this, &AMassDspHUD::ToggleInventoryWidget);
    InputComponent->BindKey(EKeys::F3, IE_Pressed, this, &AMassDspHUD::ToggleSystemStatsWidget);
    InputComponent->BindKey(EKeys::T, IE_Pressed, this, &AMassDspHUD::ToggleTechTreeWidget);
}

void AMassDspHUD::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    if (InventoryWidget && !InventoryWidget->IsInViewport())
    {
        InventoryWidget = nullptr;
    }

    if (SystemStatsWidget && !SystemStatsWidget->IsInViewport())
    {
        SystemStatsWidget = nullptr;
    }

    if (TechTreeWidget && !TechTreeWidget->IsInViewport())
    {
        TechTreeWidget = nullptr;
    }

    UpdateCameraMovement(DeltaSeconds);
}

void AMassDspHUD::HandleInteractKey()
{
    if (InventoryWidget)
    {
        InventoryWidget->CloseWidget();
        InventoryWidget = nullptr;
    }

    if (SystemStatsWidget)
    {
        SystemStatsWidget->CloseWidget();
        SystemStatsWidget = nullptr;
    }

    if (TechTreeWidget)
    {
        TechTreeWidget->CloseWidget();
        TechTreeWidget = nullptr;
    }

    if (HotbarWidget)
    {
        HotbarWidget->OnInteractKey();
    }
}

void AMassDspHUD::ToggleInventoryWidget()
{
    APlayerController* PC = GetOwningPlayerController();
    if (!PC) return;

    if (InventoryWidget)
    {
        InventoryWidget->CloseWidget();
        InventoryWidget = nullptr;
        return;
    }

    if (SystemStatsWidget)
    {
        SystemStatsWidget->CloseWidget();
        SystemStatsWidget = nullptr;
    }

    if (TechTreeWidget)
    {
        TechTreeWidget->CloseWidget();
        TechTreeWidget = nullptr;
    }

    if (HotbarWidget && HotbarWidget->CurrentBuildingWidget)
    {
        HotbarWidget->CurrentBuildingWidget->CloseWidget();
        HotbarWidget->CurrentBuildingWidget = nullptr;
    }

    AMassDspGameMode* GM = GetWorld() ? Cast<AMassDspGameMode>(GetWorld()->GetAuthGameMode()) : nullptr;
    TSubclassOf<UMassDspInventoryWidget> InventoryClass = GM && GM->GameConfig ? GM->GameConfig->InventoryWidgetClass : nullptr;
    if (!InventoryClass) return;

    InventoryWidget = CreateWidget<UMassDspInventoryWidget>(PC, InventoryClass);
    if (!InventoryWidget) return;

    InventoryWidget->AddToViewport(10);

    FInputModeGameAndUI UIMode;
    UIMode.SetWidgetToFocus(InventoryWidget->TakeWidget());
    UIMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    PC->SetInputMode(UIMode);
    PC->bShowMouseCursor = true;
}

void AMassDspHUD::ToggleSystemStatsWidget()
{
    APlayerController* PC = GetOwningPlayerController();
    if (!PC) return;

    if (SystemStatsWidget)
    {
        SystemStatsWidget->CloseWidget();
        SystemStatsWidget = nullptr;
        return;
    }

    if (InventoryWidget)
    {
        InventoryWidget->CloseWidget();
        InventoryWidget = nullptr;
    }

    if (TechTreeWidget)
    {
        TechTreeWidget->CloseWidget();
        TechTreeWidget = nullptr;
    }

    if (HotbarWidget && HotbarWidget->CurrentBuildingWidget)
    {
        HotbarWidget->CurrentBuildingWidget->CloseWidget();
        HotbarWidget->CurrentBuildingWidget = nullptr;
    }

    AMassDspGameMode* GM = GetWorld() ? Cast<AMassDspGameMode>(GetWorld()->GetAuthGameMode()) : nullptr;
    TSubclassOf<UMassDspSystemStatsWidget> StatsClass = GM && GM->GameConfig ? GM->GameConfig->SystemStatsWidgetClass : nullptr;
    if (!StatsClass) return;

    if (UMassDspDebugStatsSubsystem* StatsSubsystem = GetWorld()->GetSubsystem<UMassDspDebugStatsSubsystem>())
    {
        StatsSubsystem->ForceRefresh();
    }

    SystemStatsWidget = CreateWidget<UMassDspSystemStatsWidget>(PC, StatsClass);
    if (!SystemStatsWidget) return;

    SystemStatsWidget->AddToViewport(15);

    FInputModeGameAndUI UIMode;
    UIMode.SetWidgetToFocus(SystemStatsWidget->TakeWidget());
    UIMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    PC->SetInputMode(UIMode);
    PC->bShowMouseCursor = true;
}

void AMassDspHUD::ToggleTechTreeWidget()
{
    APlayerController* PC = GetOwningPlayerController();
    if (!PC) return;

    if (TechTreeWidget)
    {
        TechTreeWidget->CloseWidget();
        TechTreeWidget = nullptr;
        return;
    }

    if (InventoryWidget)
    {
        InventoryWidget->CloseWidget();
        InventoryWidget = nullptr;
    }

    if (SystemStatsWidget)
    {
        SystemStatsWidget->CloseWidget();
        SystemStatsWidget = nullptr;
    }

    if (HotbarWidget && HotbarWidget->CurrentBuildingWidget)
    {
        HotbarWidget->CurrentBuildingWidget->CloseWidget();
        HotbarWidget->CurrentBuildingWidget = nullptr;
    }

    AMassDspGameMode* GM = GetWorld() ? Cast<AMassDspGameMode>(GetWorld()->GetAuthGameMode()) : nullptr;
    TSubclassOf<UMassDspTechTreeWidget> TechTreeClass = GM && GM->GameConfig ? GM->GameConfig->TechTreeWidgetClass : nullptr;
    if (!TechTreeClass) return;

    TechTreeWidget = CreateWidget<UMassDspTechTreeWidget>(PC, TechTreeClass);
    if (!TechTreeWidget) return;

    TechTreeWidget->AddToViewport(12);

    FInputModeGameAndUI UIMode;
    UIMode.SetWidgetToFocus(TechTreeWidget->TakeWidget());
    UIMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    PC->SetInputMode(UIMode);
    PC->bShowMouseCursor = true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  建造预览更新（每帧）
// ─────────────────────────────────────────────────────────────────────────────

// ─────────────────────────────────────────────────────────────────────────────
//  高度自适应镜头移动
// ─────────────────────────────────────────────────────────────────────────────

void AMassDspHUD::UpdateCameraMovement(float DeltaSeconds)
{
    APlayerController* PC = GetOwningPlayerController();
    if (!PC) return;
    APawn* Pawn = PC->GetPawn();
    if (!Pawn) return;

    // 目标速度（按高度比例），平滑插值避免缩放时抽搐
    const float Height = GetCameraHeight();
    const float TargetSpeed = GetAdaptiveCameraSpeed(Height);
    CurrentCameraSpeed = FMath::FInterpTo(CurrentCameraSpeed, TargetSpeed, DeltaSeconds, CameraSpeedSmoothRate);

    // 轮询 WASD 键状态（不消耗输入事件，与其他绑定共存）
    const bool bW = PC->IsInputKeyDown(EKeys::W);
    const bool bS = PC->IsInputKeyDown(EKeys::S);
    const bool bA = PC->IsInputKeyDown(EKeys::A);
    const bool bD = PC->IsInputKeyDown(EKeys::D);

    if (!bW && !bS && !bA && !bD) return;

    // 取摄像机水平朝向（忽略 Pitch/Roll，保证在地面平面移动）
    FRotator CamRot = PC->GetControlRotation();
    CamRot.Pitch = 0.f;
    CamRot.Roll = 0.f;
    const FVector FwdDir = FRotationMatrix(CamRot).GetUnitAxis(EAxis::X);
    const FVector RightDir = FRotationMatrix(CamRot).GetUnitAxis(EAxis::Y);

    FVector MoveDir = FVector::ZeroVector;
    if (bW) MoveDir += FwdDir;
    if (bS) MoveDir -= FwdDir;
    if (bD) MoveDir += RightDir;
    if (bA) MoveDir -= RightDir;

    if (MoveDir.IsNearlyZero()) return;

    MoveDir.Z = 0.f; // 确保只在水平面移动
    MoveDir.Normalize();

    // 直接偏移 Pawn（绕过 MovementComponent，不与蓝图默认移动绑定叠加）
    Pawn->AddActorWorldOffset(MoveDir * CurrentCameraSpeed * DeltaSeconds, false);
}

float AMassDspHUD::GetCameraHeight() const
{
    APlayerController* PC = GetOwningPlayerController();
    if (!PC || !PC->GetPawn()) return 1000.f;

    const FVector PawnLoc = PC->GetPawn()->GetActorLocation();

    if (bUseSurfaceTraceForHeight)
    {
        FHitResult Hit;
        const FVector TraceEnd = PawnLoc - FVector(0.f, 0.f, 100000.f);
        if (GetWorld()->LineTraceSingleByChannel(Hit, PawnLoc, TraceEnd, ECC_WorldStatic))
            return FMath::Max(PawnLoc.Z - Hit.Location.Z, 1.f);
    }

    return FMath::Max(PawnLoc.Z, 1.f);
}

float AMassDspHUD::GetAdaptiveCameraSpeed(float Height) const
{
    return FMath::Clamp(Height * CameraSpeedFactor, CameraMinSpeed, CameraMaxSpeed);
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

    // ── 常驻 FPS（左上角） ──
    DrawPersistentFps();

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

void AMassDspHUD::DrawPersistentFps()
{
    if (!Canvas || !GEngine || !GEngine->GetSmallFont()) return;

    const UWorld* World = GetWorld();
    const float CurrentFps = World && World->GetDeltaSeconds() > 0.f ? 1.f / World->GetDeltaSeconds() : 0.f;

    const FString FpsText = FString::Printf(
        TEXT("FPS %.1f  |  Avg %.1f  |  1%% Low %.1f"),
        CurrentFps,
        AverageFPS,
        OnePercentLowFPS);

    constexpr float StartX = 18.f;
    constexpr float StartY = 16.f;
    constexpr float PaddingX = 12.f;
    constexpr float PaddingY = 8.f;
    constexpr float Scale = 1.15f;

    float TextW = 0.f;
    float TextH = 0.f;
    GetTextSize(FpsText, TextW, TextH, GEngine->GetSmallFont(), Scale);

    FCanvasTileItem Background(
        FVector2D(StartX - PaddingX, StartY - PaddingY),
        FVector2D(TextW + PaddingX * 2.f, TextH + PaddingY * 2.f),
        FLinearColor(0.03f, 0.04f, 0.06f, 0.72f));
    Background.BlendMode = SE_BLEND_Translucent;
    Canvas->DrawItem(Background);

    DrawText(FpsText, FLinearColor::Black, StartX + 1.f, StartY + 1.f, GEngine->GetSmallFont(), Scale);
    DrawText(FpsText, FLinearColor(0.88f, 0.96f, 1.0f), StartX, StartY, GEngine->GetSmallFont(), Scale);
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

    const float TextScale = 1.6f;
    const float TextY = CanvasH - 130.f;
    float TextW = 0.f, TextH = 0.f;
    GetTextSize(ModeText, TextW, TextH, GEngine->GetSmallFont(), TextScale);
    const float TextX = (CanvasW - TextW) * 0.5f;

    DrawText(ModeText, FLinearColor::Black, TextX + 1.f, TextY + 1.f, GEngine->GetSmallFont(), TextScale);
    DrawText(ModeText, HintColor, TextX, TextY, GEngine->GetSmallFont(), TextScale);
}

// ─────────────────────────────────────────────────────────────────────────────
//  可交互建筑 HUD 提示
// ─────────────────────────────────────────────────────────────────────────────

void AMassDspHUD::DrawInteractionHint()
{
    if (!HotbarWidget || HotbarWidget->CurrentBuildingWidget) return;
    if (!Canvas) return;

    UMassDspManager* Manager = GetWorld() ? GetWorld()->GetSubsystem<UMassDspManager>() : nullptr;
    if (!Manager) return;

    APlayerController* PC = GetOwningPlayerController();
    if (!PC || !PC->GetPawn()) return;

    const FVector PlayerLoc = PC->GetPawn()->GetActorLocation();
    FMassEntityHandle NearestEntity;
    EBuildingType NearestType = EBuildingType::None;
    FVector NearestLoc;

    if (!Manager->FindNearestBuilding(PlayerLoc, UMassDspHotbarWidget::BuildingInteractRadius,
                                      NearestEntity, NearestType, NearestLoc,
                                      [this](const FVector& Loc) { return HotbarWidget->IsBuildingInViewCone(Loc); }))
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

    FVector2D BuildingScreenPos;
    if (!PC->ProjectWorldLocationToScreen(NearestLoc, BuildingScreenPos, true)) return;

    constexpr float OffsetY = 48.f;
    const float TX = BuildingScreenPos.X - TW * 0.5f;
    const float TY = BuildingScreenPos.Y - TH - PadY * 2.f - OffsetY;

    DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.50f), TX - PadX, TY - PadY, TW + PadX * 2.f, TH + PadY * 2.f);
    DrawText(HintText, FLinearColor::Black, TX + 1.f, TY + 1.f, GEngine->GetSmallFont(), Scale);
    DrawText(HintText, FLinearColor(1.f, 0.95f, 0.3f), TX, TY, GEngine->GetSmallFont(), Scale);
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

    // 当前预览 Transform（状态均来自 HotbarWidget）
    if (!HotbarWidget) return;
    const FTransform PreviewTransform(HotbarWidget->BuildingRotation, HotbarWidget->CachedHitLocation);

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
    if (!HotbarWidget) return;

    // ── 范围高亮：收集附近所有槽口并绘制半透明小圆 ────────────────────────
    {
        TArray<FVector> NearOutputLocs, NearInputLocs;
        const FVector SearchCenter = bHasStart ? Manager->GetBeltStartSlotLocation() : HotbarWidget->CachedHitLocation;
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
        const FLinearColor RingColor = HotbarWidget->bBeltHoverSnapped
                                           ? FLinearColor(0.1f, 1.0f, 0.3f)
                                           : FLinearColor(0.5f, 0.5f, 0.5f);
        DrawWorldRing(HotbarWidget->BeltHoverSnapLocation, RingColor, 14.f, 2.5f);
    }
    else
    {
        // 起点：金色大圈（已锁定）
        DrawWorldRing(Manager->GetBeltStartSlotLocation(), FLinearColor(1.f, 0.8f, 0.1f), 18.f, 3.0f);

        // 终点：蓝色（有效吸附）或灰色（无槽/超距）
        const bool bValid = Manager->IsPreviewBeltValid();
        FLinearColor EndColor;
        if (!bValid)
            EndColor = FLinearColor(1.f, 0.15f, 0.15f);
        else if (HotbarWidget->bBeltHoverSnapped)
            EndColor = FLinearColor(0.2f, 0.6f, 1.0f);
        else
            EndColor = FLinearColor(0.5f, 0.5f, 0.5f);
        DrawWorldRing(HotbarWidget->BeltHoverSnapLocation, EndColor, 14.f, 2.5f);

        if (!bValid && Canvas)
        {
            const float Dist = FVector::Dist(Manager->GetBeltStartSlotLocation(), HotbarWidget->BeltHoverSnapLocation);
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
