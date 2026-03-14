#include "Subsystems/MassDspDebugStatsSubsystem.h"

#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "MassDspBeltTypes.h"
#include "Fragments/MassDspAssemblerFragment.h"
#include "Fragments/MassDspLogisticsTowerFragment.h"
#include "Fragments/MassDspMinerFragment.h"
#include "Fragments/MassDspStorageFragment.h"
#include "Fragments/MassDspWarehouseFragment.h"
#include "Subsystems/MassDspLogisticsSubsystem.h"
#include "Subsystems/MassDspManager.h"

namespace
{
constexpr int32 MaxTrackedItemTypes = 256;
constexpr int32 MaxTopDeltaItems = 4;
}

void UMassDspDebugStatsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    PreviousItemTotals.Init(0, MaxTrackedItemTypes);
    SmoothedItemDeltaRates.Init(0.f, MaxTrackedItemTypes);
    CachedSnapshot.TopItemDeltas.Reserve(MaxTopDeltaItems);
}

void UMassDspDebugStatsSubsystem::Deinitialize()
{
    CachedSnapshot = FMassDspDebugStatsSnapshot();
    PreviousItemTotals.Empty();
    SmoothedItemDeltaRates.Empty();
    CachedManager.Reset();
    CachedLogistics.Reset();
    bHasValidDeltaHistory = false;
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

    int32 BusiestTowerScore = -1;
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

        if (Type == EBuildingType::LogisticsTower)
        {
            // const FTowerDroneStatus TowerStatus = Logistics->QueryTowerDroneStatus(Entity);
            // const FMassDspLogisticsTowerFragment* Tower = EntityManager.GetFragmentDataPtr<FMassDspLogisticsTowerFragment>(Entity);
            // const FMassDspStorageFragment* Storage = EntityManager.GetFragmentDataPtr<FMassDspStorageFragment>(Entity);
            // const int32 Pending = TowerStatus.SupplyRequests + TowerStatus.DemandRequests;
            // const int32 Score = TowerStatus.ActiveTasks * 1000 + Pending * 100 + TowerStatus.Incoming * 10 + TowerStatus.OwnedDeployed;
            // if (Score > BusiestTowerScore)
            // {
            //     BusiestTowerScore = Score;
            //     const FString ModeText = Tower
            //         ? StaticEnum<ELogisticsTowerMode>()->GetDisplayNameTextByValue(static_cast<int64>(Tower->TowerMode)).ToString()
            //         : TEXT("未知");
            //     const FString ItemText = Tower && Tower->ItemType != EItemType::None
            //         ? StaticEnum<EItemType>()->GetDisplayNameTextByValue(static_cast<int64>(Tower->ItemType)).ToString()
            //         : TEXT("未配置");
            //     const FString InventoryText = Storage
            //         ? FString::Printf(TEXT("%d/%d"), Storage->InventoryCount, Storage->MaxInventory)
            //         : TEXT("-");
            //     Snapshot.BusiestTowerSummary = FString::Printf(
            //         TEXT("物流塔 [%s] 物品:%s | 任务:%d | 请求:%d | 来航:%d | 库存:%s"),
            //         *ModeText,
            //         *ItemText,
            //         TowerStatus.ActiveTasks,
            //         Pending,
            //         TowerStatus.Incoming,
            //         *InventoryText);
            // }
        }
    }

    Snapshot.TotalDrones = Logistics->GetTotalDroneCount();
    Snapshot.IdleDrones = Logistics->GetIdleDroneCount();
    Snapshot.PendingRequests = Logistics->GetPendingRequestCount();
    Snapshot.ActiveTasks = Logistics->GetActiveTaskCount();

    struct FSortableDelta
    {
        EItemType ItemType = EItemType::None;
        float DeltaPerSecond = 0.f;
        int32 CurrentCount = 0;
    };

    const float SmoothingAlpha = GetDeltaSmoothingAlpha(SampleDeltaTime);
    TArray<FSortableDelta> SortedDeltas;
    SortedDeltas.Reserve(MaxTrackedItemTypes);
    for (int32 ItemIndex = 0; ItemIndex < CurrentItemTotals.Num(); ++ItemIndex)
    {
        const int32 CurrentCount = CurrentItemTotals[ItemIndex];
        const int32 PreviousCount = PreviousItemTotals.IsValidIndex(ItemIndex) ? PreviousItemTotals[ItemIndex] : 0;
        const int32 DeltaCount = CurrentCount - PreviousCount;
        const EItemType ItemType = static_cast<EItemType>(ItemIndex);
        if (ItemType == EItemType::None) continue;

        const float InstantRate = static_cast<float>(DeltaCount) / SampleDeltaTime;
        if (SmoothedItemDeltaRates.IsValidIndex(ItemIndex))
        {
            if (!bHasValidDeltaHistory)
            {
                SmoothedItemDeltaRates[ItemIndex] = InstantRate;
            }
            else
            {
                SmoothedItemDeltaRates[ItemIndex] = FMath::Lerp(SmoothedItemDeltaRates[ItemIndex], InstantRate, SmoothingAlpha);
            }
        }

        const float SmoothedRate = SmoothedItemDeltaRates.IsValidIndex(ItemIndex) ? SmoothedItemDeltaRates[ItemIndex] : InstantRate;
        if (CurrentCount <= 0 && FMath::Abs(SmoothedRate) < 0.01f) continue;

        FSortableDelta Entry;
        Entry.ItemType = ItemType;
        Entry.CurrentCount = CurrentCount;
        Entry.DeltaPerSecond = SmoothedRate;
        SortedDeltas.Add(Entry);
    }

    SortedDeltas.Sort([](const FSortableDelta& A, const FSortableDelta& B)
    {
        return FMath::Abs(A.DeltaPerSecond) > FMath::Abs(B.DeltaPerSecond);
    });

    const int32 DeltaCount = FMath::Min(MaxTopDeltaItems, SortedDeltas.Num());
    Snapshot.TopItemDeltas.Reset(DeltaCount);
    for (int32 Index = 0; Index < DeltaCount; ++Index)
    {
        FMassDspItemDeltaStat Delta;
        Delta.ItemType = SortedDeltas[Index].ItemType;
        Delta.DeltaPerSecond = SortedDeltas[Index].DeltaPerSecond;
        Delta.CurrentCount = SortedDeltas[Index].CurrentCount;
        Snapshot.TopItemDeltas.Add(Delta);
    }

    Snapshot.BottleneckSummary = BuildBottleneckSummary(Snapshot);
    if (Snapshot.BusiestTowerSummary.IsEmpty())
    {
        Snapshot.BusiestTowerSummary = TEXT("暂无活跃物流塔");
    }

    PreviousItemTotals = MoveTemp(CurrentItemTotals);
    bHasValidDeltaHistory = true;
    CachedSnapshot = MoveTemp(Snapshot);
}

void UMassDspDebugStatsSubsystem::AccumulateItemCount(TArray<int32>& TotalsByItem, EItemType ItemType, int32 Quantity) const
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

float UMassDspDebugStatsSubsystem::GetDeltaSmoothingAlpha(float SampleDeltaTime) const
{
    const float Window = FMath::Max(DeltaSmoothingWindowSeconds, KINDA_SMALL_NUMBER);
    return FMath::Clamp(SampleDeltaTime / Window, 0.05f, 1.0f);
}