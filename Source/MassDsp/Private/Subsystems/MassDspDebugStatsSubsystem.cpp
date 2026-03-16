#include "Subsystems/MassDspDebugStatsSubsystem.h"

#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "MassDspBeltTypes.h"
#include "Fragments/MassDspAssemblerFragment.h"
#include "Fragments/MassDspMinerFragment.h"
#include "Fragments/MassDspStorageFragment.h"
#include "Fragments/MassDspWarehouseFragment.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Inventory/MassDspPlayerInventoryComponent.h"
#include "Subsystems/MassDspLogisticsSubsystem.h"
#include "Subsystems/MassDspManager.h"

#include "Misc/ScopeLock.h"

namespace
{
    constexpr int32 MaxTrackedItemTypes = 256;
    constexpr int32 MaxRateBucketCount = 120;

    UMassDspPlayerInventoryComponent* GetPlayerInventoryComponent(UWorld* World)
    {
        if (!World)
        {
            return nullptr;
        }

        APlayerController* PlayerController = World->GetFirstPlayerController();
        APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
        return Pawn ? Pawn->FindComponentByClass<UMassDspPlayerInventoryComponent>() : nullptr;
    }

    int32 ResolveRateBucketCount(float WindowSeconds, float BucketDurationSeconds)
    {
        const float SafeBucketDuration = FMath::Max(BucketDurationSeconds, KINDA_SMALL_NUMBER);
        return FMath::Clamp(FMath::CeilToInt(WindowSeconds / SafeBucketDuration), 1, MaxRateBucketCount);
    }

    int64 ResolveCurrentBucketIndex(float WorldTimeSeconds, float BucketDurationSeconds)
    {
        const float SafeBucketDuration = FMath::Max(BucketDurationSeconds, KINDA_SMALL_NUMBER);
        return static_cast<int64>(FMath::FloorToDouble(WorldTimeSeconds / SafeBucketDuration));
    }

    void AdvanceRateWindow(FMassDspItemRateWindow& Window, int64 CurrentBucketIndex, int32 MaxBucketCount)
    {
        if (MaxBucketCount <= 0)
        {
            Window.Reset();
            return;
        }

        if (Window.Buckets.IsEmpty())
        {
            Window.Buckets.PushLast({CurrentBucketIndex, 0});
            return;
        }

        int64 LastBucketIndex = Window.Buckets[Window.Buckets.Num() - 1].BucketIndex;
        if (CurrentBucketIndex <= LastBucketIndex)
        {
            return;
        }

        if (CurrentBucketIndex - LastBucketIndex >= MaxBucketCount)
        {
            Window.Reset();
            Window.Buckets.PushLast({CurrentBucketIndex, 0});
            return;
        }

        while (LastBucketIndex < CurrentBucketIndex)
        {
            Window.Buckets.PushLast({++LastBucketIndex, 0});
            if (Window.Buckets.Num() > MaxBucketCount)
            {
                Window.RollingQuantity -= Window.Buckets[0].Quantity;
                Window.Buckets.PopFirst();
            }
        }
    }

    void AddSampleToRateWindow(FMassDspItemRateWindow& Window, int64 CurrentBucketIndex, int32 Quantity, int32 MaxBucketCount)
    {
        AdvanceRateWindow(Window, CurrentBucketIndex, MaxBucketCount);
        if (Quantity <= 0 || Window.Buckets.IsEmpty())
        {
            return;
        }

        Window.Buckets[Window.Buckets.Num() - 1].Quantity += Quantity;
        Window.RollingQuantity += Quantity;
    }

    float CalculateRatePerSecond(const FMassDspItemRateWindow& Window, float BucketDurationSeconds)
    {
        if (Window.Buckets.IsEmpty())
        {
            return 0.f;
        }

        const float DurationSeconds = FMath::Max(static_cast<float>(Window.Buckets.Num()) * BucketDurationSeconds, BucketDurationSeconds);
        return static_cast<float>(Window.RollingQuantity) / DurationSeconds;
    }
}

void UMassDspDebugStatsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    ItemProductionWindows.SetNum(MaxTrackedItemTypes);
    ItemConsumptionWindows.SetNum(MaxTrackedItemTypes);
    PendingProducedItemCounts.Init(0, MaxTrackedItemTypes);
    PendingConsumedItemCounts.Init(0, MaxTrackedItemTypes);
    CachedSnapshot.ItemStats.Reserve(MaxTrackedItemTypes);
}

void UMassDspDebugStatsSubsystem::Deinitialize()
{
    CachedSnapshot = FMassDspDebugStatsSnapshot();
    ItemProductionWindows.Empty();
    ItemConsumptionWindows.Empty();
    PendingProducedItemCounts.Empty();
    PendingConsumedItemCounts.Empty();
    CachedManager.Reset();
    CachedLogistics.Reset();
    Super::Deinitialize();
}

