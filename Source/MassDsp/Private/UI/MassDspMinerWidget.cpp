#include "UI/MassDspMinerWidget.h"

#include "MassEntitySubsystem.h"
#include "Components/Button.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Subsystems/MassDspManager.h"

namespace
{
TArray<EItemType> GetMinerSelectableItems()
{
    return {
        EItemType::IronOre,
        EItemType::CopperOre,
        EItemType::Stone,
        EItemType::Coal,
    };
}
}

void UMassDspMinerWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (Button_PrevItemType && !Button_PrevItemType->OnClicked.IsBound())
    {
        Button_PrevItemType->OnClicked.AddDynamic(this, &UMassDspMinerWidget::OnPrevItemTypeClicked);
    }

    if (Button_NextItemType && !Button_NextItemType->OnClicked.IsBound())
    {
        Button_NextItemType->OnClicked.AddDynamic(this, &UMassDspMinerWidget::OnNextItemTypeClicked);
    }
}

const FMassDspMinerFragment* UMassDspMinerWidget::GetFragment() const
{
    if (!TargetEntity.IsValid()) return nullptr;
    UMassEntitySubsystem* ESub = GetWorld() ? GetWorld()->GetSubsystem<UMassEntitySubsystem>() : nullptr;
    if (!ESub) return nullptr;
    FMassEntityManager& EM = ESub->GetMutableEntityManager();
    if (!EM.IsEntityValid(TargetEntity)) return nullptr;
    return EM.GetFragmentDataPtr<FMassDspMinerFragment>(TargetEntity);
}

EItemType UMassDspMinerWidget::GetSuggestedTransferItemType() const
{
    const FMassDspMinerFragment* Fragment = GetFragment();
    return Fragment ? Fragment->StoredItemType : EItemType::None;
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
        auto Num = FText::AsNumber(FMath::RoundToInt(1.0 / F->ProductionInterval));
        TextBlock_Interval->SetText(FText::Format(NSLOCTEXT("MassDsp", "MinerInterval", "每秒产出 {0} 个"), Num));
    }
}

void UMassDspMinerWidget::ChangeMinerItemType(int32 Direction)
{
    const FMassDspMinerFragment* Fragment = GetFragment();
    UMassDspManager* Manager = GetDspManager();
    if (!Fragment || !Manager) return;

    const TArray<EItemType> SelectableItems = GetMinerSelectableItems();
    if (SelectableItems.IsEmpty()) return;

    int32 CurrentIndex = SelectableItems.Find(Fragment->StoredItemType);
    if (CurrentIndex == INDEX_NONE) CurrentIndex = 0;

    const int32 NewIndex = (CurrentIndex + Direction + SelectableItems.Num()) % SelectableItems.Num();
    Manager->SetMinerItemType(TargetEntity, SelectableItems[NewIndex]);
    RefreshWidgets();
}

void UMassDspMinerWidget::OnPrevItemTypeClicked()
{
    ChangeMinerItemType(-1);
}

void UMassDspMinerWidget::OnNextItemTypeClicked()
{
    ChangeMinerItemType(1);
}
