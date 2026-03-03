#pragma once

#include "CoreMinimal.h"
#include "GameConst.h"
#include "Logistics/MassDspLogisticsTypes.h"
#include "MassEntityTypes.h"

/**
 * 地面小车运行时数据（全 POD，对标 FBeltItemCache）
 *
 * 存储在 UMassDspLogisticsSubsystem::VehiclePool（TSparseArray<FVehicleData>）。
 * 路径航点数据另存于 VehiclePaths[PoolIndex]（不在此结构体内，保持 POD）。
 * 每帧按 CurrentWaypointIdx 线性插值后批量写入 VehicleISM。
 *
 * TODO[VEHICLE]: 寻路（NavMesh）+ 动态避障 在 UpdateDevices 中填充 VehiclePaths
 */
struct MASSDSP_API FVehicleData
{
    //  句柄版本 
    int32 Generation = 0;

    //  状态机 
    ELogisticsDeviceState State        = ELogisticsDeviceState::Idle;
    FGuid                 CurrentTaskId;

    //  当前位置与目标段 
    FVector CurrentLocation  = FVector::ZeroVector;
    FVector TargetLocation   = FVector::ZeroVector; ///< 当前路径段的下一个航点
    float   SegmentProgress  = 0.f;                 ///< 当前段完成进度 [0,1]
    float   SegmentLength    = 0.f;                 ///< 当前段距离（cm）

    //  配置 
    float MoveSpeed         = 800.f;  ///< cm/s
    float CooldownDuration  = 0.5f;
    float CooldownRemaining = 0.f;

    //  路径状态（路径本身存在 VehiclePaths[PoolIndex]） 
    int32 CurrentWaypointIdx = 0;
    int32 TotalWaypointCount = 0;

    //  任务关联 
    FMassEntityHandle PickupEntity;
    FMassEntityHandle DeliveryEntity;

    //  携带物品 
    EItemType CarriedItemType = EItemType::None;
    int32     CarriedQuantity = 0;
    int32     CarryCapacity   = 50;

    //  ISM 渲染索引 
    int32 ISMInstanceIndex = -1;

    bool IsIdle() const { return State == ELogisticsDeviceState::Idle; }
};
