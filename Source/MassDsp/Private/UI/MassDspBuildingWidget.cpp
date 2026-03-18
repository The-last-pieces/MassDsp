#include "UI/MassDspBuildingWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "GameFramework/PlayerController.h"
#include "Inventory/MassDspPlayerInventoryComponent.h"
#include "Fragments/MassDspAssemblerFragment.h"
#include "Fragments/MassDspMinerFragment.h"
#include "Fragments/MassDspStorageFragment.h"
#include "Fragments/MassDspWarehouseFragment.h"
#include "MassEntitySubsystem.h"
#include "MassDspGameMode.h"
#include "Subsystems/MassDspManager.h"
#include "Subsystems/MassDspTechTreeSubsystem.h"
#include "UI/MassDspItemGridUtils.h"
#include "UI/MassDspItemSlotButton.h"

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

    MassDspItemGridUtils::CollectGridSlots(this, TEXT("PlayerSlot"), PlayerGridSlotCount, TEXT("Player"), PlayerGridSlots);
    MassDspItemGridUtils::CollectGridSlots(this, TEXT("BuildingSlot"), BuildingGridSlotCount, TEXT("Building"), BuildingGridSlots);

    for (const FMassDspItemGridSlotRefs& GridSlot : PlayerGridSlots)
    {
        if (GridSlot.Button)
        {
            GridSlot.Button->OnItemSlotClicked.RemoveDynamic(this, &UMassDspBuildingWidget::OnItemSlotClicked);
            GridSlot.Button->OnItemSlotClicked.AddDynamic(this, &UMassDspBuildingWidget::OnItemSlotClicked);
        }
    }

    for (const FMassDspItemGridSlotRefs& GridSlot : BuildingGridSlots)
    {
        if (GridSlot.Button)
        {
            GridSlot.Button->OnItemSlotClicked.RemoveDynamic(this, &UMassDspBuildingWidget::OnItemSlotClicked);
            GridSlot.Button->OnItemSlotClicked.AddDynamic(this, &UMassDspBuildingWidget::OnItemSlotClicked);
        }
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
    TargetEntity = InEntity;
    BuildingType = InBuildingType;

    // 写入标题（若蓝图放置了 TextBlock_Title）
    if (TextBlock_Title)
    {
        TextBlock_Title->SetText(MassDspEnumText::GetBuildingType(BuildingType));
    }

    // 立即刷新一次，避免第一帧空白
    RefreshAccum = RefreshInterval;
    RefreshWidgets();
    RefreshTransferWidgets();
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
    return MassDspEnumText::GetItemType(ItemType);
}

