#pragma once

#include "CoreMinimal.h"
#include "GameConst.h"
#include "MassEntityTypes.h"

#include "MassDspLogisticsTowerFragment.generated.h"

/**
 * 物流塔专用 Mass Fragment（全 POD，无 TArray）——戴森球计划行星内物流风格
 *
 * 物流塔继承自 AMassDspStorage，因此同时拥有：
 *   - FMassDspStorageFragment        物品缓冲（传送带 I/O 照常走带）
 *   - FMassDspBuildingSlotsFragment   槽口（传送带联动）
 *   - FMassDspLogisticsTowerFragment  本 Fragment，物流调度专属
 *
 * 运行模式（TowerMode）：
 *   Supply  — 库存 > RequestThreshold 时向全局队列提交 Supply 请求
 *   Demand  — 库存 < RequestThreshold 时向全局队列提交 Demand 请求
 *   Storage — 不参与无人机调度，仅缓冲传送带物品
 *
 * 全局匹配：SubSystem 跨所有塔收集 Supply/Demand，按 ItemType 分桶配对，
 *           无需显式指定协调塔（CoordinatorTowerEntity 已移除）。
 *
 * 所有动态列表存在 UMassDspLogisticsSubsystem::TowerRuntimeData 中，
 * Fragment 只保留 POD 触发字段，避免 Mass Chunk 膨胀。
 */
USTRUCT()
struct MASSDSP_API FMassDspLogisticsTowerFragment : public FMassFragment
{
    GENERATED_BODY()

    // ─── 运行模式 ────────────────────────────────────────────────────────────

    /** 物流运行模式：供应 / 需求 / 仓储 */
    UPROPERTY()
    ELogisticsTowerMode TowerMode = ELogisticsTowerMode::Storage;

    /** 该塔处理的物品类型（None = 未配置，不参与调度） */
    UPROPERTY()
    EItemType ItemType = EItemType::None;

    /**
     * 请求阈值（绝对数量）：
     *   Supply 模式 — 库存 > RequestThreshold 时发出供货请求，发货量 = InventoryCount - RequestThreshold
     *   Demand 模式 — 库存 < RequestThreshold 时发出补货请求，补货量 = RequestThreshold - InventoryCount
     *   Storage 模式 — 作为容量上限参考，不触发请求
     */
    UPROPERTY()
    int32 RequestThreshold = 30;

    /** 单架无人机单次携带货物数量上限（覆盖全局默认值 FGameConst::DroneCarryCapacity） */
    UPROPERTY()
    int32 DroneCargoCount = FGameConst::DroneCarryCapacity;

    // ─── 调度控制 ────────────────────────────────────────────────────────────

    /** 服务覆盖半径（cm）：当前保留，未来可用于限制塔间匹配距离 */
    UPROPERTY()
    float CoverageRadius = FGameConst::DefaultLogisticsCoverageRadius;

    /** 兜底轮询间隔（秒）：事件推送（bDirty）优先，超时则 Processor 主动触发扫描 */
    UPROPERTY()
    float ScanInterval = FGameConst::DefaultLogisticsScanInterval;

    /** 上次兜底扫描的时间戳（世界时间，秒） */
    UPROPERTY()
    float LastScanTime = 0.f;

    /** 脏标记：有新请求入队时置 true，SubSystem Tick Step2 优先处理后清零 */
    UPROPERTY()
    bool bDirty = false;

    /** 是否接受请求（运行时可动态关闭，置 false 后不再提交新请求） */
    UPROPERTY()
    bool bAcceptsRequests = true;
};
