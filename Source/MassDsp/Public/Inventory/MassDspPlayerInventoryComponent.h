#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Inventory/MassDspItemInventory.h"

#include "MassDspPlayerInventoryComponent.generated.h"

UCLASS(ClassGroup = (MassDsp), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class MASSDSP_API UMassDspPlayerInventoryComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UMassDspPlayerInventoryComponent();

    virtual void BeginPlay() override;

    int32 AddItem(EItemType ItemType, int32 Quantity);
    int32 RemoveItem(EItemType ItemType, int32 Quantity);
    int32 GetItemCount(EItemType ItemType) const;
    int32 GetTotalItemCount() const;
    int32 GetCapacity() const;
    int32 GetFreeCapacity() const;
    void GetActiveEntries(TArray<FInventoryEntryView>& OutEntries) const;
    void GetActiveItems(TArray<EItemType>& OutItems) const;
    void RestoreInventorySnapshot(int32 InMaxInventoryItems, const TArray<FInventoryEntryView>& Entries);

    const FCompactItemInventory& GetInventory() const { return Inventory; }

protected:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MassDsp|Inventory", meta = (ClampMin = "1"))
    int32 MaxInventoryItems = 2000;

private:
    UPROPERTY(VisibleAnywhere, Category = "MassDsp|Inventory")
    FCompactItemInventory Inventory;
};
