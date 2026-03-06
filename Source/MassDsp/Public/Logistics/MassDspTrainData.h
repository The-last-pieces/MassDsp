#pragma once

#include "CoreMinimal.h"
#include "GameConst.h"
#include "Logistics/MassDspLogisticsTypes.h"
#include "MassEntityTypes.h"

/**
 * 火车运行时数据（全 POD，对标 FBeltItemCache）
 *
 * 存储在 UMassDspLogisticsSubsystem::TrainPool（TSparseArray<FTrainData>）。
 * 轨道轨迹数据另存于 TrainTracks[PoolIndex]（不在此结构体内）。
 * 每帧按 DistanceAlongTrack 在轨道样条 LUT 中查表后写入 TrainISM。
 *
 * TODO[TRAIN]: 轨道区间调度（信号灯 / 闭塞区间） 在 UpdateDevices 中实现
 * TODO[TRAIN]: TrainTracks = 预烘焙贝塞尔 LUT，复用 FBeltTrajectory 模式
 */
struct MASSDSP_API FTrainData
{
    //  句柄版本 
    int32 Generation = 0;

    //  状态机 
    ELogisticsDeviceState State          = ELogisticsDeviceState::Idle;
    int32                 CurrentTaskId  = -1; ///< TSparseArray<FLogisticsTask> 下标，-1 = 无任务（与无人机保持一致）

    //  轨道运动 
    /** 归属轨道段 Index（对应 TrainTracks[TrackSegmentIndex] 轨迹数据），-1 = 未上轨 */
    int32 TrackSegmentIndex   = -1;
    float DistanceAlongTrack  = 0.f;  ///< 当前在轨道段上的行驶距离（cm）
    bool  bForwardDirection   = true; ///< 行驶方向

    //  配置 
    float MoveSpeed         = 3000.f; ///< cm/s
    float CooldownDuration  = 2.f;
    float CooldownRemaining = 0.f;

    //  任务关联 
    FMassEntityHandle PickupEntity;
    FMassEntityHandle DeliveryEntity;

    //  携带物品 
    EItemType CarriedItemType = EItemType::None;
    int32     CarriedQuantity = 0;
    int32     CarryCapacity   = 200;

    //  ISM 渲染索引 
    int32 ISMInstanceIndex = -1;

    bool IsIdle() const { return State == ELogisticsDeviceState::Idle; }
};