void UMassDspDebugStatsSubsystem::Tick(float DeltaTime)
{
    UpdateAccum += DeltaTime;
    TimeSinceLastRefresh += DeltaTime;
    if (UpdateAccum < UpdateInterval)
    {
        return;
    }

    const float SampleDeltaTime = FMath::Max(TimeSinceLastRefresh, KINDA_SMALL_NUMBER);
    UpdateAccum = 0.f;
    TimeSinceLastRefresh = 0.f;
    RebuildSnapshot(SampleDeltaTime);
}

void UMassDspDebugStatsSubsystem::ForceRefresh()
{
    RebuildSnapshot(FMath::Max(TimeSinceLastRefresh, UpdateInterval));
    UpdateAccum = 0.f;
    TimeSinceLastRefresh = 0.f;
}

void UMassDspDebugStatsSubsystem::RecordProducedItem(EItemType ItemType, int32 Quantity)
{
    FScopeLock ScopeLock(&PendingItemEventMutex);
    AccumulateItemEvent(PendingProducedItemCounts, ItemType, Quantity);
}

void UMassDspDebugStatsSubsystem::RecordConsumedItem(EItemType ItemType, int32 Quantity)
{
    FScopeLock ScopeLock(&PendingItemEventMutex);
    AccumulateItemEvent(PendingConsumedItemCounts, ItemType, Quantity);
}

