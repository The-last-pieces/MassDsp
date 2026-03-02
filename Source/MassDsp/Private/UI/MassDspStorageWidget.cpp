#include "UI/MassDspStorageWidget.h"

#include "MassEntitySubsystem.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"

const FMassDspStorageFragment* UMassDspStorageWidget::GetFragment() const
{
    if (!TargetEntity.IsValid()) return nullptr;
    UMassEntitySubsystem* ESub = GetWorld() ? GetWorld()->GetSubsystem<UMassEntitySubsystem>() : nullptr;
    if (!ESub) return nullptr;
    FMassEntityManager& EM = ESub->GetMutableEntityManager();
    if (!EM.IsEntityValid(TargetEntity)) return nullptr;
    return EM.GetFragmentDataPtr<FMassDspStorageFragment>(TargetEntity);
}

void UMassDspStorageWidget::RefreshWidgets()
{
    const FMassDspStorageFragment* F = GetFragment();
    if (!F) return;

    // 存储物品名
    TextBlock_ItemType->SetText(GetItemTypeDisplayName(F->StoredItemType));

    // 库存文本
    TextBlock_Inventory->SetText(
        FText::Format(NSLOCTEXT("MassDsp", "StorageInventory", "{0} / {1}"),
            FText::AsNumber(F->InventoryCount),
            FText::AsNumber(F->MaxInventory)));

    // 填充进度条
    const float Fill = F->MaxInventory > 0
        ? static_cast<float>(F->InventoryCount) / static_cast<float>(F->MaxInventory)
        : 0.f;
    ProgressBar_Fill->SetPercent(Fill);
}
