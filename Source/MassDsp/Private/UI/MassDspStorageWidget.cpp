#include "UI/MassDspStorageWidget.h"

#include "MassEntitySubsystem.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"

const FMassDspWarehouseFragment* UMassDspStorageWidget::GetFragment() const
{
    if (!TargetEntity.IsValid()) return nullptr;
    UMassEntitySubsystem* ESub = GetWorld() ? GetWorld()->GetSubsystem<UMassEntitySubsystem>() : nullptr;
    if (!ESub) return nullptr;
    FMassEntityManager& EM = ESub->GetMutableEntityManager();
    if (!EM.IsEntityValid(TargetEntity)) return nullptr;
    return EM.GetFragmentDataPtr<FMassDspWarehouseFragment>(TargetEntity);
}

EItemType UMassDspStorageWidget::GetSuggestedTransferItemType() const
{
    const FMassDspWarehouseFragment* Fragment = GetFragment();
    return Fragment ? Fragment->GetFirstItemType() : EItemType::None;
}

void UMassDspStorageWidget::RefreshWidgets()
{
    const FMassDspWarehouseFragment* F = GetFragment();
    if (!F) return;

    const int32 UsedSlotCount = F->GetUsedSlotCount();
    if (UsedSlotCount <= 0)
    {
        TextBlock_ItemType->SetText(FText::FromString(TEXT("空仓库")));
    }
    else if (UsedSlotCount == 1)
    {
        TextBlock_ItemType->SetText(GetItemTypeDisplayName(F->GetFirstItemType()));
    }
    else
    {
        TextBlock_ItemType->SetText(FText::Format(NSLOCTEXT("MassDsp", "StorageMultiItemType", "多种物品 ({0})"), FText::AsNumber(UsedSlotCount)));
    }

    TextBlock_Inventory->SetText(
        FText::Format(NSLOCTEXT("MassDsp", "StorageInventory", "{0} / {1}"),
            FText::AsNumber(F->GetInventoryCount()),
            FText::AsNumber(F->GetMaxInventory())));

    const float Fill = F->GetMaxInventory() > 0
        ? static_cast<float>(F->GetInventoryCount()) / static_cast<float>(F->GetMaxInventory())
        : 0.f;
    ProgressBar_Fill->SetPercent(Fill);
}
