#include "UI/MassDspBuildingWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "GameFramework/PlayerController.h"
#include "Inventory/MassDspPlayerInventoryComponent.h"
#include "MassDspGameMode.h"
#include "Subsystems/MassDspManager.h"

// 
//  生命周期
// 

void UMassDspBuildingWidget::NativeConstruct()
{
    Super::NativeConstruct();

    // 自动绑定关闭按钮（若蓝图中存在名为 Button_Close 的按钮）
    if (Button_Close && !Button_Close->OnClicked.IsBound())
    {
        Button_Close->OnClicked.AddDynamic(this, &UMassDspBuildingWidget::OnCloseButtonClicked);
    }

    if (Button_PrevTransferItem && !Button_PrevTransferItem->OnClicked.IsBound())
    {
        Button_PrevTransferItem->OnClicked.AddDynamic(this, &UMassDspBuildingWidget::OnPrevTransferItemClicked);
    }

    if (Button_NextTransferItem && !Button_NextTransferItem->OnClicked.IsBound())
    {
        Button_NextTransferItem->OnClicked.AddDynamic(this, &UMassDspBuildingWidget::OnNextTransferItemClicked);
    }

    if (Button_StoreOne && !Button_StoreOne->OnClicked.IsBound())
    {
        Button_StoreOne->OnClicked.AddDynamic(this, &UMassDspBuildingWidget::OnStoreOneClicked);
    }

    if (Button_TakeOne && !Button_TakeOne->OnClicked.IsBound())
    {
        Button_TakeOne->OnClicked.AddDynamic(this, &UMassDspBuildingWidget::OnTakeOneClicked);
    }

    if (Button_StoreAll && !Button_StoreAll->OnClicked.IsBound())
    {
        Button_StoreAll->OnClicked.AddDynamic(this, &UMassDspBuildingWidget::OnStoreAllClicked);
    }

    if (Button_TakeAll && !Button_TakeAll->OnClicked.IsBound())
    {
        Button_TakeAll->OnClicked.AddDynamic(this, &UMassDspBuildingWidget::OnTakeAllClicked);
    }
}

void UMassDspBuildingWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

    RefreshAccum += InDeltaTime;
    if (RefreshAccum >= RefreshInterval)
    {
        RefreshAccum = 0.f;
        RefreshWidgets();
        RefreshTransferWidgets();
    }
}

// 
//  公共接口
// 

void UMassDspBuildingWidget::InitWidget(FMassEntityHandle InEntity, EBuildingType InBuildingType)
{
    TargetEntity  = InEntity;
    BuildingType  = InBuildingType;

    // 写入标题（若蓝图放置了 TextBlock_Title）
    if (TextBlock_Title)
    {
        const UEnum* Enum = StaticEnum<EBuildingType>();
        FText Title = Enum
            ? Enum->GetDisplayNameTextByValue(static_cast<int64>(BuildingType))
            : FText::FromString(TEXT("Building"));
        TextBlock_Title->SetText(Title);
    }

    // 立即刷新一次，避免第一帧空白
    RefreshAccum = RefreshInterval;
    EnsureTransferItemSelected();
}

void UMassDspBuildingWidget::CloseWidget()
{
    RemoveFromParent();

    APlayerController* PC = GetOwningPlayer();
    if (!PC) return;

    FInputModeGameOnly GameOnly;
    PC->SetInputMode(GameOnly);
    PC->bShowMouseCursor = false;
}

int32 UMassDspBuildingWidget::TryStoreItemsFromPlayer(EItemType ItemType, int32 Quantity)
{
    UMassDspManager* Manager = GetDspManager();
    return Manager ? Manager->TryStoreItemsFromPlayer(TargetEntity, ItemType, Quantity) : 0;
}

int32 UMassDspBuildingWidget::TryTakeItemsForPlayer(EItemType ItemType, int32 Quantity)
{
    UMassDspManager* Manager = GetDspManager();
    return Manager ? Manager->TryTakeItemsForPlayer(TargetEntity, ItemType, Quantity) : 0;
}

// 
//  工具函数
// 

FText UMassDspBuildingWidget::GetItemTypeDisplayName(EItemType ItemType)
{
    const UEnum* Enum = StaticEnum<EItemType>();
    return Enum
        ? Enum->GetDisplayNameTextByValue(static_cast<int64>(ItemType))
        : FText::FromString(TEXT("Unknown"));
}

FText UMassDspBuildingWidget::GetRecipeTypeDisplayName(ERecipeType RecipeType)
{
    const UEnum* Enum = StaticEnum<ERecipeType>();
    return Enum
        ? Enum->GetDisplayNameTextByValue(static_cast<int64>(RecipeType))
        : FText::FromString(TEXT("None"));
}

UMassDspManager* UMassDspBuildingWidget::GetDspManager() const
{
    return GetWorld() ? GetWorld()->GetSubsystem<UMassDspManager>() : nullptr;
}

UGameConfigData* UMassDspBuildingWidget::GetGameConfig() const
{
    const UWorld* World = GetWorld();
    const AMassDspGameMode* GM = World ? Cast<AMassDspGameMode>(World->GetAuthGameMode()) : nullptr;
    return GM ? GM->GameConfig.Get() : nullptr;
}

