#include "UI/MassDspMinerWidget.h"

#include "MassEntitySubsystem.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"

const FMassDspMinerFragment* UMassDspMinerWidget::GetFragment() const
{
    if (!TargetEntity.IsValid()) return nullptr;
    UMassEntitySubsystem* ESub = GetWorld() ? GetWorld()->GetSubsystem<UMassEntitySubsystem>() : nullptr;
    if (!ESub) return nullptr;
    FMassEntityManager& EM = ESub->GetMutableEntityManager();
    if (!EM.IsEntityValid(TargetEntity)) return nullptr;
    return EM.GetFragmentDataPtr<FMassDspMinerFragment>(TargetEntity);
}

void UMassDspMinerWidget::RefreshWidgets()
{
    const FMassDspMinerFragment* F = GetFragment();
    if (!F) return;

    // 生产进度条（从绝对时间戳推算 [0,1]）
    const float WorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
    ProgressBar_Production->SetPercent(F->GetProductionProgress(WorldTime));

    // 物品类型名称
    TextBlock_ItemType->SetText(GetItemTypeDisplayName(F->StoredItemType));

    // 库存文本："当前 / 最大"
    TextBlock_Inventory->SetText(
        FText::Format(NSLOCTEXT("MassDsp", "MinerInventory", "{0} / {1}"),
            FText::AsNumber(F->InventoryCount),
            FText::AsNumber(F->MaxInventory)));

    // 生产间隔（可选）
    if (TextBlock_Interval)
    {
        TextBlock_Interval->SetText(
            FText::Format(NSLOCTEXT("MassDsp", "MinerInterval", "每 {0} 秒产出 1 个"),
                FText::AsNumber(FMath::RoundToInt(F->ProductionInterval))));
    }
}
