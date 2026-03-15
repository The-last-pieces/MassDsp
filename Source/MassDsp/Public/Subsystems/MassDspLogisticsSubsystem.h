#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"   // UTickableWorldSubsystem 在此头文件内
#include "MassEntityManager.h"
#include "MassEntityTypes.h"
#include "Components/InstancedStaticMeshComponent.h"

#include "Logistics/MassDspLogisticsTypes.h"
#include "Logistics/MassDspDroneData.h"
#include "Logistics/MassDspLogisticsDeviceStrategy.h"

#include "MassDspLogisticsSubsystem.generated.h"

class AActor;
class AMassDspGameMode;
class USceneComponent;
class UMassDspManager;
struct FMassDspLogisticsSaveChunk;

/**
 * 物流调度子系统（Demo 阶段：仅实现无人机，Vehicle/Train 暂未实现）
 *
 * 职责：
 *   1. 管理无人机对象池（DronePool，TSparseArray，O(1) 分配/回收）
 *   2. 维护无人机 ISM 渲染（DroneIdleHISM + DroneFlyingISM0-3 分桶）
 *   3. 每帧推进无人机状态机并批量同步 ISM Custom Data（无虚调用热路径）
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

    //  空闲索引池（O(1) 查找空闲无人机） 
    /** TArray 供 SelectBestDeviceIndex 顺序迭代；TSet 供 O(1) Contains 查询，两者始终同步 */
    TArray<int32> IdleDroneIndices;
    TSet<int32> IdleDroneIndexSet; ///< 镜像 IdleDroneIndices，专门用于 O(1) Contains 判断

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
    UPROPERTY()
    AActor* DroneISMHostActor = nullptr;
    UPROPERTY()
    USceneComponent* DroneISMHostRoot = nullptr;

    /**
     * 初始化 ISM 组件（由蓝图或关卡初始化逻辑调用一次）
     * @param InDroneISM    无人机 ISM 组件（需提前在关卡中放置或代码创建）
     */
    UFUNCTION(BlueprintCallable, Category = "MassDsp|Logistics")
    void SetupISMComponents(
        UInstancedStaticMeshComponent* InDroneISM);

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
     * @return                      请求 ID（int32）；-1 = 提交失败（无可用塔）
     */
    int32 SubmitSupplyRequest(
        FMassEntityHandle SourceEntity,
        EItemType ItemType,
        int32 Quantity,
        ELogisticsRequestPriority Priority = ELogisticsRequestPriority::Normal,
        FMassEntityHandle PreferredTowerEntity = FMassEntityHandle());

    /**
     * 提交需货请求（建筑库存不足，需要补入物品）。
     * 参数同 SubmitSupplyRequest。
     */
    int32 SubmitDemandRequest(
        FMassEntityHandle SourceEntity,
        EItemType ItemType,
        int32 Quantity,
        ELogisticsRequestPriority Priority = ELogisticsRequestPriority::Normal,
        FMassEntityHandle PreferredTowerEntity = FMassEntityHandle());

    /**
     * 取消请求。
     * 若请求已被配对成任务，同时取消对应任务并重置设备。
     */
    bool CancelRequest(int32 RequestId);

    /** 查询请求当前状态（若 ID 无效返回空指针） */
    const FLogisticsRequest* GetRequest(int32 RequestId) const;

    /** 查询任务当前状态（若 ID 无效返回空指针） */
    const FLogisticsTask* GetTask(int32 TaskId) const;

    int32 GetTotalDroneCount() const { return DronePool.Num(); }
    int32 GetIdleDroneCount() const { return IdleDroneIndices.Num(); }
    int32 GetPendingRequestCount() const { return AllRequests.Num(); }
    int32 GetActiveTaskCount() const { return AllTasks.Num(); }

    /**
     * 查询物流塔的无人机状态快照，供 Widget UI 每帧刷新时调用。
     * O(归属机数 + 活跃任务数)，非常轻量。
     */
    FTowerDroneStatus QueryTowerDroneStatus(FMassEntityHandle TowerEntity) const;

    void CollectSaveData(FMassDspLogisticsSaveChunk& OutSaveData) const;
    bool RestoreSaveData(const FMassDspLogisticsSaveChunk& InSaveData);

    // 
    //   设备生命周期（设备创建/销毁时调用）
    // 

public:
    /**
     * 创建并注册一架无人机。
     *
     * @param AffiliatedTowerEntity  归属物流塔 Entity；Invalid = 全局无归属设备
     * @param InitialLocation        初始悬停世界坐标（同步设置 P0~P3 与 ISM 实例位置，避免首次起飞跳变）
     * @param FlightSpeed            飞行速度 cm/s
     * @param CarryCapacity          最大载重数量
     * @return                       无人机句柄
     */
    FDroneHandle CreateDrone(
        FMassEntityHandle AffiliatedTowerEntity,
        const FVector& InitialLocation,
        float FlightSpeed,
        int32 CarryCapacity);

    /** 销毁无人机（从池中回收，释放 ISM 实例） */
    void DestroyDrone(FDroneHandle Handle);

    // 
    //   任务回调（由 Tick 内部状态机推进后调用，不对外暴露）
    // 

