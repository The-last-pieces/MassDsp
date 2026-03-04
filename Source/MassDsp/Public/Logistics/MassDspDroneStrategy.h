#pragma once

#include "CoreMinimal.h"
#include "Logistics/MassDspLogisticsDeviceStrategy.h"
#include "Logistics/MassDspDroneData.h"
#include "GameConst.h"

// FDroneDispatchStrategy 定义在自身 .h，避免引入 Subsystem 完整头（循环依赖风险）
// 使用者需要在 .cpp 中 #include "Subsystems/MassDspLogisticsSubsystem.h" 并自行传入 DronePool

/**
 * 无人机分派策略（FLogisticsDeviceDispatchStrategy 的具体实现）
 *
 * SelectBestDeviceIndex：在空闲无人机池中选取距取货点最近的无人机（欧氏距离 P3 近似当前位置）
 * InitDeviceForTask   ：生成飞往取货点的贝塞尔曲线（P0=当前位置, P3=取货位置, 两端各加高度弧）
 * ResetDevice         ：重置为 Idle（任务取消/失败回调）
 * GetDeviceLocation   ：返回 Drone.P3（贝塞尔末端，近似飞行当前位置）
 *
 * 用法（在 GameMode BeginPlay 或子系统初始化中注册一次）：
 *   UMassDspLogisticsSubsystem* Sub = World->GetSubsystem<UMassDspLogisticsSubsystem>();
 *   Sub->RegisterDispatchStrategy(ELogisticsDeviceType::Drone,
 *       MakeUnique<FDroneDispatchStrategy>(Sub));
 */
struct MASSDSP_API FDroneDispatchStrategy : public FLogisticsDeviceDispatchStrategy
{
    /** 引用宿主子系统，仅在 SelectBestDeviceIndex 中读取 DronePool 做距离排序；非热路径 */
    class UMassDspLogisticsSubsystem* Subsystem = nullptr;

    explicit FDroneDispatchStrategy(class UMassDspLogisticsSubsystem* InSubsystem)
        : Subsystem(InSubsystem) {}

    // ------------------------------------------------------------------
    //  SelectBestDeviceIndex：距取货点最近的空闲无人机
    // ------------------------------------------------------------------
    virtual int32 SelectBestDeviceIndex(
        const TArray<int32>&  IdlePoolIndices,
        const FLogisticsTask& Task) const override;

    // ------------------------------------------------------------------
    //  InitDeviceForTask：写入贝塞尔曲线及任务信息
    // ------------------------------------------------------------------
    virtual void InitDeviceForTask(
        void*                 DeviceDataPtr,
        const FLogisticsTask& Task) const override;

    // ------------------------------------------------------------------
    //  ResetDevice：重置无人机为 Idle
    // ------------------------------------------------------------------
    virtual void ResetDevice(void* DeviceDataPtr) const override;

    // ------------------------------------------------------------------
    //  GetDeviceLocation：返回 P3（贝塞尔末端，近似当前悬停位置）
    // ------------------------------------------------------------------
    virtual FVector GetDeviceLocation(const void* DeviceDataPtr) const override;
};