UMassDspPlayerInventoryComponent* UMassDspBuildingWidget::GetPlayerInventory() const
{
    const APlayerController* PC = GetOwningPlayer();
    const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
    return Pawn ? Pawn->FindComponentByClass<UMassDspPlayerInventoryComponent>() : nullptr;
}

void UMassDspBuildingWidget::BuildTransferSelectableItems(TArray<EItemType>& OutItems) const
{
    OutItems.Reset();

    const UGameConfigData* GameConfig = GetGameConfig();
    const UEnum* ItemEnum = StaticEnum<EItemType>();
    if (!GameConfig || !ItemEnum) return;

    for (int32 Index = 0; Index < ItemEnum->NumEnums() - 1; ++Index)
    {
        const EItemType ItemType = static_cast<EItemType>(ItemEnum->GetValueByIndex(Index));
        if (ItemType == EItemType::None) continue;
        if (GameConfig->GetItemConfig(ItemType))
        {
            OutItems.Add(ItemType);
        }
    }
}

void UMassDspBuildingWidget::EnsureTransferItemSelected()
{
    TArray<EItemType> Items;
    BuildTransferSelectableItems(Items);
    if (Items.IsEmpty())
    {
        SelectedTransferItem = EItemType::None;
        return;
    }

    if (Items.Contains(SelectedTransferItem)) return;

    const EItemType Suggested = GetSuggestedTransferItemType();
    if (Suggested != EItemType::None && Items.Contains(Suggested))
    {
        SelectedTransferItem = Suggested;
        return;
    }

    if (const UMassDspPlayerInventoryComponent* Inventory = GetPlayerInventory())
    {
        TArray<EItemType> ActiveItems;
        Inventory->GetActiveItems(ActiveItems);
        for (EItemType ItemType : ActiveItems)
        {
            if (Items.Contains(ItemType))
            {
                SelectedTransferItem = ItemType;
                return;
            }
        }
    }

    SelectedTransferItem = Items[0];
}

void UMassDspBuildingWidget::RefreshTransferWidgets()
{
    EnsureTransferItemSelected();

    if (TextBlock_TransferItem)
    {
        TextBlock_TransferItem->SetText(SelectedTransferItem != EItemType::None
            ? GetItemTypeDisplayName(SelectedTransferItem)
            : FText::FromString(TEXT("未选择物品")));
    }

    if (TextBlock_TransferStatus)
    {
        const UMassDspPlayerInventoryComponent* Inventory = GetPlayerInventory();
        const int32 Count = Inventory ? Inventory->GetItemCount(SelectedTransferItem) : 0;
        const FString Prefix = FString::Printf(TEXT("背包持有: %d"), Count);
        TextBlock_TransferStatus->SetText(LastTransferStatus.IsEmpty()
            ? FText::FromString(Prefix)
            : FText::FromString(Prefix + TEXT(" | ") + LastTransferStatus.ToString()));
    }
}

void UMassDspBuildingWidget::ChangeTransferItem(int32 Direction)
{
    TArray<EItemType> Items;
    BuildTransferSelectableItems(Items);
    if (Items.IsEmpty()) return;

    EnsureTransferItemSelected();
    int32 CurrentIndex = Items.Find(SelectedTransferItem);
    if (CurrentIndex == INDEX_NONE) CurrentIndex = 0;

    const int32 NextIndex = (CurrentIndex + Direction + Items.Num()) % Items.Num();
    SelectedTransferItem = Items[NextIndex];
    LastTransferStatus = FText();
    RefreshTransferWidgets();
}

void UMassDspBuildingWidget::ExecuteStore(bool bStoreAll)
{
    EnsureTransferItemSelected();
    if (SelectedTransferItem == EItemType::None) return;

    const int32 Quantity = bStoreAll ? MAX_int32 : 1;
    const int32 Stored = TryStoreItemsFromPlayer(SelectedTransferItem, Quantity);
    LastTransferStatus = Stored > 0
        ? FText::FromString(FString::Printf(TEXT("已存入 %d"), Stored))
        : FText::FromString(TEXT("存入失败"));
    RefreshWidgets();
    RefreshTransferWidgets();
}

void UMassDspBuildingWidget::ExecuteTake(bool bTakeAll)
{
    EnsureTransferItemSelected();
    if (SelectedTransferItem == EItemType::None) return;

    const int32 Quantity = bTakeAll ? MAX_int32 : 1;
    const int32 Taken = TryTakeItemsForPlayer(SelectedTransferItem, Quantity);
    LastTransferStatus = Taken > 0
        ? FText::FromString(FString::Printf(TEXT("已取出 %d"), Taken))
        : FText::FromString(TEXT("取出失败"));
    RefreshWidgets();
    RefreshTransferWidgets();
}

// 
//  私有
// 

void UMassDspBuildingWidget::OnCloseButtonClicked()
{
    CloseWidget();
}

void UMassDspBuildingWidget::OnPrevTransferItemClicked()
{
    ChangeTransferItem(-1);
}

void UMassDspBuildingWidget::OnNextTransferItemClicked()
{
    ChangeTransferItem(1);
}

void UMassDspBuildingWidget::OnStoreOneClicked()
{
    ExecuteStore(false);
}

void UMassDspBuildingWidget::OnTakeOneClicked()
{
    ExecuteTake(false);
}

void UMassDspBuildingWidget::OnStoreAllClicked()
{
    ExecuteStore(true);
}

void UMassDspBuildingWidget::OnTakeAllClicked()
{
    ExecuteTake(true);
}
