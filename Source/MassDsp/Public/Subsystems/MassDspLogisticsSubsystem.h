#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"   // UTickableWorldSubsystem 在此头文件内
#include "MassEntityManager.h"
#include "MassEntityTypes.h"
#include "Components/InstancedStaticMeshComponent.h"

#include "Logistics/MassDspLogisticsTypes.h"
#include "Logistics/MassDspDroneData.h"
#include "Logistics/MassDspVehicleData.h"
#include "Logistics/MassDspTrainData.h"
#include "Logistics/MassDspLogisticsDeviceStrategy.h"

#include "MassDspLogisticsSubsystem.generated.h"

class UMassDspManager;

/**
 * 物流调度子系统
 *
 * 职责：
 *   1. 管理三类设备的对象池（DronePool / VehiclePool / TrainPool）
 *      - 全 POD，TSparseArray，O(1) 分配回收，对标传送带 BeltEntityRegistry
 *   2. 维护设备的 ISM 渲染（DroneISM / VehicleISM / TrainISM）
 *      - 同 UMassDspManager::ItemISMPool 完全一致的模式
 *   3. 每帧推进设备状态机并批量同步 ISM 位置（无虚调用热路径）
 *   4. 管理请求/任务生命周期（AllRequests / AllTasks）
 *   5. 为物流塔提供请求提交接口（事件推送，置塔的 bDirty）
 *   6. 通过策略表（DispatchStrategies）在任务分配时做一次虚调用，其余零虚调用
 *
 * 性能关键点：
 *   - 10w 无人机：TSparseArray<FDroneData> 连续内存，Tick 直接遍历，
 *     每帧做贝塞尔插值 + UpdateInstanceTransform，无任何 UObject / 虚调用
 *   - 无人机就近分配：DroneGridCells 空间哈希，O(1) 平均查找空闲无人机
 *   - TODO[PERF]: 可改为 BatchUpdateInstancesTransforms，每帧一次 API 调用
 */
UCLASS()
class MASSDSP_API UMassDspLogisticsSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

    // 
    //  初始化 / 销毁
    // 

protected:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }

    // 
    //  Tick（UTickableWorldSubsystem 已内置处理 FTickableGameObject vtable 问题）
    // 

public:
    virtual void Tick(float DeltaTime) override;

    virtual TStatId GetStatId() const override
    {
        RETURN_QUICK_DECLARE_CYCLE_STAT(UMassDspLogisticsSubsystem, STATGROUP_Tickables);
    }

    // 
    //   设备池（对标 UMassDspManager::BeltEntityRegistry 模式）
    // 

public:
    /** 无人机数据池（10w+ 量级，TSparseArray O(1) 分配/回收） */
    TSparseArray<FDroneData> DronePool;
    /** 地面小车数据池（1k+ 量级） */
    TSparseArray<FVehicleData> VehiclePool;
    /** 火车数据池（1k+ 量级） */
    TSparseArray<FTrainData> TrainPool;

    /**
     * 地面小车路径点数据（与 VehiclePool 按 Index 对齐，不在 FVehicleData 内保持 POD）
     * VehiclePaths[vehiclePoolIndex] = 当前任务的路径航点列表
     * TODO[VEHICLE]: NavMesh 寻路结果填入此处
     */
    TArray<TArray<FVector>> VehiclePaths;

    /**
     * 火车轨道 LUT（与 TrainPool 按 Index 对齐）
     * TrainTrackLUTs[trainPoolIndex] = 预烘焙轨道采样点（复用 FBeltLUTSample 模式）
     * TODO[TRAIN]: 轨道 Editor 工具填入；运行时区间调度在 UpdateTrains 中实现
     */
    TArray<TArray<FVector>> TrainTrackLUTs;

    //  空闲索引池（O(1) 查找空闲设备，无需遍历整个 Pool） 
    TArray<int32> IdleDroneIndices;
    TArray<int32> IdleVehicleIndices;
    TArray<int32> IdleTrainIndices;

    //  无人机空间哈希（同 UMassDspManager::BuildingHashGrid 完全一致的实现） 
    /** Key = MakeDroneCellKey(CX,CY)，Value = DronePool 物理 Index（可能包含非空闲无人机） */
    TMap<uint64, TArray<int32>> DroneGridCells;

    static FORCEINLINE uint64 MakeDroneCellKey(int32 CX, int32 CY)
    {
        return (static_cast<uint64>(static_cast<uint32>(CX)) << 32) | static_cast<uint32>(CY);
    }

    // 
    //   ISM 渲染（同 UMassDspManager::ItemISMPool 模式）
    // 