FText UMassDspBuildingWidget::GetRecipeTypeDisplayName(ERecipeType RecipeType)
{
    return MassDspEnumText::GetRecipeType(RecipeType);
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

UMassDspTechTreeSubsystem* UMassDspBuildingWidget::GetTechTreeSubsystem() const
{
    return GetWorld() ? GetWorld()->GetSubsystem<UMassDspTechTreeSubsystem>() : nullptr;
}

UMassDspPlayerInventoryComponent* UMassDspBuildingWidget::GetPlayerInventory() const
{
    const APlayerController* PC = GetOwningPlayer();
    const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
    return Pawn ? Pawn->FindComponentByClass<UMassDspPlayerInventoryComponent>() : nullptr;
}

void UMassDspBuildingWidget::CollectBuildingInventoryEntries(TArray<FInventoryEntryView>& OutEntries) const
{
    OutEntries.Reset();

    if (!TargetEntity.IsValid()) return;

    UMassEntitySubsystem* EntitySubsystem = GetWorld() ? GetWorld()->GetSubsystem<UMassEntitySubsystem>() : nullptr;
    if (!EntitySubsystem) return;

    FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
    if (!EntityManager.IsEntityValid(TargetEntity)) return;

    auto AppendEntry = [&OutEntries](EItemType ItemType, int32 Quantity)
    {
        if (ItemType == EItemType::None || Quantity <= 0) return;

        for (FInventoryEntryView& Entry : OutEntries)
        {
            if (Entry.ItemType == ItemType)
            {
                Entry.Quantity += Quantity;
                return;
            }
        }

        FInventoryEntryView Entry;
        Entry.ItemType = ItemType;
        Entry.Quantity = Quantity;
        OutEntries.Add(Entry);
    };

    if (const FMassDspMinerFragment* Miner = EntityManager.GetFragmentDataPtr<FMassDspMinerFragment>(TargetEntity))
    {
        AppendEntry(Miner->StoredItemType, Miner->InventoryCount);
    }

    if (const FMassDspWarehouseFragment* Warehouse = EntityManager.GetFragmentDataPtr<FMassDspWarehouseFragment>(TargetEntity))
    {
        Warehouse->GetActiveEntries(OutEntries);
    }

    if (const FMassDspStorageFragment* Storage = EntityManager.GetFragmentDataPtr<FMassDspStorageFragment>(TargetEntity))
    {
        AppendEntry(Storage->StoredItemType, Storage->InventoryCount);
    }

    if (const FMassDspAssemblerFragment* Assembler = EntityManager.GetFragmentDataPtr<FMassDspAssemblerFragment>(TargetEntity))
    {
        for (const FBufferEntry& Entry : Assembler->InputBuffers)
        {
            AppendEntry(Entry.ItemType, Entry.Amount);
        }

        for (const FBufferEntry& Entry : Assembler->OutputBuffers)
        {
            AppendEntry(Entry.ItemType, Entry.Amount);
        }
    }
}

void UMassDspBuildingWidget::RefreshTransferWidgets()
{
    CachedPlayerEntries.Reset();
    if (const UMassDspPlayerInventoryComponent* Inventory = GetPlayerInventory())
    {
        Inventory->GetActiveEntries(CachedPlayerEntries);
    }

    CollectBuildingInventoryEntries(CachedBuildingEntries);

    if (TextBlock_PlayerSummary)
    {
        TextBlock_PlayerSummary->SetText(BuildPlayerSummaryText());
    }

    if (TextBlock_PlayerHint)
    {
        TextBlock_PlayerHint->SetText(FText::FromString(TEXT("点击左侧格子，将整组物品存入建筑")));
    }

    if (TextBlock_BuildingSummary)
    {
        TextBlock_BuildingSummary->SetText(BuildBuildingSummaryText());
    }

    if (TextBlock_BuildingHint)
    {
        TextBlock_BuildingHint->SetText(FText::FromString(TEXT("点击右侧格子，将整组物品取回背包")));
    }

    if (TextBlock_TransferStatus)
    {
        TextBlock_TransferStatus->SetText(LastTransferStatus.IsEmpty()
                                              ? FText::FromString(TEXT("点击任意物品格子即可自动双向传输"))
                                              : LastTransferStatus);
    }

    const UGameConfigData* GameConfig = GetGameConfig();
    MassDspItemGridUtils::ApplyGridEntries(GameConfig, PlayerGridSlots, CachedPlayerEntries, true, FText::FromString(TEXT("背包为空")));
    MassDspItemGridUtils::ApplyGridEntries(GameConfig, BuildingGridSlots, CachedBuildingEntries, true, FText::FromString(TEXT("建筑为空")));
}

FText UMassDspBuildingWidget::BuildPlayerSummaryText() const
{
    const UMassDspPlayerInventoryComponent* Inventory = GetPlayerInventory();
    if (!Inventory)
    {
        return FText::FromString(TEXT("背包未连接"));
    }

    return FText::Format(NSLOCTEXT("MassDsp", "PlayerInventorySummary", "背包 {0} / {1}"),
                         FText::AsNumber(Inventory->GetTotalItemCount()),
                         FText::AsNumber(Inventory->GetCapacity()));
}

FText UMassDspBuildingWidget::BuildBuildingSummaryText() const
{
    if (!TargetEntity.IsValid())
    {
        return FText::FromString(TEXT("建筑未连接"));
    }

    UMassEntitySubsystem* EntitySubsystem = GetWorld() ? GetWorld()->GetSubsystem<UMassEntitySubsystem>() : nullptr;
    if (!EntitySubsystem)
    {
        return FText::FromString(TEXT("建筑状态不可用"));
    }

    FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
    if (!EntityManager.IsEntityValid(TargetEntity))
    {
        return FText::FromString(TEXT("建筑实体失效"));
    }

    if (const FMassDspMinerFragment* Miner = EntityManager.GetFragmentDataPtr<FMassDspMinerFragment>(TargetEntity))
    {
        return FText::Format(NSLOCTEXT("MassDsp", "MinerGridSummary", "矿机缓存 {0} / {1}"),
                             FText::AsNumber(Miner->InventoryCount),
                             FText::AsNumber(Miner->MaxInventory));
    }

    if (const FMassDspWarehouseFragment* Warehouse = EntityManager.GetFragmentDataPtr<FMassDspWarehouseFragment>(TargetEntity))
    {
        return FText::Format(NSLOCTEXT("MassDsp", "WarehouseGridSummary", "仓库库存 {0} / {1}"),
                             FText::AsNumber(Warehouse->GetInventoryCount()),
                             FText::AsNumber(Warehouse->GetMaxInventory()));
    }

    if (const FMassDspStorageFragment* Storage = EntityManager.GetFragmentDataPtr<FMassDspStorageFragment>(TargetEntity))
    {
        return FText::Format(NSLOCTEXT("MassDsp", "StorageGridSummary", "建筑库存 {0} / {1}"),
                             FText::AsNumber(Storage->InventoryCount),
                             FText::AsNumber(Storage->MaxInventory));
    }

    if (EntityManager.GetFragmentDataPtr<FMassDspAssemblerFragment>(TargetEntity))
    {
        int32 TotalBuffered = 0;
        for (const FInventoryEntryView& Entry : CachedBuildingEntries)
        {
            TotalBuffered += Entry.Quantity;
        }
        return FText::Format(NSLOCTEXT("MassDsp", "AssemblerGridSummary", "输入/输出缓冲 {0}"),
                             FText::AsNumber(TotalBuffered));
    }

    return FText::FromString(TEXT("可交互库存"));
}

void UMassDspBuildingWidget::HandleTransferFromPlayerSlot(int32 SlotIndex)
{
    if (!CachedPlayerEntries.IsValidIndex(SlotIndex)) return;

    const FInventoryEntryView& Entry = CachedPlayerEntries[SlotIndex];
    const int32 Stored = TryStoreItemsFromPlayer(Entry.ItemType, Entry.Quantity);
    LastTransferStatus = Stored > 0
                             ? FText::Format(NSLOCTEXT("MassDsp", "StoreStatus", "已存入 {0} x {1}"), GetItemTypeDisplayName(Entry.ItemType), FText::AsNumber(Stored))
                             : FText::Format(NSLOCTEXT("MassDsp", "StoreFailedStatus", "{0} 无法存入当前建筑"), GetItemTypeDisplayName(Entry.ItemType));

    RefreshWidgets();
    RefreshTransferWidgets();
}

void UMassDspBuildingWidget::HandleTransferFromBuildingSlot(int32 SlotIndex)
{
    if (!CachedBuildingEntries.IsValidIndex(SlotIndex)) return;

    const FInventoryEntryView& Entry = CachedBuildingEntries[SlotIndex];
    const int32 Taken = TryTakeItemsForPlayer(Entry.ItemType, Entry.Quantity);
    LastTransferStatus = Taken > 0
                             ? FText::Format(NSLOCTEXT("MassDsp", "TakeStatus", "已取出 {0} x {1}"), GetItemTypeDisplayName(Entry.ItemType), FText::AsNumber(Taken))
                             : FText::Format(NSLOCTEXT("MassDsp", "TakeFailedStatus", "{0} 无法取回背包"), GetItemTypeDisplayName(Entry.ItemType));

    RefreshWidgets();
    RefreshTransferWidgets();
}

void UMassDspBuildingWidget::OnItemSlotClicked(UMassDspItemSlotButton* ClickedButton)
{
    if (!ClickedButton) return;

    if (ClickedButton->SlotGroup == TEXT("Player"))
    {
        HandleTransferFromPlayerSlot(ClickedButton->SlotIndex);
    }
    else if (ClickedButton->SlotGroup == TEXT("Building"))
    {
        HandleTransferFromBuildingSlot(ClickedButton->SlotIndex);
    }
}

// 
//  私有
// 

void UMassDspBuildingWidget::OnCloseButtonClicked()
{
    CloseWidget();
}
