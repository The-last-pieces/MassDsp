#pragma once

#include "CoreMinimal.h"
#include "GameConst.h"
#include "Logistics/MassDspLogisticsTypes.h"
#include "MassEntityTypes.h"

/**
 * 无人机运行时数据（全 POD，对标 FBeltItemCache）
 *
 * 存储在 UMassDspLogisticsSubsystem::DronePool（TSparseArray<FDroneData>）中。
 * 位置通过三次贝塞尔曲线（P0-P3）每帧插值后批量写入 DroneISM。
 * 不使用 Mass Entity / Mass Fragment，无 Actor，无虚调用。
 */
struct MASSDSP_API FDroneData
{
    //  句柄版本（TSparseArray 回收再用时自增，防止悬空句柄） 
    int32 Generation = 0;

    //  状态机 
    ELogisticsDeviceState State       = ELogisticsDeviceState::Idle;
    int32                 CurrentTaskId = -1; ///< -1 = 空闲（TSparseArray 下标）

    //  贝塞尔飞行曲线（State 变化时由 InitDeviceForTask 重新生成） 
    // P0=出发点, P1=控制点1, P2=控制点2, P3=目标点
    // 弧形高度由 FGameConst::DroneFlightArcHeight 决定
    FVector P0 = FVector::ZeroVector;
    FVector P1 = FVector::ZeroVector;
    FVector P2 = FVector::ZeroVector;
    FVector P3 = FVector::ZeroVector;

    float TotalFlightTime   = 0.f;  ///< 本段飞行总时间（秒）= 距离 / FlightSpeed
    float ElapsedTime       = 0.f;  ///< 本段已飞时间（秒）

    //  配置 
    float FlightSpeed       = FGameConst::DefaultDroneFlightSpeed; ///< cm/s
    float CooldownDuration  = 0.2f;  ///< 任务完成后冷却时间（秒）
    float CooldownRemaining = 0.f;

    //  任务关联 
    FMassEntityHandle PickupEntity;
    FMassEntityHandle DeliveryEntity;
    /** 取货点世界坐标（由策略 InitDeviceForTask 写入，避免 Tick 时反查 AllTasks） */
    FVector           PickupLocation   = FVector::ZeroVector;
    /** 交货点世界坐标（同上；OnDroneArrivedAtPickup 时用于生成飞往交货点的贝塞尔曲线） */
    FVector           DeliveryLocation = FVector::ZeroVector;

    //  携带物品 
    EItemType CarriedItemType = EItemType::None;
    int32     CarriedQuantity = 0;
    int32     CarryCapacity   = 10;

    //  归属塔（Invalid = 全局无归属，小车/火车模式） 
    FMassEntityHandle AffiliatedTowerEntity;
    /** 归属塔的世界坐标（创建时记录，用于 ReturningHome 贝塞尔目标点） */
    FVector           HomeLocation = FVector::ZeroVector;
    /** Idle 状态盘旋时的初始相位（黄金角分布，防止多架无人机完全重叠） */
    float             IdlePhaseOffset = 0.f;

    //  ISM 渲染索引（-1 = 尚未分配实例） 
    int32 ISMInstanceIndex = -1;

    //  工具方法 
    bool IsIdle()        const { return State == ELogisticsDeviceState::Idle; }
    bool IsReturning()   const { return State == ELogisticsDeviceState::ReturningHome; }
    bool IsBusy()        const { return !IsIdle() && !IsReturning() && State != ELogisticsDeviceState::Cooldown; }

    /** 计算三次贝塞尔插值位置（t  [0,1]） */
    FVector EvalBezier(float t) const
    {
        const float u  = 1.f - t;
        const float t2 = t * t;
        const float u2 = u * u;
        return (u2 * u) * P0
             + (3.f * u2 * t) * P1
             + (3.f * u  * t2) * P2
             + (t2  * t) * P3;
    }
};
