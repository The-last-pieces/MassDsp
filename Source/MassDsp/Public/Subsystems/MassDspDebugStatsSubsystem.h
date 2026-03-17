#pragma once

#include "Containers/Deque.h"
#include "CoreMinimal.h"
#include "GameConst.h"
#include "HAL/CriticalSection.h"

#include "MassDspDebugStatsSubsystem.generated.h"

class UMassDspManager;
class UMassDspLogisticsSubsystem;

struct FMassDspItemRateBucket
{
    int64 BucketIndex = 0;
    int32 Quantity = 0;
};

struct FMassDspItemRateWindow
{
    TDeque<FMassDspItemRateBucket> Buckets;
    int32 RollingQuantity = 0;

    void Reset()
    {
        Buckets.Empty();
        RollingQuantity = 0;
    }
};

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
struct MASSDSP_API FMassDspModuleProfileStat
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "MassDsp|Stats")
    FString ModuleName;

    UPROPERTY(BlueprintReadOnly, Category = "MassDsp|Stats")
    float TotalMilliseconds = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "MassDsp|Stats")
    float AverageMilliseconds = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "MassDsp|Stats")
    float FrameSharePercent = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "MassDsp|Stats")
    int32 SampleCount = 0;
};

struct FMassDspModuleProfileAccumulator
{
    double TotalSeconds = 0.0;
    int32 SampleCount = 0;

    void Reset()
    {
        TotalSeconds = 0.0;
        SampleCount = 0;
    }
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
    TArray<FMassDspModuleProfileStat> ModuleProfileStats;

    UPROPERTY(BlueprintReadOnly, Category = "MassDsp|Stats")
    float SampleIntervalSeconds = 0.f;
};

UCLASS()
class MASSDSP_API UMassDspDebugStatsSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    static constexpr int32 TrackedItemTypeCount = 256;

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;

    virtual TStatId GetStatId() const override
    {
        RETURN_QUICK_DECLARE_CYCLE_STAT(UMassDspDebugStatsSubsystem, STATGROUP_Tickables);
    }

    const FMassDspDebugStatsSnapshot& GetSnapshot() const { return CachedSnapshot; }
    void ForceRefresh();
    void RegisterStatsConsumer();
    void UnregisterStatsConsumer();
    bool IsStatsCollectionActive() const;
    void RecordProducedItem(EItemType ItemType, int32 Quantity = 1);
    void RecordConsumedItem(EItemType ItemType, int32 Quantity = 1);
    void RecordModuleProfileSample(FName ModuleName, double DurationSeconds);
    void ResetTheoreticalRates();
    void AccumulateTheoreticalRates(const TArray<float>& ProductionRates, const TArray<float>& ConsumptionRates);

protected:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }

private:
    void RebuildSnapshot(float SampleDeltaTime);
    void ResetSamplingState();
    void AccumulateItemCount(TArray<int32>& TotalsByItem, EItemType ItemType, int32 Quantity) const;
    void ConsumeModuleProfileSnapshot(TArray<FMassDspModuleProfileStat>& OutStats, float SampleIntervalSeconds);
    FString BuildBottleneckSummary(const FMassDspDebugStatsSnapshot& Snapshot) const;

    UPROPERTY(EditAnywhere, Category = "MassDsp|Stats", meta = (ClampMin = "0.1"))
    float UpdateInterval = 0.5f;

    UPROPERTY(EditAnywhere, Category = "MassDsp|Stats")
    bool bCollectStatsWithoutConsumer = false;

    float UpdateAccum = 0.f;
    float TimeSinceLastRefresh = 0.f;
    int32 ActiveStatsConsumerCount = 0;

    FMassDspDebugStatsSnapshot CachedSnapshot;
    TArray<FMassDspItemRateWindow> ItemProductionWindows;
    TArray<FMassDspItemRateWindow> ItemConsumptionWindows;
    mutable FCriticalSection PendingItemEventMutex;
    TArray<float> CurrentTheoreticalProductionRates;
    TArray<float> CurrentTheoreticalConsumptionRates;
    mutable FCriticalSection TheoreticalRateMutex;
    TMap<FName, FMassDspModuleProfileAccumulator> PendingModuleProfiles;
    mutable FCriticalSection ModuleProfileMutex;

    TWeakObjectPtr<UMassDspManager> CachedManager;
    TWeakObjectPtr<UMassDspLogisticsSubsystem> CachedLogistics;

    UPROPERTY(EditAnywhere, Category = "MassDsp|Stats", meta = (ClampMin = "0.5"))
    float DeltaSmoothingWindowSeconds = 60;

    UPROPERTY(EditAnywhere, Category = "MassDsp|Stats", meta = (ClampMin = "0.1"))
    float RateBucketDurationSeconds = 0.25f;

    UPROPERTY(EditAnywhere, Category = "MassDsp|Stats", meta = (ClampMin = "0"))
    int32 MaxDisplayedItemStats = 0;

    UPROPERTY(EditAnywhere, Category = "MassDsp|Stats", meta = (ClampMin = "0"))
    int32 MaxDisplayedModuleProfiles = 8;
};

class MASSDSP_API FMassDspScopedModuleProfile final
{
public:
    FMassDspScopedModuleProfile(UWorld* InWorld, FName InModuleName);
    ~FMassDspScopedModuleProfile();

private:
    TWeakObjectPtr<UMassDspDebugStatsSubsystem> StatsSubsystem;
    FName ModuleName;
    double StartTimeSeconds = 0.0;
};
