#include "UI/MassDspLogisticsTowerWidget.h"

#include "MassEntitySubsystem.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"

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
    const FMassDspStorageFragment*       SF = nullptr;
    const FMassDspLogisticsTowerFragment* TF = nullptr;
    if (!GetFragments(SF, TF)) return;

    //  物品类型：优先显示 DesiredItemType，其次显示实际存储类型 
    const EItemType DisplayItem = (TF->DesiredItemType != EItemType::None)
        ? TF->DesiredItemType
        : SF->StoredItemType;
    TextBlock_ItemType->SetText(GetItemTypeDisplayName(DisplayItem));

    //  库存数量 
    TextBlock_Inventory->SetText(
        FText::Format(NSLOCTEXT("MassDsp", "LogisticsTowerInventory", "{0} / {1}"),
            FText::AsNumber(SF->InventoryCount),
            FText::AsNumber(SF->MaxInventory)));

    //  库存进度条 
    const float Fill = SF->MaxInventory > 0
        ? static_cast<float>(SF->InventoryCount) / static_cast<float>(SF->MaxInventory)
        : 0.f;
    ProgressBar_Storage->SetPercent(Fill);

    //  可选控件：覆盖半径 
    if (TextBlock_CoverageRadius)
    {
        // 将 cm 转换并显示为 m，小数点两位
        const float RadiusM = TF->CoverageRadius / 100.f;
        TextBlock_CoverageRadius->SetText(
            FText::Format(NSLOCTEXT("MassDsp", "LogisticsTowerRadius", "{0} m"),
                FText::AsNumber(FMath::RoundToInt(RadiusM))));
    }

    //  可选控件：无人机上限（从 Actor CDO 读取静态配置） 
    // 注意：当前仅展示扫描间隔配置，实时无人机计数留待后续扩展
    if (TextBlock_DroneCount)
    {
        TextBlock_DroneCount->SetText(
            FText::Format(
                NSLOCTEXT("MassDsp", "LogisticsTowerScanInterval", "扫描间隔 {0} s"),
                FText::AsNumber(FMath::RoundToInt(TF->ScanInterval))));
    }

    //  可选控件：运行状态 
    if (TextBlock_Status)
    {
        FText StatusText;
        if (!TF->bAcceptsRequests)
        {
            StatusText = NSLOCTEXT("MassDsp", "LogisticsTowerStatusPaused", "暂停调度");
        }
        else if (TF->bDirty)
        {
            StatusText = NSLOCTEXT("MassDsp", "LogisticsTowerStatusActive", "调度中");
        }
        else
        {
            StatusText = NSLOCTEXT("MassDsp", "LogisticsTowerStatusIdle", "空闲");
        }
        TextBlock_Status->SetText(StatusText);
    }
}
