#pragma once

#include "CoreMinimal.h"
#include "GameConst.h"

#include "Subsystems/WorldSubsystem.h"
#include "MassEntityTemplate.h"
#include "MassEntityManager.h"
#include "MassDspBeltTypes.h"
#include "ProceduralMeshComponent.h"

#include "Containers/SparseArray.h"
#include "Components/InstancedStaticMeshComponent.h"

#include "MassDspManager.generated.h"

class UProceduralMeshComponent;
class AMassDspGameMode;
class AMassDspBuilding;
class UMassEntityConfigAsset;

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
    TMap<FBeltHandle, FBeltData> BeltEntityRegistry;

    TSparseArray<FBeltTrajectory> BeltTrajectories;

    // 已创建的建筑 Mass Entity 数量（在 CreateBuildingEntityInternal 中自增）
    int32 BuildingEntityCount = 0;

    TWeakObjectPtr<AMassDspGameMode> GameMode;

    // ISM 物品渲染池，按物品类型分组，一种物品一个 ISM 组件
    // 注意：只存储近处物品（距离 < NearDistanceThreshold），远处物品不放入 ISM
    UPROPERTY()
    TMap<EItemType, UInstancedStaticMeshComponent*> ItemISMPool;

    // 每帧（降频）重建的 Transform 缓存（仅近处物品），避免堆分配
    TMap<EItemType, TArray<FTransform>> CachedTransformsByType;
    
    TMap<EBuildingType, FStaticMeshInstanceVisualizationDescHandle> CachedBuildingMeshDesc;

    // Cache: EBuildingType => Archetype（避免每次重建，在 CreateBuildingEntityInternal / BatchSpawnBuildings 中懒初始化）
    TMap<EBuildingType, FMassArchetypeHandle> CachedBuildingArchetypes;

    // Transform 同步累计时间（~30fps）
    float SyncAccum = 0.f;

    // 最大渲染距离（cm）：超过此距离的传送带即使在视锥内也不渲染
    // 解决飞高时视锥裆盖大量传送带的问题，默认 150m
    float MaxRenderDistance = 50000.f;

protected:
    UPROPERTY()
    AActor* BeltsContainerActor;

    UPROPERTY()
    UProceduralMeshComponent* BeltProceduralMesh;

private:
    TWeakObjectPtr<AMassDspGameMode> TryGetGameMode();

    FBeltHandle CreateRuntimeBelt(const TFunction<void(USplineComponent*)>& InitSpline, EBeltType BeltType);

    // 新增：合并缓存
    struct FMergedBeltMeshData
    {
        TArray<FVector> Vertices;
        TArray<int32> Triangles;
        TArray<FVector> Normals;
        TArray<FVector2D> UVs;
        TArray<FProcMeshTangent> Tangents;
        TArray<FLinearColor> Colors; // R通道存Speed
    };

    static constexpr float C_Width = 110.0f;
    static constexpr float C_BeltThickness = 20.0f;
    static constexpr float C_UVScale = 100.0f;

    /**
      * 静态生成传送带网格
      * @param OutMesh
      * @param Spline           定义路径的样条线组件
      * @param Width            传送带宽度
      * @param Thickness        传送带厚度
      * @param UVScale          UV平铺比例 (通常设为 100.0，即 1米重复一次)
      * @param AngleThreshold   自适应细分角度阈值 (建议 5.0 度)
      */
    static void GenerateConveyorMesh(
        FMergedBeltMeshData& OutMesh,
        const USplineComponent* Spline,
        float Width = 200.0f,
        float Thickness = 20.0f,
        float UVScale = 100.0f,
        float AngleThreshold = 5.0f
    );

public:
    FBeltHandle CreateAndLinkBeltForSlot(FMassEntityHandle SBuilding, int32 StartSlotIndex, FMassEntityHandle EBuilding, int32 EndSlotIndex, EBeltType BeltType);

    void FlushBeltMesh();

    bool ProvideItemToBelt(FBeltHandle BeltHandle, const TFunction<EItemType()>& GetItemFunc);

    EItemType ConsumeItemFromBelt(FBeltHandle BeltHandle, const TFunction<bool(EItemType)>& ValidateItemFunc);

    // ISM 渲染：只把位于视锥体内且距离小于 MaxRenderDistance 的传送带物品放入 ISM
    // 平视：视锥剔除侧面/背面；飞高：距离上限截断覆盖面积，两者互补
    void UpdateAllBeltItemTransforms(const FConvexVolume& ViewFrustum, const FVector& CameraPos);

    // 新增：从蓝图类创建单个Building Entity（运行时动态创建）
    FMassEntityHandle SpawnBuildingFromClass(FMassCommandBuffer& CommandBuffer, TSubclassOf<AMassDspBuilding> BuildingClass, const FTransform& WorldTransform,
                                             EBuildingType BuildingType);

    // 新增：批量创建Building Entity（关卡初始化用）
    TArray<FMassEntityHandle> BatchSpawnBuildings(const TArray<FBuildingSpawnData>& SpawnDataList);

private:
    // 内部辅助方法：创建Building Entity的核心逻辑
    FMassEntityHandle CreateBuildingEntityInternal(FMassEntityManager& EntityManager, const FBuildingSpawnData& SpawnData);

    // 按需懒创建指定物品类型的 ISM 组件
    UInstancedStaticMeshComponent* GetOrCreateIsmForItemType(EItemType ItemType);

    TMap<EBeltType, FMergedBeltMeshData> PendingBeltMeshMap;

    TSet<EBeltType> BeltMaterializedSet; // 记录已生成网格的 BeltType，避免重复生成
};