void UMassDspDebugStatsSubsystem::RebuildSnapshot(float SampleDeltaTime)
{
    UWorld* World = GetWorld();
    UMassDspManager* Manager = CachedManager.IsValid() ? CachedManager.Get() : (World ? World->GetSubsystem<UMassDspManager>() : nullptr);
    UMassDspLogisticsSubsystem* Logistics = CachedLogistics.IsValid() ? CachedLogistics.Get() : (World ? World->GetSubsystem<UMassDspLogisticsSubsystem>() : nullptr);
    if (!Manager || !Logistics || !World)
    {
        return;
    }

    CachedManager = Manager;
    CachedLogistics = Logistics;

    UMassEntitySubsystem* EntitySubsystem = World->GetSubsystem<UMassEntitySubsystem>();
    if (!EntitySubsystem)
    {
        return;
    }

    FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();

    FMassDspDebugStatsSnapshot Snapshot;
    Snapshot.SampleIntervalSeconds = SampleDeltaTime;
    Snapshot.TotalBuildings = Manager->BuildingEntityCount;
    Snapshot.TotalBelts = Manager->BeltEntityRegistry.Num();

    TArray<int32> CurrentItemTotals;
    CurrentItemTotals.Init(0, MaxTrackedItemTypes);
    TArray<int32> ProducedItemCounts;
    TArray<int32> ConsumedItemCounts;
    ProducedItemCounts.Init(0, MaxTrackedItemTypes);
    ConsumedItemCounts.Init(0, MaxTrackedItemTypes);

    {
        FScopeLock ScopeLock(&PendingItemEventMutex);
        ProducedItemCounts = PendingProducedItemCounts;
        ConsumedItemCounts = PendingConsumedItemCounts;
        PendingProducedItemCounts.Init(0, MaxTrackedItemTypes);
        PendingConsumedItemCounts.Init(0, MaxTrackedItemTypes);
    }

    for (const TPair<FBeltHandle, FBeltData>& Pair : Manager->BeltEntityRegistry)
    {
        const FBeltData& BeltData = Pair.Value;
        Snapshot.TotalBeltItems += BeltData.ItemCache.Num();
        Snapshot.EstimatedBeltThroughputPerSecond += BeltData.BeltSpeed / FGameConst::ItemSpace;
        if (BeltData.BlockedCount > 0)
        {
            ++Snapshot.BlockedBelts;
        }

        for (const FBeltItemCache& Item : BeltData.ItemCache)
        {
            AccumulateItemCount(CurrentItemTotals, Item.ItemType, 1);
        }
    }

    for (const FMassEntityHandle& Entity : Manager->SpawnedBuildingEntities)
    {
        if (!EntityManager.IsEntityValid(Entity)) continue;

        const EBuildingType* BuildingType = Manager->BuildingEntityTypeRegistry.Find(Entity);
        const EBuildingType Type = BuildingType ? *BuildingType : EBuildingType::None;
        switch (Type)
        {
        case EBuildingType::Miner:
            ++Snapshot.MinerCount;
            break;
        case EBuildingType::Assembler:
            ++Snapshot.AssemblerCount;
            break;
        case EBuildingType::Storage:
            ++Snapshot.StorageCount;
            break;
        case EBuildingType::LogisticsTower:
            ++Snapshot.LogisticsTowerCount;
            break;
        default:
            break;
        }

        if (const FMassDspMinerFragment* Miner = EntityManager.GetFragmentDataPtr<FMassDspMinerFragment>(Entity))
        {
            if (Miner->InventoryCount >= Miner->MaxInventory)
            {
                ++Snapshot.FullMinerNodes;
            }
            AccumulateItemCount(CurrentItemTotals, Miner->StoredItemType, Miner->InventoryCount);
        }

        if (const FMassDspWarehouseFragment* Warehouse = EntityManager.GetFragmentDataPtr<FMassDspWarehouseFragment>(Entity))
        {
            if (Warehouse->GetInventoryCount() >= Warehouse->GetMaxInventory())
            {
                ++Snapshot.FullStorageNodes;
            }

            TArray<FInventoryEntryView> Entries;
            Warehouse->GetActiveEntries(Entries);
            for (const FInventoryEntryView& Entry : Entries)
            {
                AccumulateItemCount(CurrentItemTotals, Entry.ItemType, Entry.Quantity);
            }
        }

        if (const FMassDspStorageFragment* Storage = EntityManager.GetFragmentDataPtr<FMassDspStorageFragment>(Entity))
        {
            if (Storage->InventoryCount >= Storage->MaxInventory)
            {
                if (Type == EBuildingType::LogisticsTower)
                {
                    ++Snapshot.FullLogisticsTowers;
                }
                else
                {
                    ++Snapshot.FullStorageNodes;
                }
            }
            AccumulateItemCount(CurrentItemTotals, Storage->StoredItemType, Storage->InventoryCount);
        }

        if (const FMassDspAssemblerFragment* Assembler = EntityManager.GetFragmentDataPtr<FMassDspAssemblerFragment>(Entity))
        {
            if (!Assembler->bInputSatisfied)
            {
                ++Snapshot.StarvedAssemblers;
            }

            for (const FBufferEntry& Entry : Assembler->InputBuffers)
            {
                AccumulateItemCount(CurrentItemTotals, Entry.ItemType, Entry.Amount);
            }
            for (const FBufferEntry& Entry : Assembler->OutputBuffers)
            {
                AccumulateItemCount(CurrentItemTotals, Entry.ItemType, Entry.Amount);
            }
        }
    }

    Snapshot.TotalDrones = Logistics->GetTotalDroneCount();
    Snapshot.IdleDrones = Logistics->GetIdleDroneCount();
    Snapshot.PendingRequests = Logistics->GetPendingRequestCount();
    Snapshot.ActiveTasks = Logistics->GetActiveTaskCount();

    for (const FDroneData& Drone : Logistics->DronePool)
    {
        AccumulateItemCount(CurrentItemTotals, Drone.CarriedItemType, Drone.CarriedQuantity);
    }

    if (UMassDspPlayerInventoryComponent* PlayerInventory = GetPlayerInventoryComponent(World))
    {
        TArray<FInventoryEntryView> PlayerEntries;
        PlayerInventory->GetActiveEntries(PlayerEntries);
        for (const FInventoryEntryView& Entry : PlayerEntries)
        {
            AccumulateItemCount(CurrentItemTotals, Entry.ItemType, Entry.Quantity);
        }
    }

    struct FSortableDelta
    {
        EItemType ItemType = EItemType::None;
        float ProductionPerSecond = 0.f;
        float ConsumptionPerSecond = 0.f;
        float NetGrowthPerSecond = 0.f;
        int32 CurrentCount = 0;
    };

    const float BucketDurationSeconds = FMath::Max(RateBucketDurationSeconds, KINDA_SMALL_NUMBER);
    const int32 WindowBucketCount = ResolveRateBucketCount(DeltaSmoothingWindowSeconds, BucketDurationSeconds);
    const int64 CurrentBucketIndex = ResolveCurrentBucketIndex(World->GetTimeSeconds(), BucketDurationSeconds);
    TArray<FSortableDelta> SortedDeltas;
    SortedDeltas.Reserve(MaxTrackedItemTypes);
    for (int32 ItemIndex = 0; ItemIndex < CurrentItemTotals.Num(); ++ItemIndex)
    {
        const int32 CurrentCount = CurrentItemTotals[ItemIndex];
        const EItemType ItemType = static_cast<EItemType>(ItemIndex);
        if (ItemType == EItemType::None) continue;

        AddSampleToRateWindow(ItemProductionWindows[ItemIndex], CurrentBucketIndex, ProducedItemCounts[ItemIndex], WindowBucketCount);
        AddSampleToRateWindow(ItemConsumptionWindows[ItemIndex], CurrentBucketIndex, ConsumedItemCounts[ItemIndex], WindowBucketCount);

        const float SmoothedProductionRate = CalculateRatePerSecond(ItemProductionWindows[ItemIndex], BucketDurationSeconds);
        const float SmoothedConsumptionRate = CalculateRatePerSecond(ItemConsumptionWindows[ItemIndex], BucketDurationSeconds);
        const float SmoothedNetGrowthRate = SmoothedProductionRate - SmoothedConsumptionRate;
        if (CurrentCount <= 0
            && FMath::Abs(SmoothedProductionRate) < 0.01f
            && FMath::Abs(SmoothedConsumptionRate) < 0.01f
            && FMath::Abs(SmoothedNetGrowthRate) < 0.01f)
        {
            continue;
        }

        FSortableDelta Entry;
        Entry.ItemType = ItemType;
        Entry.CurrentCount = CurrentCount;
        Entry.ProductionPerSecond = SmoothedProductionRate;
        Entry.ConsumptionPerSecond = SmoothedConsumptionRate;
        Entry.NetGrowthPerSecond = SmoothedNetGrowthRate;
        SortedDeltas.Add(Entry);
    }

    SortedDeltas.Sort([](const FSortableDelta& A, const FSortableDelta& B)
    {
        if (!FMath::IsNearlyEqual(A.NetGrowthPerSecond, B.NetGrowthPerSecond))
        {
            return A.NetGrowthPerSecond > B.NetGrowthPerSecond;
        }
        return A.CurrentCount > B.CurrentCount;
    });

    const int32 DisplayCount = MaxDisplayedItemStats > 0
                                   ? FMath::Min(MaxDisplayedItemStats, SortedDeltas.Num())
                                   : SortedDeltas.Num();
    Snapshot.ItemStats.Reset(DisplayCount);
    for (int32 Index = 0; Index < DisplayCount; ++Index)
    {
        FMassDspItemDeltaStat Delta;
        Delta.ItemType = SortedDeltas[Index].ItemType;
        Delta.ProductionPerSecond = SortedDeltas[Index].ProductionPerSecond;
        Delta.ConsumptionPerSecond = SortedDeltas[Index].ConsumptionPerSecond;
        Delta.NetGrowthPerSecond = SortedDeltas[Index].NetGrowthPerSecond;
        Delta.CurrentCount = SortedDeltas[Index].CurrentCount;
        Snapshot.ItemStats.Add(Delta);
    }

    Snapshot.BottleneckSummary = BuildBottleneckSummary(Snapshot);

    CachedSnapshot = MoveTemp(Snapshot);
}

