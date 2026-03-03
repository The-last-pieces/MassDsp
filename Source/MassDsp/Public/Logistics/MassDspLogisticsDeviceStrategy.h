#pragma once

#include "CoreMinimal.h"
#include "Logistics/MassDspLogisticsTypes.h"

/**
 * 物流设备分派策略接口（非 UObject，纯 C++ 抽象）
 *
 * 这是整个物流系统中**唯一的虚接口层**，仅在以下两个时机调用：
 *   1. 任务匹配成功后，选择最优设备（SelectBestDeviceIndex）
 *   2. 选定设备后，将任务数据写入对应 POD 结构（InitDeviceForTask）
 *
 * 热路径（每帧数万次设备状态机推进）完全不经过此接口，零虚调用。
 *
 * 扩展方式：
 *   - 实现子类并调用 UMassDspLogisticsSubsystem::RegisterDispatchStrategy 注册
 *   - 本文件无需修改
 *
 * 当前提供三个占位声明，后续各设备模块各自实现：
 *   - FDroneDispatchStrategy
 *   - FGroundVehicleDispatchStrategy
 *   - FTrainDispatchStrategy
 */
struct MASSDSP_API FLogisticsDeviceDispatchStrategy
{
    virtual ~FLogisticsDeviceDispatchStrategy() = default;

    /**
     * 从候选空闲设备池中选出最优设备的 PoolIndex。
     *
     * @param IdlePoolIndices  当前空闲设备在 TSparseArray 中的物理 Index 列表
     * @param Task             待分派的物流任务（含取货/送货位置）
     * @return                 选中设备的 PoolIndex；-1 = 无可用设备
     *
     * 实现时可按距离（取货点最近优先）、容量、冷却时间等策略排序。
     * 单次任务分配调用一次，不在帧循环中。
     */
    virtual int32 SelectBestDeviceIndex(
        const TArray<int32>&  IdlePoolIndices,
        const FLogisticsTask& Task) const = 0;

    /**
     * 将任务数据写入选定设备的 POD 结构。
     *
     * @param DeviceDataPtr  指向 FDroneData / FVehicleData / FTrainData 的裸指针
     * @param Task           当前任务（含起终点位置等信息）
     *
     * 实现示例（无人机）：
     *   auto* Drone = static_cast<FDroneData*>(DeviceDataPtr);
     *   Drone->P0 = Drone->CurrentPos; // 当前位置
     *   Drone->P3 = Task.PickupLocation;
     *   Drone->P1 = P0 + FVector(0,0, FGameConst::DroneFlightArcHeight);
     *   Drone->P2 = P3 + FVector(0,0, FGameConst::DroneFlightArcHeight);
     *   Drone->TotalFlightTime = FVector::Dist(P0, P3) / Drone->FlightSpeed;
     *   Drone->ElapsedTime = 0.f;
     *   Drone->State = ELogisticsDeviceState::MovingToPickup;
     *   Drone->CurrentTaskId = Task.TaskId;
     */
    virtual void InitDeviceForTask(
        void*                 DeviceDataPtr,
        const FLogisticsTask& Task) const = 0;

    /**
     * 将设备重置为 Idle 状态（任务取消/失败时调用）。
     *
     * @param DeviceDataPtr  同 InitDeviceForTask，裸指针
     */
    virtual void ResetDevice(void* DeviceDataPtr) const = 0;

    /**
     * 返回设备当前世界位置（供 SelectBestDeviceIndex 就近判断使用）。
     *
     * @param DeviceDataPtr  同上
     */
    virtual FVector GetDeviceLocation(const void* DeviceDataPtr) const = 0;
};

//  占位声明（后续各设备模块实现，本次不填充逻辑） 

/**
 * 无人机分派策略（待实现）
 * TODO[DRONE]: 按取货点距离最近空闲无人机优先；生成贝塞尔控制点
 */
// struct FDroneDispatchStrategy : FLogisticsDeviceDispatchStrategy { ... };

/**
 * 地面小车分派策略（待实现）
 * TODO[VEHICLE]: 按 NavMesh 路径长度排序；填充 VehiclePaths[PoolIndex]
 */
// struct FGroundVehicleDispatchStrategy : FLogisticsDeviceDispatchStrategy { ... };

/**
 * 火车分派策略（待实现）
 * TODO[TRAIN]: 按轨道区间占用状态调度；分配 TrackSegmentIndex
 */
// struct FTrainDispatchStrategy : FLogisticsDeviceDispatchStrategy { ... };
