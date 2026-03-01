#pragma once

#include "CoreMinimal.h"
#include "GameConst.h"

#include "Subsystems/WorldSubsystem.h"
#include "MassEntityTemplate.h"
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

    TWeakObjectPtr<AMassDspGameMode> GameMode;

    // ISM 物品渲染池，按物品类型分组，一种物品一个 ISM 组件
    UPROPERTY()
    TMap<EItemType, UInstancedStaticMeshComponent*> ItemISMPool;

    // 每帧（降频）重建的 Transform 缓存，避免堆分配
    TMap<EItemType, TArray<FTransform>> CachedTransformsByType;

    // 降频累计时间（~30fps 更新 Transform）
    float SyncAccum = 0.f;

protected:
    UPROPERTY()
    AActor* BeltsContainerActor;

    UPROPERTY()
    UProceduralMeshComponent* BeltProceduralMesh;

private:
    TWeakObjectPtr<AMassDspGameMode> TryGetGameMode();

    FBeltHandle CreateRuntimeBelt(const TFunction<void(USplineComponent*)>& InitSpline, UMaterialInterface* Material, int32 SegmentsPerSection);

    /**
      * 静态生成传送带网格
      * @param TargetMesh       要填充数据的 ProceduralMesh 组件
      * @param Spline           定义路径的样条线组件
      * @param Material         要应用的材质 (支持前面做的动态材质)
      * @param Width            传送带宽度
      * @param Thickness        传送带厚度
      * @param UVScale          UV平铺比例 (通常设为 100.0，即 1米重复一次)
      * @param AngleThreshold   自适应细分角度阈值 (建议 5.0 度)
      * @param BeltSpeed
      */
    void GenerateConveyorMesh(
        UProceduralMeshComponent* TargetMesh,
        const USplineComponent* Spline,
        UMaterialInterface* Material,
        float Width = 200.0f,
        float Thickness = 20.0f,
        float UVScale = 100.0f,
        float AngleThreshold = 5.0f,
        float BeltSpeed = 1000
    );

public:
    FBeltHandle CreateAndLinkBeltForSlot(FMassEntityHandle SBuilding, int32 StartSlotIndex, FMassEntityHandle EBuilding, int32 EndSlotIndex, UMaterialInterface* Material);

    void FlushBeltMesh(UMaterialInterface* Material) const;

    bool ProvideItemToBelt(FBeltHandle BeltHandle, const TFunction<EItemType()>& GetItemFunc);

    EItemType ConsumeItemFromBelt(FBeltHandle BeltHandle, const TFunction<bool(EItemType)>& ValidateItemFunc);

    // ISM 渲染：按物品类型分池，每帧（降频至~30fps）批量更新 Transform，绕开 Mass 渲染层
    void UpdateAllBeltItemTransforms();

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

    FMergedBeltMeshData PendingBeltMesh;
};
