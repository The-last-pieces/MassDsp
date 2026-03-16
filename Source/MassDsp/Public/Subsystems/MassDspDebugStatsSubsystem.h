#pragma once

#include "CoreMinimal.h"
#include "GameConst.h"
#include "HAL/CriticalSection.h"

#include "MassDspDebugStatsSubsystem.generated.h"

class UMassDspManager;
class UMassDspLogisticsSubsystem;

USTRUCT(BlueprintType)
struct MASSDSP_API FMassDspItemDeltaStat
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "MassDsp|Stats")
    EItemType ItemType = EItemType::None;

    UPROPERTY(BlueprintReadOnly, Category = "MassDsp|Stats")
    float ProductionPerSecond = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "MassDsp|Stats")
    float ConsumptionPerSecond = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "MassDsp|Stats")
    float NetGrowthPerSecond = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "MassDsp|Stats")
    int32 CurrentCount = 0;
};

USTRUCT(BlueprintType)
struct MASSDSP_API FMassDspDebugStatsSnapshot
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "MassDsp|Stats")
    int32 TotalBuildings = 0;

    UPROPERTY(BlueprintReadOnly, Category = "MassDsp|Stats")
    int32 MinerCount = 0;

    UPROPERTY(BlueprintReadOnly, Category = "MassDsp|Stats")
    int32 AssemblerCount = 0;

    UPROPERTY(BlueprintReadOnly, Category = "MassDsp|Stats")
    int32 StorageCount = 0;

    UPROPERTY(BlueprintReadOnly, Category = "MassDsp|Stats")
    int32 LogisticsTowerCount = 0;

    UPROPERTY(BlueprintReadOnly, Category = "MassDsp|Stats")
    int32 TotalBelts = 0;

    UPROPERTY(BlueprintReadOnly, Category = "MassDsp|Stats")
    int32 TotalBeltItems = 0;

    UPROPERTY(BlueprintReadOnly, Category = "MassDsp|Stats")
    int32 BlockedBelts = 0;

    UPROPERTY(BlueprintReadOnly, Category = "MassDsp|Stats")
    float EstimatedBeltThroughputPerSecond = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "MassDsp|Stats")
    int32 TotalDrones = 0;

    UPROPERTY(BlueprintReadOnly, Category = "MassDsp|Stats")
    int32 IdleDrones = 0;

    UPROPERTY(BlueprintReadOnly, Category = "MassDsp|Stats")
    int32 PendingRequests = 0;

    UPROPERTY(BlueprintReadOnly, Category = "MassDsp|Stats")
    int32 ActiveTasks = 0;

    UPROPERTY(BlueprintReadOnly, Category = "MassDsp|Stats")
    int32 StarvedAssemblers = 0;

    UPROPERTY(BlueprintReadOnly, Category = "MassDsp|Stats")
    int32 FullMinerNodes = 0;

    UPROPERTY(BlueprintReadOnly, Category = "MassDsp|Stats")
    int32 FullStorageNodes = 0;

    UPROPERTY(BlueprintReadOnly, Category = "MassDsp|Stats")
    int32 FullLogisticsTowers = 0;

    UPROPERTY(BlueprintReadOnly, Category = "MassDsp|Stats")
    FString BottleneckSummary;

    UPROPERTY(BlueprintReadOnly, Category = "MassDsp|Stats")
    TArray<FMassDspItemDeltaStat> ItemStats;

    UPROPERTY(BlueprintReadOnly, Category = "MassDsp|Stats")
    float SampleIntervalSeconds = 0.f;
};

UCLASS()
class MASSDSP_API UMassDspDebugStatsSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;

    virtual TStatId GetStatId() const override
    {
        RETURN_QUICK_DECLARE_CYCLE_STAT(UMassDspDebugStatsSubsystem, STATGROUP_Tickables);
    }

    const FMassDspDebugStatsSnapshot& GetSnapshot() const { return CachedSnapshot; }
    void ForceRefresh();
    void RecordProducedItem(EItemType ItemType, int32 Quantity = 1);
    void RecordConsumedItem(EItemType ItemType, int32 Quantity = 1);

protected:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }

private:
    void RebuildSnapshot(float SampleDeltaTime);
    void AccumulateItemCount(TArray<int32>& TotalsByItem, EItemType ItemType, int32 Quantity) const;
    void AccumulateItemEvent(TArray<int32>& TotalsByItem, EItemType ItemType, int32 Quantity) const;
    FString BuildBottleneckSummary(const FMassDspDebugStatsSnapshot& Snapshot) const;

    float GetDeltaSmoothingAlpha(float SampleDeltaTime) const;

    UPROPERTY(EditAnywhere, Category = "MassDsp|Stats", meta = (ClampMin = "0.1"))
    float UpdateInterval = 0.5f;

    float UpdateAccum = 0.f;
    float TimeSinceLastRefresh = 0.f;
    bool bHasValidDeltaHistory = false;

    FMassDspDebugStatsSnapshot CachedSnapshot;
    TArray<int32> PreviousItemTotals;
    TArray<float> SmoothedItemProductionRates;
    TArray<float> SmoothedItemConsumptionRates;
    TArray<float> SmoothedItemNetGrowthRates;
    TArray<int32> PendingProducedItemCounts;
    TArray<int32> PendingConsumedItemCounts;
    mutable FCriticalSection PendingItemEventMutex;

    TWeakObjectPtr<UMassDspManager> CachedManager;
    TWeakObjectPtr<UMassDspLogisticsSubsystem> CachedLogistics;

    UPROPERTY(EditAnywhere, Category = "MassDsp|Stats", meta = (ClampMin = "0.5"))
    float DeltaSmoothingWindowSeconds = 30.0f;

    UPROPERTY(EditAnywhere, Category = "MassDsp|Stats", meta = (ClampMin = "0"))
    int32 MaxDisplayedItemStats = 0;
};
