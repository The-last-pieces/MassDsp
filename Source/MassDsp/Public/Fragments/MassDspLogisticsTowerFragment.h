#pragma once

#include "CoreMinimal.h"
#include "GameConst.h"
#include "MassEntityTypes.h"

#include "MassDspLogisticsTowerFragment.generated.h"

/**
 * 物流塔专用 Mass Fragment（全 POD，无 TArray）
 *
 * 物流塔继承自 AMassDspStorage，因此同时拥有：
 *   - FMassDspStorageFragment       物品缓冲（传送带 I/O 照常走带，免费）
 *   - FMassDspBuildingSlotsFragment  槽口（可保留用于传送带联动）
 *   - FMassDspLogisticsTowerFragment  本 Fragment，物流调度专属
 *
 * 所有动态列表（请求 ID、任务 ID、归属无人机句柄）存在子系统
 * UMassDspLogisticsSubsystem::TowerRuntimeData[EntityHandle] 中，
 * Fragment 只保留必要的 POD 触发字段。
 */
USTRUCT()
struct MASSDSP_API FMassDspLogisticsTowerFragment : public FMassFragment
{
    GENERATED_BODY()

    /** 服务覆盖半径（cm）：Processor 扫描此范围内的建筑并提交请求 */
    UPROPERTY()
    float CoverageRadius = FGameConst::DefaultLogisticsCoverageRadius;

    /**
     * 兜底轮询扫描间隔（秒）。
     * 事件推送（bDirty）优先；超过此间隔仍无活动则 Processor 主动扫描。
     */
    UPROPERTY()
    float ScanInterval = FGameConst::DefaultLogisticsScanInterval;

    /** 上次 Processor 兜底扫描的时间戳（世界时间，秒） */
    UPROPERTY()
    float LastScanTime = 0.f;

    /**
     * 脏标记：有新请求通过事件推送挂入时置 true。
     * UMassDspLogisticsSubsystem::Tick 收集所有 bDirty=true 的塔优先匹配，
     * 处理完毕后清零。
     * 混合策略：事件推送快速响应 + Processor 轮询兜底覆盖漏网请求。
     */
    UPROPERTY()
    bool bDirty = false;

    /**
     * 当前塔是否支持接受外部请求提交（蓝图/运行时可动态开关）。
     * false 时 SubmitXxxRequest 会跳过此塔的自动路由。
     */
    UPROPERTY()
    bool bAcceptsRequests = true;
};
