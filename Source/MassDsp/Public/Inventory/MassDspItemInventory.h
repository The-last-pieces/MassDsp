#pragma once

#include "CoreMinimal.h"
#include "GameConst.h"

#include "MassDspItemInventory.generated.h"

USTRUCT(BlueprintType)
struct MASSDSP_API FInventoryEntryView
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "MassDsp|Inventory")
    EItemType ItemType = EItemType::None;

    UPROPERTY(BlueprintReadOnly, Category = "MassDsp|Inventory")
    int32 Quantity = 0;
};

/**
 * 通用聚合库存：按 ItemType 直接计数，并维护活跃物品列表，
 * 查询/增删为 O(1)，UI 只需遍历非空项。
 */
USTRUCT(BlueprintType)
struct MASSDSP_API FCompactItemInventory
{
    GENERATED_BODY()

public:
    static constexpr int32 ItemTypeCapacity = 256;

    void Initialize(int32 InMaxTotalItems)
    {
        MaxTotalItems = FMath::Max(0, InMaxTotalItems);
        if (Quantities.Num() != ItemTypeCapacity)
        {
            Quantities.Init(0, ItemTypeCapacity);
        }
        else
        {
            for (int32& Quantity : Quantities)
            {
                Quantity = 0;
            }
        }

        if (ActiveIndices.Num() != ItemTypeCapacity)
        {
            ActiveIndices.Init(INDEX_NONE, ItemTypeCapacity);
        }
        else
        {
            for (int32& Index : ActiveIndices)
            {
                Index = INDEX_NONE;
            }
        }

        ActiveItems.Reset();
        TotalItemCount = 0;
    }

    int32 GetCapacity() const { return MaxTotalItems; }
    int32 GetTotalItemCount() const { return TotalItemCount; }
    int32 GetFreeCapacity() const { return FMath::Max(0, MaxTotalItems - TotalItemCount); }

    int32 GetItemCount(EItemType ItemType) const
    {
        const int32 ItemIndex = ToItemIndex(ItemType);
        return Quantities.IsValidIndex(ItemIndex) ? Quantities[ItemIndex] : 0;
    }

    int32 TryAddItem(EItemType ItemType, int32 Quantity)
    {
        if (ItemType == EItemType::None || Quantity <= 0) return 0;

        const int32 ItemIndex = ToItemIndex(ItemType);
        if (!Quantities.IsValidIndex(ItemIndex)) return 0;

        const int32 Accepted = FMath::Min(Quantity, GetFreeCapacity());
        if (Accepted <= 0) return 0;

        if (Quantities[ItemIndex] == 0)
        {
            ActiveIndices[ItemIndex] = ActiveItems.Add(ItemType);
        }

        Quantities[ItemIndex] += Accepted;
        TotalItemCount += Accepted;
        return Accepted;
    }

    int32 TryRemoveItem(EItemType ItemType, int32 Quantity)
    {
        if (ItemType == EItemType::None || Quantity <= 0) return 0;

        const int32 ItemIndex = ToItemIndex(ItemType);
        if (!Quantities.IsValidIndex(ItemIndex)) return 0;

        const int32 Removed = FMath::Min(Quantity, Quantities[ItemIndex]);
        if (Removed <= 0) return 0;

        Quantities[ItemIndex] -= Removed;
        TotalItemCount -= Removed;

        if (Quantities[ItemIndex] == 0)
        {
            RemoveActiveItem(ItemType, ItemIndex);
        }

        return Removed;
    }

    void GetActiveEntries(TArray<FInventoryEntryView>& OutEntries) const
    {
        OutEntries.Reset(ActiveItems.Num());
        for (EItemType ItemType : ActiveItems)
        {
            const int32 Quantity = GetItemCount(ItemType);
            if (Quantity <= 0) continue;

            FInventoryEntryView Entry;
            Entry.ItemType = ItemType;
            Entry.Quantity = Quantity;
            OutEntries.Add(Entry);
        }
    }

    void GetActiveItems(TArray<EItemType>& OutItems) const
    {
        OutItems = ActiveItems;
    }

private:
    static int32 ToItemIndex(EItemType ItemType)
    {
        return static_cast<int32>(static_cast<uint8>(ItemType));
    }

    void RemoveActiveItem(EItemType ItemType, int32 ItemIndex)
    {
        const int32 ActiveIndex = ActiveIndices.IsValidIndex(ItemIndex) ? ActiveIndices[ItemIndex] : INDEX_NONE;
        if (!ActiveItems.IsValidIndex(ActiveIndex))
        {
            ActiveIndices[ItemIndex] = INDEX_NONE;
            return;
        }

        const int32 LastIndex = ActiveItems.Num() - 1;
        const EItemType LastItem = ActiveItems[LastIndex];
        ActiveItems.RemoveAtSwap(ActiveIndex, 1, EAllowShrinking::No);
        ActiveIndices[ItemIndex] = INDEX_NONE;

        if (ActiveIndex != LastIndex)
        {
            const int32 LastItemIndex = ToItemIndex(LastItem);
            if (ActiveIndices.IsValidIndex(LastItemIndex))
            {
                ActiveIndices[LastItemIndex] = ActiveIndex;
            }
        }
    }

    UPROPERTY()
    int32 MaxTotalItems = 0;

    UPROPERTY()
    int32 TotalItemCount = 0;

    UPROPERTY()
    TArray<int32> Quantities;

    UPROPERTY()
    TArray<EItemType> ActiveItems;

    UPROPERTY()
    TArray<int32> ActiveIndices;
};