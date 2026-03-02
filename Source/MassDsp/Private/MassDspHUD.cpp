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
    }
    else if (Manager->IsPreviewingBelt() && Manager->BeltHasStartSlot())
    {
        Manager->UpdateBeltPreviewEndPoint(CachedHitLocation);
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
            // 左键尝试吸附槽口
            // - 首次：锁定起始 Output 槽
            // - 次次：锁定终止 Input 槽并立即确认
            const bool bBothSelected = Manager->SelectBeltSlot(CachedHitLocation);
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
