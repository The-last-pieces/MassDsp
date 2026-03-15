#include "UI/MassDspHotbarWidget.h"

#include "GameConst.h"
#include "MassDspGameMode.h"
#include "Subsystems/MassDspManager.h"
#include "Actors/MassDspBuilding.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/Border.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"

// ─────────────────────────────────────────────────────────────────────────────
//  槽位定义
// ─────────────────────────────────────────────────────────────────────────────

void UMassDspHotbarWidget::InitSlotDefs()
{
    SlotDefs.SetNum(TotalSlots);
    SlotDefs[0] = {TEXT("矿机"), TEXT("矿机"), EHotbarAction::PlaceBuilding, EBuildingType::Miner, EBeltType::None, 0};
    SlotDefs[1] = {TEXT("合成"), TEXT("合成台"), EHotbarAction::PlaceBuilding, EBuildingType::Assembler, EBeltType::None, 0};
    SlotDefs[2] = {TEXT("仓库"), TEXT("仓库"), EHotbarAction::PlaceBuilding, EBuildingType::Storage, EBeltType::None, 0};
    SlotDefs[3] = {TEXT("物流"), TEXT("物流塔"), EHotbarAction::PlaceBuilding, EBuildingType::LogisticsTower, EBeltType::None, 0};
    SlotDefs[4] = {TEXT("低速"), TEXT("低级传送带"), EHotbarAction::PlaceBelt, EBuildingType::None, EBeltType::Normal, 0};
    SlotDefs[5] = {TEXT("高速"), TEXT("高级传送带"), EHotbarAction::PlaceBelt, EBuildingType::None, EBeltType::Fast, 0};
    SlotDefs[6] = {TEXT("极速"), TEXT("极速传送带"), EHotbarAction::PlaceBelt, EBuildingType::None, EBeltType::Express, 0};
    SlotDefs[7] = {TEXT("测1"), TEXT("测试场景1"), EHotbarAction::TestScene, EBuildingType::None, EBeltType::None, 1};
    SlotDefs[8] = {TEXT("测2"), TEXT("测试场景2"), EHotbarAction::TestScene, EBuildingType::None, EBeltType::None, 2};
}

// ─────────────────────────────────────────────────────────────────────────────
//  生命周期
// ─────────────────────────────────────────────────────────────────────────────

void UMassDspHotbarWidget::NativeConstruct()
{
    Super::NativeConstruct();
    InitSlotDefs();
    SlotUnlockedStates.Init(true, TotalSlots);
    BindSlotWidgets();
    RefreshSlotAvailability();
}

void UMassDspHotbarWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
    Super::NativeTick(MyGeometry, DeltaTime);
    UpdateBuildPreview();
    RefreshSlotAvailability();
    SyncHighlight();

    // 建筑交互 Widget 被内部关闭后清零指针
    if (CurrentBuildingWidget && !CurrentBuildingWidget->IsInViewport())
        CurrentBuildingWidget = nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Widget 绑定（BP_Hotbar 由 FUMaterialGeneratorUtils::CreateBuildingWidgets 生成）
// ─────────────────────────────────────────────────────────────────────────────