void UMassDspDebugStatsSubsystem::AccumulateItemCount(TArray<int32>& TotalsByItem, EItemType ItemType, int32 Quantity) const
{
    if (ItemType == EItemType::None || Quantity <= 0) return;

    const int32 ItemIndex = static_cast<int32>(static_cast<uint8>(ItemType));
    if (!TotalsByItem.IsValidIndex(ItemIndex)) return;
    TotalsByItem[ItemIndex] += Quantity;
}

void UMassDspDebugStatsSubsystem::AccumulateItemEvent(TArray<int32>& TotalsByItem, EItemType ItemType, int32 Quantity) const
{
    if (ItemType == EItemType::None || Quantity <= 0) return;

    const int32 ItemIndex = static_cast<int32>(static_cast<uint8>(ItemType));
    if (!TotalsByItem.IsValidIndex(ItemIndex)) return;
    TotalsByItem[ItemIndex] += Quantity;
}

FString UMassDspDebugStatsSubsystem::BuildBottleneckSummary(const FMassDspDebugStatsSnapshot& Snapshot) const
{
    if (Snapshot.PendingRequests > Snapshot.IdleDrones)
    {
        return FString::Printf(TEXT("物流积压: 待处理请求 %d, 空闲无人机 %d"), Snapshot.PendingRequests, Snapshot.IdleDrones);
    }
    if (Snapshot.BlockedBelts > 0)
    {
        return FString::Printf(TEXT("传送带阻塞: %d 条带正在堆积"), Snapshot.BlockedBelts);
    }
    if (Snapshot.StarvedAssemblers > 0)
    {
        return FString::Printf(TEXT("合成缺料: %d 台合成台等待输入"), Snapshot.StarvedAssemblers);
    }
    const int32 FullNodes = Snapshot.FullMinerNodes + Snapshot.FullStorageNodes + Snapshot.FullLogisticsTowers;
    if (FullNodes > 0)
    {
        return FString::Printf(TEXT("库存满载: %d 个节点需要疏通"), FullNodes);
    }
    return TEXT("系统运行稳定，没有明显瓶颈");
}
