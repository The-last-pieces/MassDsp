#include "Logistics/MassDspDroneStrategy.h"

#include "Subsystems/MassDspLogisticsSubsystem.h"
#include "Logistics/MassDspDroneData.h"
#include "Logistics/MassDspLogisticsTypes.h"
#include "GameConst.h"

// ============================================================
//  SelectBestDeviceIndex
//  从空闲无人机池中选出距取货点最近的无人机（P3 近似当前悬停位置）
// ============================================================
int32 FDroneDispatchStrategy::SelectBestDeviceIndex(
    const TArray<int32>&  IdlePoolIndices,
    const FLogisticsTask& Task) const
{
    if (!Subsystem || IdlePoolIndices.IsEmpty()) return -1;

    int32  BestIdx    = -1;
    float  BestDistSq = MAX_FLT;

    for (int32 Idx : IdlePoolIndices)
    {
        if (!Subsystem->DronePool.IsValidIndex(Idx)) continue;
        const FDroneData& Drone = Subsystem->DronePool[Idx];

        // 优先选取归属取货点所在塔的无人机，其次全局最近
        const float DistSq = FVector::DistSquared(Drone.P3, Task.PickupLocation);
        if (DistSq < BestDistSq)
        {
            BestDistSq = DistSq;
            BestIdx    = Idx;
        }
    }

    return BestIdx;
}

// ============================================================
//  InitDeviceForTask
//  生成飞往取货点的贝塞尔曲线，并写入任务关联字段
// ============================================================
void FDroneDispatchStrategy::InitDeviceForTask(
    void*                 DeviceDataPtr,
    const FLogisticsTask& Task) const
{
    FDroneData* Drone = static_cast<FDroneData*>(DeviceDataPtr);
    if (!Drone) return;

    // P0 = 无人机当前位置（上一段贝塞尔末端，或初始化时的塔位置）
    const FVector P0  = Drone->P3;
    const FVector P3  = Task.PickupLocation;
    const float   Arc  = FGameConst::DroneFlightArcHeight;
    const float   Lane = FGameConst::DroneFlightLaneOffset;

    // 水平飞行方向的右向量（XY 平面内，始终右偏形成单向航道）
    // 去程（取货）与回程（送货）方向相反，右侧天然在世界空间的两侧，避免航道重叠
    const FVector Dir2D   = (P3 - P0).GetSafeNormal2D();
    // 对 Dir2D 做 90° 顺时针旋转（俯视）得到右方向：(dx,dy) → (dy,-dx)
    // 保证去程/回程都偏向各自行进方向的右侧，两条航道在世界空间中分离
    const FVector RightXY = Dir2D.IsNearlyZero()
        ? FVector::RightVector
        : FVector(Dir2D.Y, -Dir2D.X, 0.f);

    Drone->P0 = P0;
    Drone->P1 = P0 + FVector(0.f, 0.f, Arc) + RightXY * Lane;
    Drone->P2 = P3 + FVector(0.f, 0.f, Arc) + RightXY * Lane;
    Drone->P3 = P3;

    const float Dist        = FVector::Dist(P0, P3);
    Drone->TotalFlightTime  = Dist / FMath::Max(1.f, Drone->FlightSpeed);
    Drone->ElapsedTime      = 0.f;

    Drone->State            = ELogisticsDeviceState::MovingToPickup;
    Drone->CurrentTaskId    = Task.TaskId;

    Drone->PickupEntity     = Task.PickupEntity;
    Drone->DeliveryEntity   = Task.DeliveryEntity;
    Drone->PickupLocation   = Task.PickupLocation;
    Drone->DeliveryLocation = Task.DeliveryLocation;
}

// ============================================================
//  ResetDevice
// ============================================================
void FDroneDispatchStrategy::ResetDevice(void* DeviceDataPtr) const
{
    FDroneData* Drone = static_cast<FDroneData*>(DeviceDataPtr);
    if (!Drone) return;

    Drone->State           = ELogisticsDeviceState::Idle;
    Drone->CurrentTaskId   = FGuid();
    Drone->CarriedItemType = EItemType::None;
    Drone->CarriedQuantity = 0;
}

// ============================================================
//  GetDeviceLocation
// ============================================================
FVector FDroneDispatchStrategy::GetDeviceLocation(const void* DeviceDataPtr) const
{
    const FDroneData* Drone = static_cast<const FDroneData*>(DeviceDataPtr);
    return Drone ? Drone->P3 : FVector::ZeroVector;
}