void UMassDspHotbarWidget::BindSlotWidgets()
{
    SlotButtons.SetNum(TotalSlots);
    SlotBorders.SetNum(TotalSlots);

    for (int32 i = 0; i < TotalSlots; ++i)
    {
        SlotButtons[i] = Cast<UButton>(GetWidgetFromName(*FString::Printf(TEXT("Button_Slot%d"), i)));
        SlotBorders[i] = Cast<UBorder>(GetWidgetFromName(*FString::Printf(TEXT("Border_Slot%d"), i)));
    }

    // AddDynamic 是宏，必须用字面量成员函数指针
    if (SlotButtons[0]) SlotButtons[0]->OnClicked.AddDynamic(this, &UMassDspHotbarWidget::OnSlot0Clicked);
    if (SlotButtons[1]) SlotButtons[1]->OnClicked.AddDynamic(this, &UMassDspHotbarWidget::OnSlot1Clicked);
    if (SlotButtons[2]) SlotButtons[2]->OnClicked.AddDynamic(this, &UMassDspHotbarWidget::OnSlot2Clicked);
    if (SlotButtons[3]) SlotButtons[3]->OnClicked.AddDynamic(this, &UMassDspHotbarWidget::OnSlot3Clicked);
    if (SlotButtons[4]) SlotButtons[4]->OnClicked.AddDynamic(this, &UMassDspHotbarWidget::OnSlot4Clicked);
    if (SlotButtons[5]) SlotButtons[5]->OnClicked.AddDynamic(this, &UMassDspHotbarWidget::OnSlot5Clicked);
    if (SlotButtons[6]) SlotButtons[6]->OnClicked.AddDynamic(this, &UMassDspHotbarWidget::OnSlot6Clicked);
    if (SlotButtons[7]) SlotButtons[7]->OnClicked.AddDynamic(this, &UMassDspHotbarWidget::OnSlot7Clicked);
    if (SlotButtons[8]) SlotButtons[8]->OnClicked.AddDynamic(this, &UMassDspHotbarWidget::OnSlot8Clicked);

    for (int32 i = 0; i < TotalSlots; ++i)
    {
        SetSlotHighlight(i, false);
    }
}

// ─ 9个无参回调，统一转发到 ExecuteSlot ──────────────────────────────────────
void UMassDspHotbarWidget::OnSlot0Clicked() { ExecuteSlot(0); }
void UMassDspHotbarWidget::OnSlot1Clicked() { ExecuteSlot(1); }
void UMassDspHotbarWidget::OnSlot2Clicked() { ExecuteSlot(2); }
void UMassDspHotbarWidget::OnSlot3Clicked() { ExecuteSlot(3); }
void UMassDspHotbarWidget::OnSlot4Clicked() { ExecuteSlot(4); }
void UMassDspHotbarWidget::OnSlot5Clicked() { ExecuteSlot(5); }
void UMassDspHotbarWidget::OnSlot6Clicked() { ExecuteSlot(6); }
void UMassDspHotbarWidget::OnSlot7Clicked() { ExecuteSlot(7); }
void UMassDspHotbarWidget::OnSlot8Clicked() { ExecuteSlot(8); }

// ─────────────────────────────────────────────────────────────────────────────
//  动作执行
// ─────────────────────────────────────────────────────────────────────────────

void UMassDspHotbarWidget::OnHotbarKeyPressed(int32 SlotIndex)
{
    ExecuteSlot(SlotIndex);
}

void UMassDspHotbarWidget::ExecuteSlot(int32 SlotIndex)
{
    if (!SlotDefs.IsValidIndex(SlotIndex)) return;
    UWorld* World = GetWorld();
    if (!World) return;

    UMassDspManager* Manager = World->GetSubsystem<UMassDspManager>();
    if (!Manager) return;

    const FHotbarSlotDef& Def = SlotDefs[SlotIndex];
    if (!IsSlotUnlocked(Def))
    {
        ShowLockedMessage(GetSlotLockedReason(Def));
        return;
    }

    switch (Def.Action)
    {
    case EHotbarAction::PlaceBuilding:
        {
            FVector PlaceLoc = FVector::ZeroVector;
            GetWorldHitLocation(PlaceLoc);
            Manager->BeginPreviewBuilding(Def.BuildingType, FTransform(BuildingRotation, PlaceLoc));
            break;
        }
    case EHotbarAction::PlaceBelt:
        Manager->BeginPreviewBelt(Def.BeltType);
        break;
    case EHotbarAction::TestScene:
        if (AMassDspGameMode* GM = Cast<AMassDspGameMode>(World->GetAuthGameMode()))
        {
            if (Def.TestCaseIdx == 1) GM->TestCase1();
            else if (Def.TestCaseIdx == 2) GM->TestCase2();
        }
        break;
    default: break;
    }
}