public:
    /** 无人机 ISM（10w+ 实例，由 MeshConfig 资产通过蓝图赋值） */
    UPROPERTY()
    UInstancedStaticMeshComponent* DroneISM = nullptr;

    /** 地面小车 ISM */
    UPROPERTY()
    UInstancedStaticMeshComponent* VehicleISM = nullptr;

    /** 火车 ISM */
    UPROPERTY()
    UInstancedStaticMeshComponent* TrainISM = nullptr;

    /**
     * 初始化 ISM 组件（由蓝图或关卡初始化逻辑调用一次）
     * @param InDroneISM    无人机 ISM 组件（需提前在关卡中放置或代码创建）
     * @param InVehicleISM  地面小车 ISM 组件
     * @param InTrainISM    火车 ISM 组件
     */
    UFUNCTION(BlueprintCallable, Category = "MassDsp|Logistics")
    void SetupISMComponents(
        UInstancedStaticMeshComponent* InDroneISM,
        UInstancedStaticMeshComponent* InVehicleISM,
        UInstancedStaticMeshComponent* InTrainISM);

    // 
    //   请求接口（建筑 / Processor  子系统）
    // 

public:
    /**
     * 提交供货请求（建筑库存过剩，需要输出物品）。
     *
     * @param SourceEntity          发起请求的建筑 Mass Entity
     * @param ItemType              物品类型
     * @param Quantity              数量
     * @param Priority              优先级（默认 Normal）
     * @param PreferredTowerEntity  希望服务的塔；Invalid  自动路由到最近塔
     * @return                      请求 ID（FGuid）；全零 = 提交失败（无可用塔）
     */
    FGuid SubmitSupplyRequest(
        FMassEntityHandle SourceEntity,
        EItemType ItemType,
        int32 Quantity,
        ELogisticsRequestPriority Priority = ELogisticsRequestPriority::Normal,
        FMassEntityHandle PreferredTowerEntity = FMassEntityHandle());

    /**
     * 提交需货请求（建筑库存不足，需要补入物品）。
     * 参数同 SubmitSupplyRequest。
     */
    FGuid SubmitDemandRequest(
        FMassEntityHandle SourceEntity,
        EItemType ItemType,
        int32 Quantity,
        ELogisticsRequestPriority Priority = ELogisticsRequestPriority::Normal,
        FMassEntityHandle PreferredTowerEntity = FMassEntityHandle());

    /**
     * 取消请求。
     * 若请求已被配对成任务，同时取消对应任务并重置设备。
     */
    bool CancelRequest(const FGuid& RequestId);

    /** 查询请求当前状态（若 ID 无效返回空指针） */
    const FLogisticsRequest* GetRequest(const FGuid& RequestId) const;

    /** 查询任务当前状态（若 ID 无效返回空指针） */
    const FLogisticsTask* GetTask(const FGuid& TaskId) const;

    // 
    //   设备生命周期（设备创建/销毁时调用）
    // 

public:
    /**
     * 创建并注册一架无人机。
     *
     * @param AffiliatedTowerEntity  归属物流塔 Entity；Invalid = 全局无归属设备
     * @param FlightSpeed            飞行速度 cm/s
     * @param CarryCapacity          最大载重数量
     * @return                       无人机句柄
     */
    FDroneHandle CreateDrone(
        FMassEntityHandle AffiliatedTowerEntity = FMassEntityHandle(),
        float FlightSpeed = FGameConst::DefaultDroneFlightSpeed,
        int32 CarryCapacity = 10);

    /**
     * 创建并注册一辆地面小车。
     * @param SpawnLocation  出生世界坐标
     */
    FVehicleHandle CreateVehicle(const FVector& SpawnLocation, int32 CarryCapacity = 50);

    /**
     * 创建并注册一列火车。
     * @param TrackSegmentIndex  归属轨道段 Index
     */
    FTrainHandle CreateTrain(int32 TrackSegmentIndex, int32 CarryCapacity = 200);

    /** 销毁无人机（从池中回收，释放 ISM 实例） */
    void DestroyDrone(FDroneHandle Handle);
    /** 销毁小车 */
    void DestroyVehicle(FVehicleHandle Handle);
    /** 销毁火车 */
    void DestroyTrain(FTrainHandle Handle);

    // 
    //   任务回调（由 Tick 内部状态机推进后调用，不对外暴露）
    // 

