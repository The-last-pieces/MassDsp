#pragma once

#include "CoreMinimal.h"
#include "GameConst.h"
#include "MassEntityTypes.h"

// ============================================================
//  物流系统：设备类型 / 请求类型 / 任务状态 枚举
// ============================================================

/** 物流设备类型 */
UENUM()
enum class ELogisticsDeviceType : uint8
{
    Drone = 0, ///< 无人机（纯 ISM 批量渲染，10w+ 量级）
    GroundVehicle = 1, ///< 地面小车（寻路+避障，1k+ 量级）
    Train = 2, ///< 火车（轨道区间调度，1k+ 量级）
};

/** 物流请求类型 */
enum class ELogisticsRequestType : uint8
{
    Supply = 0, ///< 供货请求：某建筑有多余库存，需要输出
    Demand = 1, ///< 需货请求：某建筑库存不足，需要补入
};

/** 物流请求优先级 */
enum class ELogisticsRequestPriority : uint8
{
    Low = 0,
    Normal = 1,
    High = 2,
    Critical = 3,
};

/** 物流任务状态 */
enum class ELogisticsTaskState : uint8
{
    Pending = 0, ///< 等待分配设备
    Dispatched = 1, ///< 已下发给设备，等待出发
    InTransit_Pickup = 2, ///< 设备前往取货点
    InTransit_Deliver = 3, ///< 设备前往交货点
    Completed = 4, ///< 任务完成，物品已转移
    Failed = 5, ///< 任务失败（超时/路径不通等）
    Cancelled = 6, ///< 被主动取消
};

/** 设备运动状态机 */
enum class ELogisticsDeviceState : uint8
{
    Idle = 0,            ///< 空闲，停在归属塔等待任务
    MovingToPickup = 1,  ///< 前往取货点
    AtPickup = 2,        ///< 到达取货点（正在装货）
    MovingToDeliver = 3, ///< 前往交货点
    AtDeliver = 4,       ///< 到达交货点（正在卸货）
    Cooldown = 5,        ///< 冷却中（任务完成后短暂等待）
    ReturningHome = 6,   ///< 返回归属塔途中（冷却后自动触发，到家后变 Idle）
};

// ============================================================
//  句柄（完全对标 FBeltHandle）
// ============================================================

struct FDroneHandle
{
    int32 Index = -1;
    int32 Generation = 0;

    bool IsValid() const { return Index >= 0; }

    bool operator==(const FDroneHandle& Other) const
    {
        return Index == Other.Index && Generation == Other.Generation;
    }

    friend uint32 GetTypeHash(const FDroneHandle& H)
    {
        return HashCombine(GetTypeHash(H.Index), GetTypeHash(H.Generation));
    }
};

struct FVehicleHandle
{
    int32 Index = -1;
    int32 Generation = 0;

    bool IsValid() const { return Index >= 0; }

    bool operator==(const FVehicleHandle& Other) const
    {
        return Index == Other.Index && Generation == Other.Generation;
    }

    friend uint32 GetTypeHash(const FVehicleHandle& H)
    {
        return HashCombine(GetTypeHash(H.Index), GetTypeHash(H.Generation));
    }
};

struct FTrainHandle
{
    int32 Index = -1;
    int32 Generation = 0;

    bool IsValid() const { return Index >= 0; }

    bool operator==(const FTrainHandle& Other) const
    {
        return Index == Other.Index && Generation == Other.Generation;
    }

    friend uint32 GetTypeHash(const FTrainHandle& H)
    {
        return HashCombine(GetTypeHash(H.Index), GetTypeHash(H.Generation));
    }
};

// ============================================================
//  请求 / 任务 数据（存在子系统 TMap，不在任何 Fragment 内）
// ============================================================

/**
 * 物流请求（Supply 或 Demand）
 * 由建筑侧 Processor 或手动调用 UMassDspLogisticsSubsystem::SubmitXxxRequest 创建。
 * 生命周期：Pending  匹配配对  合并进 FLogisticsTask  Cancelled/Expired
 */
struct FLogisticsRequest
{
    FGuid RequestId;
    ELogisticsRequestType Type = ELogisticsRequestType::Supply;
    FMassEntityHandle SourceEntity; ///< 发起请求的建筑 Mass Entity
    EItemType ItemType = EItemType::None;
    int32 Quantity = 1;
    ELogisticsRequestPriority Priority = ELogisticsRequestPriority::Normal;
    /** 希望由哪个物流塔服务；Invalid  子系统自动路由到最近塔 */
    FMassEntityHandle PreferredTowerEntity;
    float RequestTime = 0.f; ///< 提交时的世界时间（Time.GetRealTimeSeconds 等）
    float ExpiryDuration = 30.f; ///< 超时时间（秒）
};

/**
 * 物流任务（Supply + Demand 配对后生成）
 * 持有一个设备实例（通过 DevicePoolIndex 索引），记录整个转运过程。
 */
struct FLogisticsTask
{
    FGuid TaskId;
    FGuid SupplyRequestId;
    FGuid DemandRequestId;
    ELogisticsDeviceType DeviceType = ELogisticsDeviceType::Drone;
    /** 对应设备 TSparseArray 的物理 Index */
    int32 DevicePoolIndex = -1;
    ELogisticsTaskState State = ELogisticsTaskState::Pending;
    FVector PickupLocation = FVector::ZeroVector;
    FVector DeliveryLocation = FVector::ZeroVector;
    FMassEntityHandle PickupEntity; ///< 取货建筑
    FMassEntityHandle DeliveryEntity; ///< 送货建筑
    int32 TransferQuantity = 0;
    float CreatedTime = 0.f;
};

/**
 * 物流塔运行时辅助数据（存在子系统 TMap<FMassEntityHandle, FLogisticsTowerRuntimeData>）
 * 包含所有动态列表，不放入 Mass Fragment（Fragment 需全 POD）。
 */
struct FLogisticsTowerRuntimeData
{
    /** 等待配对的请求 ID 列表（Supply + Demand 混存，通过 AllRequests[id].Type 区分） */
    TArray<FGuid> PendingRequestIds;
    /** 当前进行中的任务 ID 列表 */
    TArray<FGuid> ActiveTaskIds;
    /** 归属此塔的无人机句柄列表（小车/火车走全局设备池，无需归属绑定） */
    TArray<FDroneHandle> AffiliatedDroneHandles;
};

/**
 * Widget 查询结果：物流塔无人机状态快照
 * O(归属机数 + 活跃任务数)，供 UI RefreshWidgets() 每帧调用
 */
struct FTowerDroneStatus
{
    int32 OwnedDeployed = 0; ///< 归属本塔且正在执行任务（MovingToPickup/AtPickup/MovingToDeliver/AtDeliver）
    int32 OwnedResting  = 0; ///< 归属本塔且空闲 / 冷却（Idle / Cooldown）
    int32 Incoming      = 0; ///< DeliveryEntity == 本塔 且 State == InTransit_Deliver 的正在飞来无人机数
};
