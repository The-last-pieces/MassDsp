#include "Inventory/MassDspPlayerInventoryComponent.h"

UMassDspPlayerInventoryComponent::UMassDspPlayerInventoryComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UMassDspPlayerInventoryComponent::BeginPlay()
{
    Super::BeginPlay();
    Inventory.Initialize(MaxInventoryItems);
}

int32 UMassDspPlayerInventoryComponent::AddItem(EItemType ItemType, int32 Quantity)
{
    return Inventory.TryAddItem(ItemType, Quantity);
}

int32 UMassDspPlayerInventoryComponent::RemoveItem(EItemType ItemType, int32 Quantity)
{
    return Inventory.TryRemoveItem(ItemType, Quantity);
}

int32 UMassDspPlayerInventoryComponent::GetItemCount(EItemType ItemType) const
{
    return Inventory.GetItemCount(ItemType);
}

int32 UMassDspPlayerInventoryComponent::GetTotalItemCount() const
{
    return Inventory.GetTotalItemCount();
}

int32 UMassDspPlayerInventoryComponent::GetCapacity() const
{
    return Inventory.GetCapacity();
}

int32 UMassDspPlayerInventoryComponent::GetFreeCapacity() const
{
    return Inventory.GetFreeCapacity();
}

void UMassDspPlayerInventoryComponent::GetActiveEntries(TArray<FInventoryEntryView>& OutEntries) const
{
    Inventory.GetActiveEntries(OutEntries);
}

void UMassDspPlayerInventoryComponent::GetActiveItems(TArray<EItemType>& OutItems) const
{
    Inventory.GetActiveItems(OutItems);
}