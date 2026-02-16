#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MassEntityTemplate.h"
#include "MassDspBeltTypes.h"

#include "Containers/Deque.h"
#include "Containers/SparseArray.h"

#include "MassDspManager.generated.h"

class UMassEntityConfigAsset;

struct FBeltEntityArray
{
    TDeque<FMassEntityHandle> Entities;
};

// 我们可以用一个 Wrapper Component 或 Actor 来持有 SplineComponent，
// 防止它们游离于 World 之外，被 GC 意外回收或者 Level Unload 问题。
// 简单起见，让这些 USplineComponent 附着于一个由 Manager 管理的 Hidden Actor 上。

UCLASS()
class MASSDSP_API UMassDspManager : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
    TObjectPtr<UMassEntityConfigAsset> DefaultItemConfig;

    // 存储车道实体的注册表
    // Key: ベルト句柄
    // Value: 实体队列
    // 使用 TSparseArray 或 TMap 都可以，考虑到 Handle 结构，直接用 TMap<FBeltHandle, ...>
    TMap<FBeltHandle, FBeltEntityArray> BeltEntityRegistry;

    // 存储所有传送带轨迹数据
    // 我们使用 SparseArray 来管理，这样 Index 可以直接做 Handle
    TSparseArray<FBeltTrajectory> BeltTrajectories;

    // 用于持有所有 Runtime 生成的 Spline 组件的 Actor
    UPROPERTY()
    class AActor* BeltsContainerActor;

public:
    FBeltHandle CreateRuntimeBelt(const TArray<FVector>& ControlPoints, UStaticMesh* BeltMesh, int32 SegmentsPerSection = 10);

    bool ProvideItemToBelt(FMassCommandBuffer& CommandBuffer, FBeltHandle BeltHandle, UMassEntityConfigAsset* ItemConfig);

    bool ConsumeItemFromBelt(FMassCommandBuffer& CommandBuffer, FBeltHandle BeltHandle);

    FMassEntityHandle RegisterBuildingEntity(class AMassDspBuilding* BuildingActor) const;
};