private:
    /** 无人机到达取货点（State 变为 AtPickup） */
    void OnDroneArrivedAtPickup(int32 DronePoolIndex);
    /** 无人机到达交货点，执行物品转移（State 变为 Cooldown  Idle） */
    void OnDroneArrivedAtDelivery(int32 DronePoolIndex);
    /** 无人机任务失败，重置设备并将请求重新入队 */
    void OnDroneTaskFailed(int32 DronePoolIndex);

    // TODO[VEHICLE]: OnVehicleArrivedAtPickup / OnVehicleArrivedAtDelivery
    // TODO[TRAIN]:   OnTrainArrivedAtPickup  / OnTrainArrivedAtDelivery

    // 
    //   策略扩展（后续设备模块调用注册，子系统无需改动）
    // 

public:
    /**
     * 注册设备分派策略。
     * 后续各设备模块（无人机/小车/火车）在模块加载时调用一次。
     */
    void RegisterDispatchStrategy(
        ELogisticsDeviceType Type,
        TUniquePtr<FLogisticsDeviceDispatchStrategy> Strategy);

    // 
    //  内部数据
    // 

private:
    //  请求 / 任务池 
    TMap<FGuid, FLogisticsRequest> AllRequests;
    TMap<FGuid, FLogisticsTask> AllTasks;

    //  塔运行时辅助数据（动态列表不在 Fragment 内） 
    TMap<FMassEntityHandle, FLogisticsTowerRuntimeData> TowerRuntimeData;

    //  策略表（每类设备一个，任务分配时做一次虚调用） 
    TMap<ELogisticsDeviceType, TUniquePtr<FLogisticsDeviceDispatchStrategy>> DispatchStrategies;

    //  缓存 
    TWeakObjectPtr<UMassDspManager> CachedDspManager;

    UMassDspManager* GetDspManager();

    // 
    //  私有调度方法
    // 

    /** 提交请求的内部实现（Supply / Demand 共用） */
    FGuid SubmitRequestInternal(
        ELogisticsRequestType Type,
        FMassEntityHandle SourceEntity,
        EItemType ItemType,
        int32 Quantity,
        ELogisticsRequestPriority Priority,
        FMassEntityHandle PreferredTowerEntity);

    /**
     * 找到最近的可接受塔（复用 UMassDspManager::FindNearestBuilding）。
     * 返回无效 Handle 表示无可用塔。
     */
    FMassEntityHandle FindNearestEligibleTower(FMassEntityHandle SourceEntity) const;

    /** 推进脏塔的请求匹配（Tick 每帧 Step 2） */
    void MatchPendingRequests();

    /**
     * 尝试为单个塔匹配一对 Supply + Demand 请求并派发任务。
     * 每次调用只处理一对（避免长帧），可在一帧内多次迭代。
     */
    void TryMatchAndDispatchForTower(
        FMassEntityHandle TowerEntity,
        FLogisticsTowerRuntimeData& RuntimeData);

    /** 派发任务到空闲设备（调用策略表，写设备 POD 状态） */
    bool TryDispatchTask(FLogisticsTask& Task);

    /** 推进无人机状态机并同步 ISM（Tick 内部，无虚调用热路径） */
    void UpdateDrones(float DeltaTime);

    /** 推进地面小车状态机并同步 ISM（占位，TODO[VEHICLE] 实现运动逻辑） */
    void UpdateVehicles(float DeltaTime);

    /** 推进火车状态机并同步 ISM（占位，TODO[TRAIN] 实现区间调度） */
    void UpdateTrains(float DeltaTime);

    /** 清理超时请求 */
    void CleanExpiredRequests();

    /** 将无人机 ISM 实例更新到贝塞尔插值位置 */
    void UpdateDroneISMInstance(FDroneData& Drone) const;

    /** 在无人机空间哈希中更新单架无人机的格子归属 */
    void UpdateDroneInGrid(int32 DronePoolIndex, const FVector& OldPos, const FVector& NewPos);

    /** 向 TowerRuntimeData 注册 ISM 实例 */
    int32 AllocateDroneISMInstance(const FVector& InitialLocation);
    int32 AllocateVehicleISMInstance(const FVector& InitialLocation);
    int32 AllocateTrainISMInstance(const FVector& InitialLocation);

    void FreeDroneISMInstance(int32 InstanceIndex);
    void FreeVehicleISMInstance(int32 InstanceIndex);
    void FreeTrainISMInstance(int32 InstanceIndex);

    /** 执行物品转移：取货建筑.TryProvide  设备携带  交货建筑.TryConsume */
    bool ExecuteItemTransfer(FMassEntityHandle PickupEntity, FMassEntityHandle DeliveryEntity,
                             EItemType& InOutItemType, int32& InOutQuantity);
};