private:
    AMassDspGameMode* ResolveGameMode() const;
    bool EnsureDroneISMInitialized();
    void EnsureDefaultDispatchStrategies();

    /** 无人机到达取货点（State 变为 AtPickup） */
    void OnDroneArrivedAtPickup(int32 DronePoolIndex);
    /** 无人机到达交货点，执行物品转移（State 变为 Cooldown  Idle） */
    void OnDroneArrivedAtDelivery(int32 DronePoolIndex);
    /** 无人机任务失败，重置设备并将请求重新入队 */
    void OnDroneTaskFailed(int32 DronePoolIndex);

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

    /**
     * 统计当前正在前往 DemandEntity 的在途货物总量（已派遣但尚未完成的任务中 DeliveryEntity 匹配）。
     * 用于防止过度派遣：有效需求 = Threshold - Inventory - InTransitTo
     */
    int32 ComputeInTransitToEntity(FMassEntityHandle DemandEntity) const;

    /**
     * 统计当前正在从 SupplyEntity 取货但尚未到达取货点的任务货物量（Dispatched / InTransit_Pickup）。
     * InTransit_Deliver 阶段货物已从库存扣减，InventoryCount 本身已反映，不重复计算。
     * 有效供给 = Inventory - Threshold - InTransitFrom
     */
    int32 ComputeInTransitFromEntity(FMassEntityHandle SupplyEntity) const;

    // 
    //  内部数据
    // 

private:
    //  请求 / 任务池（int32 下标 ID，替代 FGuid 哈希，内存连续、O(1) 访问） 
    TSparseArray<FLogisticsRequest> AllRequests;
    TSparseArray<FLogisticsTask> AllTasks;

    //  塔运行时辅助数据（动态列表不在 Fragment 内） 
    TMap<FMassEntityHandle, FLogisticsTowerRuntimeData> TowerRuntimeData;

    //  脂塔队列（替代每帧全量扫描，僅处理已脏塔） 
    /** 冻塔 Set（O(1) 去重） */
    TSet<FMassEntityHandle> DirtyTowerSet;
    /** 冻塔有序列表（主线程读取） */
    TArray<FMassEntityHandle> DirtyTowerQueue;

    //  策略表（每类设备一个，任务分配时做一次虚调用） 
    TMap<ELogisticsDeviceType, TUniquePtr<FLogisticsDeviceDispatchStrategy>> DispatchStrategies;

    //  缓存 
    TWeakObjectPtr<UMassDspManager> CachedDspManager;

    UMassDspManager* GetDspManager();

    /**
     * 帧计数器：CleanExpiredRequests 每 60 帧执行一次，降低每帧全量扫描开销。
     * 请求/任务超时时长为 30s，60 帧约 1s 间隔完全覆盖及时清理需求。
     */
    int32 CleanupFrameCounter = 0;

    // 
    //  私有调度方法
    // 

    /** 提交请求的内部实现（Supply / Demand 共用），返回 int32 ID；-1 = 失败 */
    int32 SubmitRequestInternal(
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

    /** 将塔加入脏队列（O(1) 去重） */
    void EnqueueDirtyTower(FMassEntityHandle TowerEntity);

    /** 全局跨塔匹配：收集脏塔的 Supply/Demand 请求，按 ItemType 分桶配对，Tick Step2 调用 */
    void MatchPendingRequests();

    /**
     * 对匹配到的供应塔列表与需求请求列表批量派遣无人机。
     * SupplyTowers：同一 ItemType 下有足量库存的供应塔实体列表。
     * DemandIds  : 同一 ItemType 下的需求请求 ID 列表。
     * 供应塔不再使用请求系统，直接从塔实体读取实时状态。
     * InTransitFromCache / InTransitToCache: 由 MatchPendingRequests 预构建的单帧缓存，
     *   O(1) 查表替代每次派遣循环中拓展 ActiveTaskIds（O(Towers x ActiveTasks) to O(1)）。
     */
    void DispatchMatchedPairs(
        TArray<FMassEntityHandle>& SupplyTowers,
        TArray<int32>& DemandIds,
        TMap<FMassEntityHandle, int32>& InTransitFromCache,
        TMap<FMassEntityHandle, int32>& InTransitToCache);

    /** 尝试从候选无人机列表中为 Task 分配一架无人机。 */
    bool TryDispatchTask(FLogisticsTask& Task, const TArray<int32>& CandidateIndices);

    /**
     * 写入无人机 ISM GPU Custom Data（18 floats）。
     * 仅在状态切换时调用一次，就帧无需任何 CPU 工作。
     */
    void WriteDroneCustomData(const FDroneData& Drone, float GameTime) const;

    /** 冷却结束处理：提交归属塔请求 + 开始返航或转 Idle */
    void HandleCooldownEnded(int32 DroneIdx);

    /** 返航到家：转 Idle，加入空闲池，通知塔匹配 */
    void HandleDroneArrivedHome(int32 DroneIdx);

    /** 推进无人机状态机并同步 ISM（Tick 内部，无虚调用热路径） */
    void UpdateDrones(float DeltaTime);

    /** 清理超时请求 */
    void CleanExpiredRequests();

    /** 将无人机 ISM 实例更新到贝塞尔插值位置（调试用，热路径不调用） */
    void UpdateDroneISMInstance(FDroneData& Drone) const;

    /** 在无人机空间哈希中更新单架无人机的格子归属 */
    void UpdateDroneInGrid(int32 DronePoolIndex, const FVector& OldPos, const FVector& NewPos);

    /** 在 DroneIdleHISM 中分配 Idle 实例（BucketIndex=-1 新建无人机时调用）。 */
    int32 AllocateDroneISMInstance(const FVector& InitialLocation);

    void RebuildDroneISMInstances();

    void FreeDroneISMInstance(int32 InstanceIndex);

    /** 执行物品转移：取货建筑.TryProvide  设备携带  交货建筑.TryConsume */
    bool ExecuteItemTransfer(FMassEntityHandle PickupEntity, FMassEntityHandle DeliveryEntity,
                             EItemType& InOutItemType, int32& InOutQuantity);

    void ResetRuntimeState();
};
