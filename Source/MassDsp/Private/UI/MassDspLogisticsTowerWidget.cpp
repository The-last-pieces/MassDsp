#include "UI/MassDspLogisticsTowerWidget.h"

#include "MassEntitySubsystem.h"
#include "Components/Button.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Subsystems/MassDspLogisticsSubsystem.h"
#include "Subsystems/MassDspManager.h"

void UMassDspLogisticsTowerWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (Button_PrevMode && !Button_PrevMode->OnClicked.IsBound())
    {
        Button_PrevMode->OnClicked.AddDynamic(this, &UMassDspLogisticsTowerWidget::OnPrevModeClicked);
    }

    if (Button_NextMode && !Button_NextMode->OnClicked.IsBound())
    {
        Button_NextMode->OnClicked.AddDynamic(this, &UMassDspLogisticsTowerWidget::OnNextModeClicked);
    }
}

// 

bool UMassDspLogisticsTowerWidget::GetFragments(
    const FMassDspStorageFragment*& OutStorage,
    const FMassDspLogisticsTowerFragment*& OutTower) const
{
    OutStorage = nullptr;
    OutTower   = nullptr;

    if (!TargetEntity.IsValid()) return false;

    UMassEntitySubsystem* ESub = GetWorld()
        ? GetWorld()->GetSubsystem<UMassEntitySubsystem>()
        : nullptr;
    if (!ESub) return false;

    FMassEntityManager& EM = ESub->GetMutableEntityManager();
    if (!EM.IsEntityValid(TargetEntity)) return false;

    OutStorage = EM.GetFragmentDataPtr<FMassDspStorageFragment>(TargetEntity);
    OutTower   = EM.GetFragmentDataPtr<FMassDspLogisticsTowerFragment>(TargetEntity);

    return OutStorage && OutTower;
}

// 

void UMassDspLogisticsTowerWidget::RefreshWidgets()
{
    const FMassDspStorageFragment*        SF = nullptr;
    const FMassDspLogisticsTowerFragment* TF = nullptr;
    if (!GetFragments(SF, TF)) return;

    // ── 物品类型：优先显示塔配置的 ItemType，再回退到实际存储类型 ──────────
    const EItemType DisplayItem = (TF->ItemType != EItemType::None)
        ? TF->ItemType
        : SF->StoredItemType;
    TextBlock_ItemType->SetText(GetItemTypeDisplayName(DisplayItem));

    // ── 库存数量 ─────────────────────────────────────────────────────────────
    TextBlock_Inventory->SetText(
        FText::Format(NSLOCTEXT("MassDsp", "LogisticsTowerInventory", "{0} / {1}"),
            FText::AsNumber(SF->InventoryCount),
            FText::AsNumber(SF->MaxInventory)));

    // ── 库存进度条 ───────────────────────────────────────────────────────────
    const float Fill = SF->MaxInventory > 0
        ? static_cast<float>(SF->InventoryCount) / static_cast<float>(SF->MaxInventory)
        : 0.f;
    ProgressBar_Storage->SetPercent(Fill);

    // ── 可选：运行模式 ───────────────────────────────────────────────────────
    if (TextBlock_Mode)
    {
        FText ModeText;
        switch (TF->TowerMode)
        {
        case ELogisticsTowerMode::Supply:
            ModeText = NSLOCTEXT("MassDsp", "LogisticsTowerModeSupply",  "供应");
            break;
        case ELogisticsTowerMode::Demand:
            ModeText = NSLOCTEXT("MassDsp", "LogisticsTowerModeDemand",  "需求");
            break;
        default:
            ModeText = NSLOCTEXT("MassDsp", "LogisticsTowerModeStorage", "仓储");
            break;
        }
        TextBlock_Mode->SetText(ModeText);
    }

    // ── 可选：请求阈值 ───────────────────────────────────────────────────────
    if (TextBlock_Threshold)
    {
        TextBlock_Threshold->SetText(FText::AsNumber(TF->RequestThreshold));
    }

    // ── 可选：单次运量 ───────────────────────────────────────
    if (TextBlock_DroneCount)
    {
        TextBlock_DroneCount->SetText(FText::AsNumber(TF->DroneCargoCount));
    }

    // ── 可选：归属无人机状态 + 来航无人机数 ─────────────────────────
    if (TextBlock_OwnedDrones || TextBlock_IncomingDrones)
    {
        if (UMassDspLogisticsSubsystem* LogSub = GetWorld()
                ? GetWorld()->GetSubsystem<UMassDspLogisticsSubsystem>() : nullptr)
        {
            const FTowerDroneStatus Status = LogSub->QueryTowerDroneStatus(TargetEntity);

            if (TextBlock_OwnedDrones)
            {
                TextBlock_OwnedDrones->SetText(
                    FText::Format(
                        NSLOCTEXT("MassDsp", "TowerOwnedDrones", "外派 {0} / 休息 {1}"),
                        FText::AsNumber(Status.OwnedDeployed),
                        FText::AsNumber(Status.OwnedResting)));
            }

            if (TextBlock_IncomingDrones)
            {
                TextBlock_IncomingDrones->SetText(
                    Status.Incoming > 0
                        ? FText::Format(
                            NSLOCTEXT("MassDsp", "TowerIncomingDrones", "来航 {0} 架"),
                            FText::AsNumber(Status.Incoming))
                        : NSLOCTEXT("MassDsp", "TowerIncomingNone", "—"));
            }
        }
    }
}

void UMassDspLogisticsTowerWidget::ChangeTowerMode(int32 Direction)
{
    const FMassDspStorageFragment* SF = nullptr;
    const FMassDspLogisticsTowerFragment* TF = nullptr;
    if (!GetFragments(SF, TF)) return;

    UMassDspManager* Manager = GetDspManager();
    if (!Manager) return;

    static const TArray<ELogisticsTowerMode> Modes = {
        ELogisticsTowerMode::Supply,
        ELogisticsTowerMode::Demand,
        ELogisticsTowerMode::Storage,
    };

    int32 CurrentIndex = Modes.Find(TF->TowerMode);
    if (CurrentIndex == INDEX_NONE) CurrentIndex = 0;

    const int32 NewIndex = (CurrentIndex + Direction + Modes.Num()) % Modes.Num();
    Manager->SetLogisticsTowerMode(TargetEntity, Modes[NewIndex]);
    RefreshWidgets();
}

void UMassDspLogisticsTowerWidget::OnPrevModeClicked()
{
    ChangeTowerMode(-1);
}

void UMassDspLogisticsTowerWidget::OnNextModeClicked()
{
    ChangeTowerMode(1);
}