void UMassDspHotbarWidget::RefreshSlotAvailability()
{
    UMassDspManager* Manager = GetWorld() ? GetWorld()->GetSubsystem<UMassDspManager>() : nullptr;
    if (!Manager) return;

    if (SlotUnlockedStates.Num() != TotalSlots)
    {
        SlotUnlockedStates.Init(true, TotalSlots);
    }

    for (int32 i = 0; i < SlotDefs.Num(); ++i)
    {
        const bool bUnlocked = IsSlotUnlocked(SlotDefs[i]);
        SlotUnlockedStates[i] = bUnlocked;

        if (SlotButtons.IsValidIndex(i) && SlotButtons[i])
        {
            SlotButtons[i]->SetIsEnabled(bUnlocked || SlotDefs[i].Action == EHotbarAction::TestScene);
        }

        if (LastHighlightedSlot != i)
        {
            SetSlotHighlight(i, false);
        }
    }
}

bool UMassDspHotbarWidget::IsSlotUnlocked(const FHotbarSlotDef& Def) const
{
    UMassDspManager* Manager = GetWorld() ? GetWorld()->GetSubsystem<UMassDspManager>() : nullptr;
    if (!Manager) return true;

    switch (Def.Action)
    {
    case EHotbarAction::PlaceBuilding:
        return Manager->IsBuildingUnlocked(Def.BuildingType);
    case EHotbarAction::PlaceBelt:
        return Manager->IsBeltUnlocked(Def.BeltType);
    default:
        return true;
    }
}

FText UMassDspHotbarWidget::GetSlotLockedReason(const FHotbarSlotDef& Def) const
{
    UMassDspManager* Manager = GetWorld() ? GetWorld()->GetSubsystem<UMassDspManager>() : nullptr;
    if (!Manager) return FText::FromString(TEXT("功能暂不可用"));

    switch (Def.Action)
    {
    case EHotbarAction::PlaceBuilding:
        {
            const FText Reason = Manager->GetBuildingUnlockRequirementText(Def.BuildingType);
            return Reason.IsEmpty() ? FText::FromString(TEXT("建筑尚未解锁")) : Reason;
        }
    case EHotbarAction::PlaceBelt:
        {
            const FText Reason = Manager->GetBeltUnlockRequirementText(Def.BeltType);
            return Reason.IsEmpty() ? FText::FromString(TEXT("传送带尚未解锁")) : Reason;
        }
    default:
        return FText::FromString(TEXT("功能暂不可用"));
    }
}

void UMassDspHotbarWidget::ShowLockedMessage(const FText& Message) const
{
    if (Message.IsEmpty() || !GEngine) return;
    GEngine->AddOnScreenDebugMessage(INDEX_NONE, 2.0f, FColor::Yellow, Message.ToString());
}

