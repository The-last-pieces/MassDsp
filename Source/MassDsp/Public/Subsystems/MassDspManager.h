#pragma once

#include "CoreMinimal.h"
#include "GameConst.h"

#include "Subsystems/WorldSubsystem.h"
#include "MassEntityTemplate.h"
#include "MassDspBeltTypes.h"

#include "Containers/Deque.h"
#include "Containers/SparseArray.h"

#include "MassDspManager.generated.h"

class AMassDspGameMode;
class AMassDspBuilding;
class UMassEntityConfigAsset;

struct FBeltEntityArray
{
    TDeque<FMassEntityHandle> Entities;
};

// Building实体生成数据
USTRUCT(BlueprintType)
struct FBuildingSpawnData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TSubclassOf<AMassDspBuilding> BuildingClass;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FTransform WorldTransform;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EBuildingType BuildingType = EBuildingType::None;

    FBuildingSpawnData() = default;

    FBuildingSpawnData(TSubclassOf<AMassDspBuilding> InClass, const FTransform& InTransform, EBuildingType InType)
        : BuildingClass(InClass), WorldTransform(InTransform), BuildingType(InType)
    {
    }
};

UCLASS()
class MASSDSP_API UMassDspManager : public UWorldSubsystem
{
    GENERATED_BODY()

protected:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

public:
    TMap<FBeltHandle, FBeltEntityArray> BeltEntityRegistry;

    TSparseArray<FBeltTrajectory> BeltTrajectories;

    TWeakObjectPtr<AMassDspGameMode> GameMode;

protected:
    UPROPERTY()
    AActor* BeltsContainerActor;

private:
    TWeakObjectPtr<AMassDspGameMode> TryGetGameMode();

    FBeltHandle CreateRuntimeBelt(const TFunction<void(USplineComponent*)>& InitSpline, UStaticMesh* BeltMesh, int32 SegmentsPerSection);

public:
    FBeltHandle CreateAndLinkBeltForSlot(FMassEntityHandle SBuilding, int32 StartSlotIndex, FMassEntityHandle EBuilding, int32 EndSlotIndex, UStaticMesh* BeltMesh);

    bool ProvideItemToBelt(FMassCommandBuffer& CommandBuffer, FBeltHandle BeltHandle, const TFunction<EItemType()>& GetItemFunc);

    EItemType ConsumeItemFromBelt(FMassCommandBuffer& CommandBuffer, FBeltHandle BeltHandle, const TFunction<bool(EItemType)>& ValidateItemFunc);

    // 新增：从蓝图类创建单个Building Entity（运行时动态创建）
    FMassEntityHandle SpawnBuildingFromClass(FMassCommandBuffer& CommandBuffer, TSubclassOf<AMassDspBuilding> BuildingClass, const FTransform& WorldTransform,
                                             EBuildingType BuildingType);

    // 新增：批量创建Building Entity（关卡初始化用）
    TArray<FMassEntityHandle> BatchSpawnBuildings(const TArray<FBuildingSpawnData>& SpawnDataList);

private:
    // 内部辅助方法：创建Building Entity的核心逻辑
    FMassEntityHandle CreateBuildingEntityInternal(FMassEntityManager& EntityManager, const FBuildingSpawnData& SpawnData);
};
