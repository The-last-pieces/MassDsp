#pragma once

#include "CoreMinimal.h"
#include "Actors/MassDspBuilding.h"
#include "Logistics/MassDspLogisticsTypes.h"

#include "MassDspLogisticsTower.generated.h"

/**
 * 物流塔 CDO（配置数据容器）
 *
 * 继承关系：AMassDspLogisticsTower  AMassDspBuilding  AActor
 *
 * 【运行时不实例化 Actor】：与所有建筑基类一致，此类仅作为蓝图配置容器。
 * 运行时通过 UMassDspManager::SpawnBuildingFromClass / BatchSpawnBuildings
 * 创建纯 Mass Entity，由 GetDefault<AMassDspLogisticsTower>() 读取配置。
 *
 * 每个物流塔实体同时拥有三种 Fragment：
 *   - FMassDspStorageFragment        单物品缓冲（本类初始化）
 *   - FMassDspBuildingSlotsFragment  槽口（传送带联动，由 AMassDspBuilding 初始化）
 *   - FMassDspLogisticsTowerFragment 物流调度（本类新增）
 *
 * ISM 渲染：同其他建筑，由 UMassRepresentationSubsystem 管理，无独立 Mesh 组件。
 */
UCLASS(Blueprintable)
class MASSDSP_API AMassDspLogisticsTower : public AMassDspBuilding
{
    GENERATED_BODY()

public:
    AMassDspLogisticsTower();

    //  AMassDspBuilding 接口 

    virtual TArray<const UScriptStruct*> GetStaticStructs() const override;

    virtual void InitFragmentForEntity(
        FMassEntityManager& EntityManager,
        FMassEntityHandle EntityHandle,
        const FTransform& WorldTransform) const override;

protected:
    virtual const UScriptStruct* GetStaticStructForFragment() const override;

public:
    /** 物流塔内部单物品缓存容量。仅用于塔自身的供需/传送带缓冲，不复用仓库泛型容器。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|LogisticsTower", meta = (ClampMin = "1", ClampMax = "10000"))
    int32 Capacity = 50;

    // ──────────────────────────────────────────────────────
    //  物流塔配置（DSP 行星内物流风格）
    // ──────────────────────────────────────────────────────

    /**
     * 运行模式：供应 / 需求 / 仓储
     *   Supply  — 库存 > RequestThreshold 时向全局队列提交 Supply
     *   Demand  — 库存 < RequestThreshold 时向全局队列提交 Demand
     *   Storage — 不参与无人机调度，仅作传送带中转缓冲
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|LogisticsTower")
    ELogisticsTowerMode TowerMode = ELogisticsTowerMode::Storage;

    /** 该塔处理的物品类型（None = 未配置，不参与调度） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|LogisticsTower")
    EItemType ItemType = EItemType::None;

    /**
     * 请求阈值（绝对数量）：
     *   Supply 模式 — 库存 > 此值时发货，发送量 = InventoryCount - RequestThreshold
     *   Demand 模式 — 库存 < 此值时补货，补货量 = RequestThreshold - InventoryCount
     *   Storage 模式 — 仅作参考上限，不触发请求
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|LogisticsTower",
        meta = (ClampMin = "0", ClampMax = "10000"))
    int32 RequestThreshold = 30;

    /** 单架无人机单次携带货物数量上限（覆盖全局 FGameConst::DroneCarryCapacity） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|LogisticsTower",
        meta = (ClampMin = "1", ClampMax = "200"))
    int32 DroneCargoCount = FGameConst::DroneCarryCapacity;

    /** 服务覆盖半径（cm），目前保留用于调试可视化，匹配不再依赖距离 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|LogisticsTower",
        meta = (ClampMin = "100.0", ClampMax = "50000.0"))
    float CoverageRadius = FGameConst::DefaultLogisticsCoverageRadius;

    /** 兜底轮询扫描间隔（秒）：事件推送优先，超时后 Processor 主动检查 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|LogisticsTower",
        meta = (ClampMin = "0.1"))
    float ScanInterval = FGameConst::DefaultLogisticsScanInterval;

    /** 归属此塔的默认无人机数量（用于初始化或手动放置后的批量 CreateDrone） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|LogisticsTower",
        meta = (ClampMin = "0", ClampMax = "500"))
    int32 MaxAffiliatedDrones = 100;
};