bool UMassDspHotbarWidget::GetWorldHitLocation(FVector& OutLoc) const
{
    APlayerController* PC = GetOwningPlayer();
    if (!PC) return false;

    int32 ViewW = 0, ViewH = 0;
    PC->GetViewportSize(ViewW, ViewH);
    FVector WorldLoc, WorldDir;
    if (!PC->DeprojectScreenPositionToWorld(ViewW * 0.5f, ViewH * 0.5f, WorldLoc, WorldDir))
        return false;

    FHitResult Hit;
    if (GetWorld()->LineTraceSingleByChannel(Hit, WorldLoc, WorldLoc + WorldDir * 100000.f, ECC_Visibility))
    {
        OutLoc = Hit.Location;
        return true;
    }
    if (FMath::Abs(WorldDir.Z) > SMALL_NUMBER)
    {
        const float T = -WorldLoc.Z / WorldDir.Z;
        OutLoc = T > 0.f ? WorldLoc + WorldDir * T : WorldLoc;
    }
    else OutLoc = WorldLoc;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  高亮同步
// ─────────────────────────────────────────────────────────────────────────────

void UMassDspHotbarWidget::SyncHighlight()
{
    const int32 Active = GetCurrentActiveSlot();
    if (Active == LastHighlightedSlot) return;

    if (LastHighlightedSlot >= 0)
        SetSlotHighlight(LastHighlightedSlot, false);
    if (Active >= 0)
        SetSlotHighlight(Active, true);

    LastHighlightedSlot = Active;
}

int32 UMassDspHotbarWidget::GetCurrentActiveSlot() const
{
    UMassDspManager* Manager = GetWorld() ? GetWorld()->GetSubsystem<UMassDspManager>() : nullptr;
    if (!Manager) return -1;

    const EBuildPlaceMode Mode = Manager->GetCurrentPlaceMode();
    if (Mode == EBuildPlaceMode::None) return -1;

    for (int32 i = 0; i < SlotDefs.Num(); ++i)
    {
        const FHotbarSlotDef& Def = SlotDefs[i];
        if (Mode == EBuildPlaceMode::Building &&
            Def.Action == EHotbarAction::PlaceBuilding &&
            Def.BuildingType == Manager->GetPreviewBuildingType())
            return i;

        if (Mode == EBuildPlaceMode::Belt &&
            Def.Action == EHotbarAction::PlaceBelt &&
            Def.BeltType == Manager->GetPreviewBeltType())
            return i;
    }
    return -1;
}

void UMassDspHotbarWidget::SetSlotHighlight(int32 SlotIndex, bool bActive)
{
    if (!SlotBorders.IsValidIndex(SlotIndex) || !SlotBorders[SlotIndex]) return;

    const bool bUnlocked = SlotUnlockedStates.IsValidIndex(SlotIndex) ? SlotUnlockedStates[SlotIndex] : true;
    SlotBorders[SlotIndex]->SetBrushColor(
        !bUnlocked
            ? FLinearColor(0.18f, 0.18f, 0.18f, 0.55f)
            : (bActive
                ? FLinearColor(0.15f, 0.45f, 1.f, 0.92f)
                : FLinearColor(0.05f, 0.05f, 0.05f, 0.82f)));
}

// ─────────────────────────────────────────────────────────────────────────────
//  每帧建造预览更新
// ─────────────────────────────────────────────────────────────────────────────

void UMassDspHotbarWidget::UpdateBuildPreview()
{
    UMassDspManager* Manager = GetWorld() ? GetWorld()->GetSubsystem<UMassDspManager>() : nullptr;
    if (!Manager || Manager->GetCurrentPlaceMode() == EBuildPlaceMode::None) return;

    if (!GetWorldHitLocation(CachedHitLocation)) return;

    if (Manager->IsPreviewingBuilding())
    {
        Manager->UpdateBuildingPreviewTransform(FTransform(BuildingRotation, CachedHitLocation));
        return;
    }

    if (!Manager->IsPreviewingBelt()) return;

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

    if (Manager->BeltHasStartSlot())
        Manager->UpdateBeltPreviewEndPoint(BeltHoverSnapLocation, BeltHoverSnapRotation, BeltHoverSnapExtend);
}

// ─────────────────────────────────────────────────────────────────────────────
//  鼠标 / 功能键响应
// ─────────────────────────────────────────────────────────────────────────────

void UMassDspHotbarWidget::OnMouseLeftClick()
{
    UMassDspManager* Manager = GetWorld() ? GetWorld()->GetSubsystem<UMassDspManager>() : nullptr;
    if (!Manager) return;

    GetWorldHitLocation(CachedHitLocation);

    switch (Manager->GetCurrentPlaceMode())
    {
    case EBuildPlaceMode::Building:
        {
            FMassEntityHandle NewEntity = Manager->ConfirmPreviewBuilding();
            if (NewEntity.IsValid())
                UE_LOG(LogTemp, Log, TEXT("[Hotbar] 建筑放置成功 [Entity=%d:%d]"),
                   NewEntity.Index, NewEntity.SerialNumber);
            break;
        }
    case EBuildPlaceMode::Belt:
        {
            const FVector SelectPos = bBeltHoverSnapped ? BeltHoverSnapLocation : CachedHitLocation;
            if (Manager->SelectBeltSlot(SelectPos))
            {
                FBeltHandle Handle = Manager->ConfirmPreviewBelt();
                if (Handle.IsValid())
                    UE_LOG(LogTemp, Log, TEXT("[Hotbar] 传送带连接成功 [Handle=%d]"), Handle.Index);
            }
            break;
        }
    default: break;
    }
}

void UMassDspHotbarWidget::OnMouseRightClick()
{
    UMassDspManager* Manager = GetWorld() ? GetWorld()->GetSubsystem<UMassDspManager>() : nullptr;
    if (Manager) Manager->CancelAnyPreview();
}

void UMassDspHotbarWidget::OnScrollUp()
{
    UMassDspManager* Manager = GetWorld() ? GetWorld()->GetSubsystem<UMassDspManager>() : nullptr;
    if (!Manager || !Manager->IsPreviewingBuilding()) return;
    BuildingRotation.Yaw += BuildingRotationStep;
    Manager->UpdateBuildingPreviewTransform(FTransform(BuildingRotation, CachedHitLocation));
}

void UMassDspHotbarWidget::OnScrollDown()
{
    UMassDspManager* Manager = GetWorld() ? GetWorld()->GetSubsystem<UMassDspManager>() : nullptr;
    if (!Manager || !Manager->IsPreviewingBuilding()) return;
    BuildingRotation.Yaw -= BuildingRotationStep;
    Manager->UpdateBuildingPreviewTransform(FTransform(BuildingRotation, CachedHitLocation));
}

void UMassDspHotbarWidget::OnInteractKey()
{
    if (CurrentBuildingWidget)
    {
        CurrentBuildingWidget->CloseWidget();
        CurrentBuildingWidget = nullptr;
        return;
    }

    UMassDspManager* Manager = GetWorld() ? GetWorld()->GetSubsystem<UMassDspManager>() : nullptr;
    if (!Manager) return;

    APlayerController* PC = GetOwningPlayer();
    if (!PC || !PC->GetPawn()) return;

    const FVector PlayerLoc = PC->GetPawn()->GetActorLocation();
    FMassEntityHandle NearestEntity;
    EBuildingType NearestType = EBuildingType::None;
    FVector NearestLoc;

    if (!Manager->FindNearestBuilding(PlayerLoc, BuildingInteractRadius, NearestEntity, NearestType, NearestLoc,
                                      [this](const FVector& Loc) { return IsBuildingInViewCone(Loc); }))
        return;

    CreateBuildingWidgets(NearestEntity, NearestType);
}

// ─────────────────────────────────────────────────────────────────────────────
//  建筑交互 Widget 创建（UMG 蓝图自动生成）
// ─────────────────────────────────────────────────────────────────────────────

void UMassDspHotbarWidget::CreateBuildingWidgets(const FMassEntityHandle& Entity, EBuildingType Type)
{
    APlayerController* PC = GetOwningPlayer();
    if (!PC) return;

    AMassDspGameMode* GM = Cast<AMassDspGameMode>(GetWorld()->GetAuthGameMode());
    if (!GM || !GM->GameConfig) return;

    const FBuildingTypeConfig* Cfg = GM->GameConfig->GetBuildingConfig(Type);
    if (!Cfg || !Cfg->InteractionWidgetClass) return;

    UMassDspBuildingWidget* Widget = CreateWidget<UMassDspBuildingWidget>(PC, Cfg->InteractionWidgetClass);
    if (!Widget) return;

    Widget->InitWidget(Entity, Type);
    Widget->AddToViewport();
    CurrentBuildingWidget = Widget;

    FInputModeGameAndUI UIMode;
    UIMode.SetWidgetToFocus(CurrentBuildingWidget->TakeWidget());
    UIMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    PC->SetInputMode(UIMode);
    PC->bShowMouseCursor = true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  视锥检测
// ─────────────────────────────────────────────────────────────────────────────

bool UMassDspHotbarWidget::IsBuildingInViewCone(const FVector& WorldLoc) const
{
    APlayerController* PC = GetOwningPlayer();
    if (!PC) return false;

    int32 ViewW = 0, ViewH = 0;
    PC->GetViewportSize(ViewW, ViewH);
    if (ViewW <= 0 || ViewH <= 0) return false;

    FVector2D ScreenPos;
    if (!PC->ProjectWorldLocationToScreen(WorldLoc, ScreenPos, true)) return false;

    return FMath::Abs(ScreenPos.X - ViewW * 0.5f) < ViewW / 5
        && FMath::Abs(ScreenPos.Y - ViewH * 0.5f) < ViewH / 5;
}
