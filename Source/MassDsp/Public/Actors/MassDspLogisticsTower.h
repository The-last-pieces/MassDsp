#pragma once

#include "CoreMinimal.h"
#include "Actors/MassDspStorage.h"
#include "Logistics/MassDspLogisticsTypes.h"

#include "MassDspLogisticsTower.generated.h"

/**
 * 物流塔 CDO（配置数据容器）
 *
 * 继承关系：AMassDspLogisticsTower  AMassDspStorage  AMassDspBuilding  AActor
 *
 * 【运行时不实例化 Actor】：与所有建筑基类一致，此类仅作为蓝图配置容器。
 * 运行时通过 UMassDspManager::SpawnBuildingFromClass / BatchSpawnBuildings
 * 创建纯 Mass Entity，由 GetDefault<AMassDspLogisticsTower>() 读取配置。
 *
 * 每个物流塔实体同时拥有三种 Fragment：
 *   - FMassDspStorageFragment             物品缓冲（由父类 AMassDspStorage 初始化）
 *   - FMassDspBuildingSlotsFragment        槽口（传送带联动，由 AMassDspBuilding 初始化）
 *   - FMassDspLogisticsTowerFragment       物流调度（本类新增）
 *
 * ISM 渲染：同其他建筑，由 UMassRepresentationSubsystem 管理，无独立 Mesh 组件。
 */
UCLASS(Blueprintable)
class MASSDSP_API AMassDspLogisticsTower : public AMassDspStorage
{
    GENERATED_BODY()

public:
    AMassDspLogisticsTower();

    //  AMassDspBuilding 接口 

    /** 除父类 Fragments（Storage + Slots），额外追加 LogisticsTowerFragment */
    virtual TArray<const UScriptStruct*> GetStaticStructs() const override;

    /** 初始化所有 Fragment（先调 Super 初始化 Storage/Slots，再初始化 Tower 字段） */
    virtual void InitFragmentForEntity(
        FMassEntityManager& EntityManager,
        FMassEntityHandle EntityHandle,
        const FTransform& WorldTransform) const override;

protected:
    virtual const UScriptStruct* GetStaticStructForFragment() const override;

public:
    //  物流塔配置 

    /**
     * 服务覆盖半径（cm）。
     * 指定范围内的 Storage / Assembler 将被 Processor 纳入请求扫描。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|LogisticsTower",
        meta = (ClampMin = "100.0", ClampMax = "50000.0"))
    float CoverageRadius = FGameConst::DefaultLogisticsCoverageRadius;

    /**
     * 兜底扫描间隔（秒）。
     * 事件推送优先（bDirty），超过此间隔仍无活动则 Processor 主动扫描周边建筑。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|LogisticsTower",
        meta = (ClampMin = "0.1"))
    float ScanInterval = FGameConst::DefaultLogisticsScanInterval;

    /**
     * 归属此塔的无人机上限数量。
     * 超出上限的创建请求将被路由到全局无人机池（无塔归属）。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|LogisticsTower",
        meta = (ClampMin = "0", ClampMax = "500"))
    int32 MaxAffiliatedDrones = 20;

    /**
     * 此塔支持使用的设备类型。
     * 任务分配时子系统只向已启用的设备类型查询空闲设备。
     * 留空 = 不接受任何设备（用于纯传送带仓库）。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|LogisticsTower")
    TArray<ELogisticsDeviceType> SupportedDeviceTypes = {ELogisticsDeviceType::Drone};

    /**
     * 库存触发阈值：Supply 请求触发比例（InventoryCount / MaxInventory > 此值时提交供货请求）。
     * 建议 0.8（80% 满触发外运）。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|LogisticsTower",
        meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float SupplyTriggerRatio = 0.8f;

    /**
     * 库存触发阈值：Demand 请求触发比例（InventoryCount / MaxInventory < 此值时提交需货请求）。
     * 建议 0.2（20% 空触发补货）。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|LogisticsTower",
        meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float DemandTriggerRatio = 0.2f;

    /**
     * 该塔作为消费方时希望不断补充的物品类型。
     * 设置后 Processor 每次扫描都会给该塔提交 Demand，
     * 无论塔自身库存是否有内容（解决空库不能自动转入问题）。
     * None = 纯供应方，不主动请求补货。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|LogisticsTower")
    EItemType DesiredItemType = EItemType::None;
};
