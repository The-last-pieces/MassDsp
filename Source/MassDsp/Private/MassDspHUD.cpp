#include "MassDspHUD.h"

#include "Engine/Engine.h"
#include "Subsystems/MassDspManager.h"

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
}

void AMassDspHUD::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    UpdateBuildPreview(DeltaSeconds);
}

// ─────────────────────────────────────────────────────────────────────────────
//  建造预览更新（每帧）
// ─────────────────────────────────────────────────────────────────────────────

bool AMassDspHUD::GetMouseWorldHitLocation(FVector& OutHitLocation) const
{
    APlayerController* PC = GetOwningPlayerController();
    if (!PC) return false;

    FVector WorldLoc, WorldDir;
    if (!PC->DeprojectMousePositionToWorld(WorldLoc, WorldDir)) return false;

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

    bBeltHoverSnapped = Manager->FindNearestBuildingSlot(
        CachedHitLocation, TargetSlotType, SnapRadius,
        DummyEntity, DummySlotIndex, SnappedPos);
    BeltHoverSnapLocation = bBeltHoverSnapped ? SnappedPos : CachedHitLocation;

    // Phase 2：每帧用（已吸附的）终点坐标重建预览网格
    if (Manager->BeltHasStartSlot())
    {
        Manager->UpdateBeltPreviewEndPoint(BeltHoverSnapLocation);
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

    auto DrawLine = [&](const FString& Text, float Y)
    {
        DrawText(Text, FLinearColor::Black, PosX + 1.0f, Y + 1.0f, GEngine->GetSmallFont(), 1.5f);
        DrawText(Text, FLinearColor::Yellow, PosX, Y, GEngine->GetSmallFont(), 1.5f);
    };

    DrawLine(FpsText, PosY);
    DrawLine(GameText, PosY + LineStep);

    // ── 建造模式提示 ──
    DrawBuildSystemHint();

    // ── 传送带吸附指示圈 ──
    DrawBeltSnapIndicator();
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

void AMassDspHUD::DrawBeltSnapIndicator()
{
    UMassDspManager* Manager = GetWorld() ? GetWorld()->GetSubsystem<UMassDspManager>() : nullptr;
    if (!Manager || !Manager->IsPreviewingBelt()) return;
    if (!Canvas) return;

    APlayerController* PC = GetOwningPlayerController();
    if (!PC) return;

    // ── 辅助：将世界坐标投影到屏幕并画实心圆点 ──
    auto DrawWorldDot = [&](FVector WorldPos, FLinearColor Color, float Radius)
    {
        FVector2D ScreenPos;
        if (!PC->ProjectWorldLocationToScreen(WorldPos, ScreenPos, true)) return;

        constexpr int32 Segs = 16;
        for (int32 i = 0; i < Segs; ++i)
        {
            const float A0 = (i      / (float)Segs) * 2.f * UE_PI;
            const float A1 = ((i + 1) / (float)Segs) * 2.f * UE_PI;
            DrawLine(
                ScreenPos.X + FMath::Cos(A0) * Radius,
                ScreenPos.Y + FMath::Sin(A0) * Radius,
                ScreenPos.X + FMath::Cos(A1) * Radius,
                ScreenPos.Y + FMath::Sin(A1) * Radius,
                Color, 2.5f);
        }
    };

    const bool bHasStart = Manager->BeltHasStartSlot();

    // Phase 1：在鼠标附近最近的 Output 槽口处画绿圈（或灰圈表示无槽口）
    // Phase 2：在鼠标附近最近的 Input 槽口处画蓝圈
    if (!bHasStart)
    {
        const FLinearColor RingColor = bBeltHoverSnapped
            ? FLinearColor(0.1f, 1.0f, 0.3f)    // 有效 Output 槽 → 绿
            : FLinearColor(0.5f, 0.5f, 0.5f);   // 无槽口 → 灰
        DrawWorldDot(BeltHoverSnapLocation, RingColor, 14.f);
    }
    else
    {
        // 起点：固定在已锁定的 Output 槽位置，画金色大圈表示「已选」
        DrawWorldDot(Manager->GetBeltStartSlotLocation(), FLinearColor(1.f, 0.8f, 0.1f), 18.f);

        // 终点跟随鼠标，就近吸附到 Input 槽 → 蓝圈
        const FLinearColor EndColor = bBeltHoverSnapped
            ? FLinearColor(0.2f, 0.6f, 1.0f)    // 有效 Input 槽 → 蓝
            : FLinearColor(0.5f, 0.5f, 0.5f);   // 无槽口 → 灰
        DrawWorldDot(BeltHoverSnapLocation, EndColor, 14.f);
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
