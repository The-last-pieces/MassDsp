#include "Subsystems/MassDspLogisticsSubsystem.h"

#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassCommonFragments.h"    // FTransformFragment
#include "Subsystems/MassDspManager.h"
#include "Fragments/MassDspStorageFragment.h"
#include "Fragments/MassDspLogisticsTowerFragment.h"
#include "Async/ParallelFor.h"      // ParallelFor
#include "Misc/ScopeLock.h"         // FScopeLock / FCriticalSection

// 安全获取 FMassEntityManager 指针（启动阶段 UMassEntitySubsystem 可能尚未就绪）
static FMassEntityManager* GetEntityManagerSafe(UWorld* World)
{
    if (!World) return nullptr;
    UMassEntitySubsystem* Sub = World->GetSubsystem<UMassEntitySubsystem>();
    return Sub ? &Sub->GetMutableEntityManager() : nullptr;
}

// 
//  初始化 / 销毁
// 

void UMassDspLogisticsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    // 预分配常用容器容量，减少运行时 rehash
    TowerRuntimeData.Reserve(64);
    IdleDroneIndices.Reserve(1024);
    IdleDroneIndexSet.Reserve(1024);
    IdleVehicleIndices.Reserve(128);
    IdleTrainIndices.Reserve(64);
    DirtyTowerQueue.Reserve(256);
}

void UMassDspLogisticsSubsystem::Deinitialize()
{
    DronePool.Empty();
    VehiclePool.Empty();
    TrainPool.Empty();
    VehiclePaths.Empty();
    TrainTrackLUTs.Empty();
    AllRequests.Empty();
    AllTasks.Empty();
    TowerRuntimeData.Empty();
    DroneGridCells.Empty();
    IdleDroneIndexSet.Empty();
    DirtyTowerSet.Reset();
    DirtyTowerQueue.Reset();
    DispatchStrategies.Empty();
    Super::Deinitialize();
}

// 
//  ISM 设置
// 

void UMassDspLogisticsSubsystem::SetupISMComponents(
    UInstancedStaticMeshComponent* InDroneISM,
    UInstancedStaticMeshComponent* InVehicleISM,
    UInstancedStaticMeshComponent* InTrainISM)
{
    DroneISM   = InDroneISM;
    VehicleISM = InVehicleISM;
    TrainISM   = InTrainISM;

    // 18 floats per instance:
    // [0]=TimeAtDispatch  [1]=TotalFlightTime
    // [2-4]=P0  [5-7]=P1  [8-10]=P2
    // [11-13]=HomeLocation (永久)  [14]=IdlePhaseOffset
    // [15-17]=P3飞行终点 (仅飞行时有意义)
    if (DroneISM)
    {
        DroneISM->NumCustomDataFloats = 18;
        // WPO 会把顶点从 HomeLocation 移到飞行轨迹，静态 bounds 不够宽导致被剥稽
        // BoundsScale 起外层安全幇作用（主要靠材质的 MaxWPODisplacement）
        DroneISM->BoundsScale = 100.f;
        // 禁用距离剥稽，配送任务范围可能超出默认导欠剥稽距离
        DroneISM->SetCullDistance(0.f);
    }
}

// 
//  Tick：三步流水线
// 

void UMassDspLogisticsSubsystem::Tick(float DeltaTime)
{
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // 渲染层 Bug 修复：执行顺序必须是「先派遣，再写 CustomData」
    //
    // 原顺序（错误）：UpdateDrones(Phase B 写 CustomData) → MatchPendingRequests(派遣)
    //   结果：新派遣的无人机 State 还是 Idle，Phase B 写入 bIdle=true (TotalFlightTime=0)，
    //         GPU 端 shader 执行悬停螺旋动画，直到下一帧才切飞行。
    //         表现为：统计显示「外派」，视觉上在塔旁盘旋。
    //
    // 正确顺序：MatchPendingRequests(派遣) → UpdateDrones(Phase B 写 CustomData)
    //   派遣完成后 Drone.State 已是 MovingToPickup，Phase B 写入 bIdle=false + 飞行贝塞尔，
    //   GPU 当帧就能收到正确的飞行 CustomData，无人机立即开始飞行动画。
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    // Step 1：处理脏塔的请求匹配与任务派发（先于 CustomData 写入）
    MatchPendingRequests();

    // Step 2：推进所有设备状态机 + 批量同步 ISM CustomData（派遣后状态已正确）
    UpdateDrones(DeltaTime);
    UpdateVehicles(DeltaTime);
    UpdateTrains(DeltaTime);

    // Step 3：清理超时请求
    CleanExpiredRequests();

    // ── 一致性自愈：State=Idle 但不在空闲池（IdleDroneIndices / Set 去同步）───────
    // 成因：任何路径调用了 Drone.State=Idle 却未同步更新池（或 TSparseArray slot 复用边界）。
    // 自愈策略：发现即补录，并打 Warning 供排查；不影响已正确在列的 Idle 无人机。
    for (auto It = DronePool.CreateIterator(); It; ++It)
    {
        const int32 Idx = It.GetIndex();
        FDroneData& D = *It;
        if (D.State == ELogisticsDeviceState::Idle && !IdleDroneIndexSet.Contains(Idx))
        {
            UE_LOG(LogTemp, Warning,
                   TEXT("[Logistics] HEAL: drone poolIdx=%d State=Idle but NOT in IdlePool! Re-adding. CurrentTaskId=%d"),
                   Idx, D.CurrentTaskId);
            IdleDroneIndices.Add(Idx);
            IdleDroneIndexSet.Add(Idx);
            // 顺便把归属塔标脏，让下一帧 MatchPendingRequests 立即派遣
            if (D.AffiliatedTowerEntity.IsValid())
                EnqueueDirtyTower(D.AffiliatedTowerEntity);
        }
    }

    // 调试：每秒输出一次各状态无人机分布，帮助定位「1个闲置」根因
    static float DbgAccum = 0.f;
    DbgAccum += DeltaTime;
    if (DbgAccum >= 1.f)
    {
        DbgAccum = 0.f;

        int32 CntIdle=0, CntCooldown=0, CntPickup=0, CntDeliver=0, CntReturning=0, CntStagger=0;
        for (auto It = DronePool.CreateConstIterator(); It; ++It)
        {
            const FDroneData& D = *It;
            if (D.ElapsedTime < -SMALL_NUMBER && D.State != ELogisticsDeviceState::Idle)
                ++CntStagger;           // 错峰等待（已派遣但倒计时中）
            else switch (D.State)
            {
            case ELogisticsDeviceState::Idle:          ++CntIdle;      break;
            case ELogisticsDeviceState::Cooldown:      ++CntCooldown;  break;
            case ELogisticsDeviceState::MovingToPickup: ++CntPickup;   break;
            case ELogisticsDeviceState::MovingToDeliver:++CntDeliver;  break;
            case ELogisticsDeviceState::ReturningHome: ++CntReturning; break;
            default: break;
            }
        }
        UE_LOG(LogTemp, Log,
               TEXT("[Drones] Total=%d | Idle=%d Stagger=%d Pickup=%d Deliver=%d Cooldown=%d Return=%d | IdlePool=%d Req=%d Tasks=%d"),
               DronePool.Num(), CntIdle, CntStagger, CntPickup, CntDeliver, CntCooldown, CntReturning,
               IdleDroneIndices.Num(), AllRequests.Num(), AllTasks.Num());
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(42, 2.f, FColor::Cyan,
                FString::Printf(TEXT("[Logistics] Idle:%d Stagger:%d Pickup:%d Deliver:%d Cool:%d Return:%d"),
                    CntIdle, CntStagger, CntPickup, CntDeliver, CntCooldown, CntReturning));
        }
    }
}

// 
//  请求接口
// 

int32 UMassDspLogisticsSubsystem::SubmitSupplyRequest(
    FMassEntityHandle SourceEntity, EItemType ItemType, int32 Quantity,
    ELogisticsRequestPriority Priority, FMassEntityHandle PreferredTowerEntity)
{
    return SubmitRequestInternal(ELogisticsRequestType::Supply,
                                 SourceEntity, ItemType, Quantity, Priority, PreferredTowerEntity);
}

int32 UMassDspLogisticsSubsystem::SubmitDemandRequest(
    FMassEntityHandle SourceEntity, EItemType ItemType, int32 Quantity,
    ELogisticsRequestPriority Priority, FMassEntityHandle PreferredTowerEntity)
{
    return SubmitRequestInternal(ELogisticsRequestType::Demand,
                                 SourceEntity, ItemType, Quantity, Priority, PreferredTowerEntity);
}

int32 UMassDspLogisticsSubsystem::SubmitRequestInternal(
    ELogisticsRequestType Type,
    FMassEntityHandle SourceEntity, EItemType ItemType, int32 Quantity,
    ELogisticsRequestPriority Priority,
    FMassEntityHandle PreferredTowerEntity)
{
    FMassEntityHandle TowerEntity = PreferredTowerEntity.IsValid()
                                        ? PreferredTowerEntity
                                        : FindNearestEligibleTower(SourceEntity);
    if (!TowerEntity.IsValid()) return -1;

    UWorld* World = GetWorld();
    if (!World) return -1;

    FMassEntityManager* EntityManagerPtr = GetEntityManagerSafe(World);
    if (!EntityManagerPtr) return -1;
    FMassEntityManager& EntityManager = *EntityManagerPtr;
    if (!EntityManager.IsEntityValid(TowerEntity)) return -1;

    FMassDspLogisticsTowerFragment* TowerFrag =
        EntityManager.GetFragmentDataPtr<FMassDspLogisticsTowerFragment>(TowerEntity);
    if (!TowerFrag || !TowerFrag->bAcceptsRequests) return -1;

    // ── O(1) 去重：每塔缓存一个请求 ID ──────────────────────────────────────────
    FLogisticsTowerRuntimeData& RuntimeDataRef = TowerRuntimeData.FindOrAdd(TowerEntity);
    const int32 CachedId = RuntimeDataRef.CachedReqId;
    if (CachedId >= 0 && AllRequests.IsValidIndex(CachedId))
    {
        FLogisticsRequest& ExistReq = AllRequests[CachedId];
        if (ExistReq.Type == Type
            && ExistReq.SourceEntity          == SourceEntity
            && ExistReq.ItemType              == ItemType
            && ExistReq.PreferredTowerEntity  == TowerEntity)
        {
            ExistReq.Quantity    = FMath::Max(1, Quantity);
            ExistReq.RequestTime = World->GetTimeSeconds();
            if (!RuntimeDataRef.PendingRequestIds.Contains(CachedId))
                RuntimeDataRef.PendingRequestIds.Add(CachedId);
            EnqueueDirtyTower(TowerEntity);
            TowerFrag->bDirty = true;
            return CachedId;
        }
        RuntimeDataRef.CachedReqId = -1; // 失效缓存
    }

    // ── 新建请求 ────────────────────────────────────────────────────────────────
    FLogisticsRequest Req;
    Req.Type                 = Type;
    Req.SourceEntity         = SourceEntity;
    Req.ItemType             = ItemType;
    Req.Quantity             = FMath::Max(1, Quantity);
    Req.Priority             = Priority;
    Req.PreferredTowerEntity = TowerEntity;
    Req.RequestTime          = World->GetTimeSeconds();
    Req.ExpiryDuration       = 30.f;

    const int32 ReqId = AllRequests.Add(Req);
    AllRequests[ReqId].RequestId = ReqId; // 自引用

    FLogisticsTowerRuntimeData& RuntimeData = TowerRuntimeData.FindOrAdd(TowerEntity);
    RuntimeData.PendingRequestIds.Add(ReqId);
    RuntimeData.CachedReqId = ReqId; // 写入 O(1) 去重缓存

    EnqueueDirtyTower(TowerEntity);
    TowerFrag->bDirty = true;
    UE_LOG(LogTemp, Log,
           TEXT("[Logistics] SubmitRequest OK | Type=%s Item=%d Qty=%d Tower=[%d,%d] PendingNow=%d"),
           Type == ELogisticsRequestType::Supply ? TEXT("Supply") : TEXT("Demand"),
           (int32)ItemType, Req.Quantity,
           TowerEntity.Index, TowerEntity.SerialNumber,
           RuntimeData.PendingRequestIds.Num());
    return ReqId;
}

bool UMassDspLogisticsSubsystem::CancelRequest(int32 RequestId)
{
    if (!AllRequests.IsValidIndex(RequestId)) return false;
    FLogisticsRequest* Req = &AllRequests[RequestId];

    // 如果已被配对进活跃任务，也取消对应任务并重置设备
    for (int32 i = 0; i < AllTasks.GetMaxIndex(); ++i)
    {
        if (!AllTasks.IsValidIndex(i)) continue;
        FLogisticsTask& Task = AllTasks[i];
        if (Task.SupplyRequestId != RequestId && Task.DemandRequestId != RequestId) continue;

        // 跳过终态任务
        if (Task.State == ELogisticsTaskState::Completed  ||
            Task.State == ELogisticsTaskState::Failed     ||
            Task.State == ELogisticsTaskState::Cancelled)
            continue;

        // 跳过仍在飞行中的活跃任务
        if (Task.State == ELogisticsTaskState::InTransit_Pickup  ||
            Task.State == ELogisticsTaskState::InTransit_Deliver)
            continue;

        Task.State = ELogisticsTaskState::Cancelled;
        if (Task.DeviceType == ELogisticsDeviceType::Drone && DronePool.IsValidIndex(Task.DevicePoolIndex))
        {
            FDroneData& Drone = DronePool[Task.DevicePoolIndex];
            if (Drone.CurrentTaskId == i)
            {
                Drone.State         = ELogisticsDeviceState::Idle;
                Drone.CurrentTaskId = -1;
                // Bug #10 修复：使用 Set 去重，防止重复入队导致重复派遣
                if (!IdleDroneIndexSet.Contains(Task.DevicePoolIndex))
                {
                    IdleDroneIndices.Add(Task.DevicePoolIndex);
                    IdleDroneIndexSet.Add(Task.DevicePoolIndex);
                }
            }
        }
    }

    // 从塔运行时数据中移除
    if (Req->PreferredTowerEntity.IsValid())
    {
        if (FLogisticsTowerRuntimeData* RTD = TowerRuntimeData.Find(Req->PreferredTowerEntity))
        {
            RTD->PendingRequestIds.Remove(RequestId);
            if (RTD->CachedReqId == RequestId) RTD->CachedReqId = -1;
        }
    }

    AllRequests.RemoveAt(RequestId);
    return true;
}

const FLogisticsRequest* UMassDspLogisticsSubsystem::GetRequest(int32 RequestId) const
{
    return AllRequests.IsValidIndex(RequestId) ? &AllRequests[RequestId] : nullptr;
}

const FLogisticsTask* UMassDspLogisticsSubsystem::GetTask(int32 TaskId) const
{
    return AllTasks.IsValidIndex(TaskId) ? &AllTasks[TaskId] : nullptr;
}

FTowerDroneStatus UMassDspLogisticsSubsystem::QueryTowerDroneStatus(FMassEntityHandle TowerEntity) const
{
    FTowerDroneStatus Result;
    const FLogisticsTowerRuntimeData* RTD = TowerRuntimeData.Find(TowerEntity);
    if (!RTD) return Result;

    // ── 统计归属无人机状态 ────────────────────────────────────────
    for (const FDroneHandle& Handle : RTD->AffiliatedDroneHandles)
    {
        if (!Handle.IsValid()) continue;
        if (!DronePool.IsValidIndex(Handle.Index)) continue;

        switch (DronePool[Handle.Index].State)
        {
        case ELogisticsDeviceState::Idle:
            ++Result.OwnedResting; // 已返家盘旋等待：展示为休息
            break;
        case ELogisticsDeviceState::Cooldown:    // 刷交货后等待返航，尚在别处
        case ELogisticsDeviceState::ReturningHome: // 返航中，尚未到家
        default:
        {
            // 错峰起飞（ElapsedTime < 0）：无人机已被派遣、货物已锁定，
            // 只是倒计时未归零，从用户角度属于"已外派"而非"休息"
            ++Result.OwnedDeployed;
            break;
        }
        }
    }

    // ── 统计正在飞来本塔的无人机（InTransit_Deliver + DeliveryEntity==本塔） ──────
    for (const int32 TaskId : RTD->ActiveTaskIds)
    {
        if (!AllTasks.IsValidIndex(TaskId)) continue;
        const FLogisticsTask& Task = AllTasks[TaskId];
        if (Task.State == ELogisticsTaskState::InTransit_Deliver &&
            Task.DeliveryEntity == TowerEntity)
        {
            ++Result.Incoming;
        }
    }

    return Result;
}

int32 UMassDspLogisticsSubsystem::ComputeInTransitToEntity(FMassEntityHandle DemandEntity) const
{
    // 统计「最终会送达该实体」的在途货物总量：
    //   InTransit_Pickup  — 无人机正飞往取货点，货物尚未装载，但该批次已承诺送达此目标。
    //   InTransit_Deliver — 无人机已取货，正飞往该交货点。
    // 两个阶段都计入，才能准确防止对同一需求塔重复派遣过量无人机。
    // 若只计 InTransit_Deliver（旧逻辑），则 Pickup 阶段的承诺量不被扣除，
    // 会导致 MatchPendingRequests 认为「还有空位」而额外派遣，产生多次取货竞争。
    const FLogisticsTowerRuntimeData* RTD = TowerRuntimeData.Find(DemandEntity);
    if (!RTD) return 0;

    int32 Total = 0;
    for (const int32 TaskId : RTD->ActiveTaskIds)
    {
        if (!AllTasks.IsValidIndex(TaskId)) continue;
        const FLogisticsTask& Task = AllTasks[TaskId];
        if (Task.DeliveryEntity == DemandEntity &&
            (Task.State == ELogisticsTaskState::InTransit_Pickup ||
             Task.State == ELogisticsTaskState::InTransit_Deliver ||
             Task.State == ELogisticsTaskState::Dispatched))
        {
            Total += Task.TransferQuantity;
        }
    }
    return Total;
}

int32 UMassDspLogisticsSubsystem::ComputeInTransitFromEntity(FMassEntityHandle SupplyEntity) const
{
    // 岗只统计已派遣但尚未到达取货点的任务：
    // InTransit_Deliver 阶段货物已从取货建筑实际历数扣减，InventoryCount 本身已包含此消耗，不重复计算。
    const FLogisticsTowerRuntimeData* RTD = TowerRuntimeData.Find(SupplyEntity);
    if (!RTD) return 0;

    int32 Total = 0;
    for (const int32 TaskId : RTD->ActiveTaskIds)
    {
        if (!AllTasks.IsValidIndex(TaskId)) continue;
        const FLogisticsTask& Task = AllTasks[TaskId];
        if (Task.PickupEntity == SupplyEntity &&
            (Task.State == ELogisticsTaskState::Dispatched ||
                Task.State == ELogisticsTaskState::InTransit_Pickup))
        {
            Total += Task.TransferQuantity;
        }
    }
    return Total;
}

// 
//  设备创建 / 销毁
// 

FDroneHandle UMassDspLogisticsSubsystem::CreateDrone(
    FMassEntityHandle AffiliatedTowerEntity, const FVector& InitialLocation, float FlightSpeed, int32 CarryCapacity)
{
    FDroneData Data;
    Data.AffiliatedTowerEntity = AffiliatedTowerEntity;
    Data.FlightSpeed = FlightSpeed;
    Data.CarryCapacity = CarryCapacity;
    Data.State = ELogisticsDeviceState::Idle;
    // P0~P3 全部初始化为 InitialLocation，确保首次起飞时贝塞尔起点即为悬停位置
    Data.P0 = Data.P1 = Data.P2 = Data.P3 = InitialLocation;
    Data.HomeLocation = InitialLocation; // 归属塔悬停位置，用于 ReturningHome 飞行目标

    const int32 Idx = DronePool.Add(Data);
    // 黄金角（≈137.5°）分布：保证同一塔的多架无人机均匀散布在螺旋轨道上
    DronePool[Idx].IdlePhaseOffset = static_cast<float>(Idx) * 2.399963f;

    // ISM 实例直接放在 InitialLocation，无需后续手动同步
    DronePool[Idx].ISMInstanceIndex = AllocateDroneISMInstance(InitialLocation);
    DronePool[Idx].Generation = 0;

    // 初始 GPU Custom Data（全 Idle 模式，让 GPU WPO 知道从哪里开始螺旋）
    {
        const float GameTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
        WriteDroneCustomData(DronePool[Idx], GameTime);
    }

    // 注册到归属塔
    if (AffiliatedTowerEntity.IsValid())
    {
        FLogisticsTowerRuntimeData& RuntimeData = TowerRuntimeData.FindOrAdd(AffiliatedTowerEntity);
        FDroneHandle Handle{Idx, DronePool[Idx].Generation};
        RuntimeData.AffiliatedDroneHandles.Add(Handle);
    }

    // 加入空闲池
    if (!IdleDroneIndexSet.Contains(Idx))
    {
        IdleDroneIndices.Add(Idx);
        IdleDroneIndexSet.Add(Idx);
    }

    // 同步扩展 VehiclePaths / TrainTrackLUTs 对齐（无人机不需要，但保持数组长度一致性）
    return FDroneHandle{Idx, DronePool[Idx].Generation};
}

FVehicleHandle UMassDspLogisticsSubsystem::CreateVehicle(const FVector& SpawnLocation, int32 CarryCapacity)
{
    FVehicleData Data;
    Data.CurrentLocation = SpawnLocation;
    Data.CarryCapacity = CarryCapacity;

    const int32 Idx = VehiclePool.Add(Data);

    // 扩展路径数组对齐
    while (VehiclePaths.Num() <= Idx) VehiclePaths.AddDefaulted();

    VehiclePool[Idx].ISMInstanceIndex = AllocateVehicleISMInstance(SpawnLocation);
    IdleVehicleIndices.Add(Idx);

    return FVehicleHandle{Idx, VehiclePool[Idx].Generation};
}

FTrainHandle UMassDspLogisticsSubsystem::CreateTrain(int32 TrackSegmentIndex, int32 CarryCapacity)
{
    FTrainData Data;
    Data.TrackSegmentIndex = TrackSegmentIndex;
    Data.CarryCapacity = CarryCapacity;

    const int32 Idx = TrainPool.Add(Data);

    while (TrainTrackLUTs.Num() <= Idx) TrainTrackLUTs.AddDefaulted();

    TrainPool[Idx].ISMInstanceIndex = AllocateTrainISMInstance(FVector::ZeroVector);
    IdleTrainIndices.Add(Idx);

    return FTrainHandle{Idx, TrainPool[Idx].Generation};
}

void UMassDspLogisticsSubsystem::DestroyDrone(FDroneHandle Handle)
{
    if (!Handle.IsValid() || !DronePool.IsValidIndex(Handle.Index)) return;
    FDroneData& Drone = DronePool[Handle.Index];
    if (Drone.Generation != Handle.Generation) return; // 悬空句柄

    FreeDroneISMInstance(Drone.ISMInstanceIndex);
    IdleDroneIndices.RemoveSwap(Handle.Index);
    IdleDroneIndexSet.Remove(Handle.Index);
    Drone.Generation++; // 使旧句柄失效
    DronePool.RemoveAt(Handle.Index);
}

void UMassDspLogisticsSubsystem::DestroyVehicle(FVehicleHandle Handle)
{
    if (!Handle.IsValid() || !VehiclePool.IsValidIndex(Handle.Index)) return;
    FVehicleData& Vehicle = VehiclePool[Handle.Index];
    if (Vehicle.Generation != Handle.Generation) return;

    FreeVehicleISMInstance(Vehicle.ISMInstanceIndex);
    IdleVehicleIndices.Remove(Handle.Index);
    Vehicle.Generation++;
    VehiclePool.RemoveAt(Handle.Index);
}

void UMassDspLogisticsSubsystem::DestroyTrain(FTrainHandle Handle)
{
    if (!Handle.IsValid() || !TrainPool.IsValidIndex(Handle.Index)) return;
    FTrainData& Train = TrainPool[Handle.Index];
    if (Train.Generation != Handle.Generation) return;

    FreeTrainISMInstance(Train.ISMInstanceIndex);
    IdleTrainIndices.Remove(Handle.Index);
    Train.Generation++;
    TrainPool.RemoveAt(Handle.Index);
}

// 
//  策略注册
// 

void UMassDspLogisticsSubsystem::RegisterDispatchStrategy(
    ELogisticsDeviceType Type, TUniquePtr<FLogisticsDeviceDispatchStrategy> Strategy)
{
    DispatchStrategies.Emplace(Type, MoveTemp(Strategy));
}

// 
//  私有：找最近可接受塔
// 

FMassEntityHandle UMassDspLogisticsSubsystem::FindNearestEligibleTower(FMassEntityHandle SourceEntity) const
{
    UMassDspManager* Manager = const_cast<UMassDspLogisticsSubsystem*>(this)->GetDspManager();
    if (!Manager) return FMassEntityHandle();

    UWorld* World = GetWorld();
    if (!World) return FMassEntityHandle();

    FMassEntityManager* EntityManagerPtr = GetEntityManagerSafe(World);
    if (!EntityManagerPtr) return FMassEntityHandle();
    FMassEntityManager& EntityManager = *EntityManagerPtr;
    if (!EntityManager.IsEntityValid(SourceEntity)) return FMassEntityHandle();

    const FTransformFragment* TF = EntityManager.GetFragmentDataPtr<FTransformFragment>(SourceEntity);
    if (!TF) return FMassEntityHandle();

    FMassEntityHandle OutEntity;
    EBuildingType OutType;
    FVector OutLocation;

    const bool bFound = Manager->FindNearestBuilding(
        TF->GetTransform().GetLocation(), 10000.f,
        OutEntity, OutType, OutLocation,
        [](const FVector&) { return true; });

    if (!bFound || OutType != EBuildingType::LogisticsTower) return FMassEntityHandle();

    // 验证塔接受请求
    const FMassDspLogisticsTowerFragment* TowerFrag =
        EntityManager.GetFragmentDataPtr<FMassDspLogisticsTowerFragment>(OutEntity);
    if (!TowerFrag || !TowerFrag->bAcceptsRequests) return FMassEntityHandle();

    return OutEntity;
}

// 
//  私有：全局跨塔请求匹配（DSP 行星内物流风格）
// 

void UMassDspLogisticsSubsystem::MatchPendingRequests()
{
    // 早退优化：脏塔队列空时，再快速检查是否有任何塔存有悬挂请求。
    // 注意：不能只凭 DirtyTowerQueue 判断——当供应因物资不足分批派遣时，
    // 需求侧请求保留在 PendingRequestIds 里但塔不被标记为脏，
    // 此时必须扫描全部有请求的塔，否则剩余无人机永远闲置。
    bool bHasWork = !DirtyTowerQueue.IsEmpty();
    if (!bHasWork)
    {
        for (auto& [Ent, RTD] : TowerRuntimeData)
        {
            if (!RTD.PendingRequestIds.IsEmpty()) { bHasWork = true; break; }
        }
    }
    if (!bHasWork) return;

    UWorld* World = GetWorld();
    if (!World) return;

    FMassEntityManager* EntityManagerPtr = GetEntityManagerSafe(World);
    if (!EntityManagerPtr) return;
    FMassEntityManager& EntityManager = *EntityManagerPtr;

    // ── Step 1：遍历所有有未决请求的塔（不限于脏塔），按 ItemType 分桶 ────────
    // 仅凭脏标记会遗漏"供应不足导致需求分批履行"的塔：
    //   第一批派 4 架后供应耗尽，需求请求保留但塔不再是脏塔；
    //   供应补货后只有供应塔被标脏，需求侧请求就永远找不到匹配。
    TMap<EItemType, TArray<int32>> SupplyByType;
    TMap<EItemType, TArray<int32>> DemandByType;
    int32 TotalSupply = 0, TotalDemand = 0;

    for (auto& [TowerEntity, RuntimeData] : TowerRuntimeData)
    {
        if (RuntimeData.PendingRequestIds.IsEmpty()) continue;
        if (!EntityManager.IsEntityValid(TowerEntity)) continue;

        for (const int32 ReqId : RuntimeData.PendingRequestIds)
        {
            if (!AllRequests.IsValidIndex(ReqId)) continue;
            const FLogisticsRequest& Req = AllRequests[ReqId];

            if (Req.Type == ELogisticsRequestType::Supply)
            {
                SupplyByType.FindOrAdd(Req.ItemType).Add(ReqId);
                ++TotalSupply;
            }
            else
            {
                DemandByType.FindOrAdd(Req.ItemType).Add(ReqId);
                ++TotalDemand;
            }
        }
    }

    UE_LOG(LogTemp, Verbose,
           TEXT("[Logistics] GlobalMatch: Supply=%d Demand=%d IdleDrones=%d DirtyTowers=%d"),
           TotalSupply, TotalDemand, IdleDroneIndices.Num(), DirtyTowerQueue.Num());

    // ── Step 2：逐物品类型配对派遣 ──────────────────────────────────────────
    for (auto& [ItemType, SupplyIds] : SupplyByType)
    {
        TArray<int32>* DemandIds = DemandByType.Find(ItemType);
        if (!DemandIds || DemandIds->IsEmpty()) continue;

        DispatchMatchedPairs(SupplyIds, *DemandIds);
    }

    // ── Step 3：清除脏标记 ───────────────────────────────────────────────────
    for (const FMassEntityHandle TowerEntity : DirtyTowerQueue)
    {
        if (!EntityManager.IsEntityValid(TowerEntity)) continue;
        if (FMassDspLogisticsTowerFragment* TF =
            EntityManager.GetFragmentDataPtr<FMassDspLogisticsTowerFragment>(TowerEntity))
        {
            TF->bDirty = false;
        }
    }
    DirtyTowerQueue.Reset();
    DirtyTowerSet.Reset();
}

void UMassDspLogisticsSubsystem::DispatchMatchedPairs(
    TArray<int32>& SupplyIds,
    TArray<int32>& DemandIds)
{
    UWorld* World = GetWorld();
    FMassEntityManager* EntityManagerPtr = World ? GetEntityManagerSafe(World) : nullptr;
    const float GameTime = World ? World->GetTimeSeconds() : 0.f;

    while (!SupplyIds.IsEmpty() && !DemandIds.IsEmpty() && !IdleDroneIndices.IsEmpty())
    {
        const int32 SupplyId = SupplyIds[0];
        const int32 DemandId = DemandIds[0];

        if (!AllRequests.IsValidIndex(SupplyId) || !AllRequests.IsValidIndex(DemandId))
        {
            SupplyIds.RemoveAt(0);
            DemandIds.RemoveAt(0);
            continue;
        }

        FLogisticsRequest* Supply = &AllRequests[SupplyId];
        FLogisticsRequest* Demand = &AllRequests[DemandId];

        // 取货/送货世界坐标
        FVector PickupLoc   = FVector::ZeroVector;
        FVector DeliveryLoc = FVector::ZeroVector;
        if (EntityManagerPtr)
        {
            if (const FTransformFragment* TF =
                EntityManagerPtr->GetFragmentDataPtr<FTransformFragment>(Supply->SourceEntity))
                PickupLoc = TF->GetTransform().GetLocation();
            if (const FTransformFragment* TF =
                EntityManagerPtr->GetFragmentDataPtr<FTransformFragment>(Demand->SourceEntity))
                DeliveryLoc = TF->GetTransform().GetLocation();
        }

        // 从 Supply 塔 Fragment 读取单架运量上限
        int32 CargoPerDrone = FGameConst::DroneCarryCapacity;
        if (EntityManagerPtr)
        {
            if (const FMassDspLogisticsTowerFragment* STF =
                EntityManagerPtr->GetFragmentDataPtr<FMassDspLogisticsTowerFragment>(
                    Supply->SourceEntity))
                CargoPerDrone = FMath::Max(1, STF->DroneCargoCount);
        }

        // ── 实时重算双侧可用量（请求 Quantity 是提交时快照，可能过时） ──────────
        // 不能简单用 Supply->Quantity / Demand->Quantity，否则旧公式生成的请求
        // 会错误限制实际派遣数量（例如旧公式 Threshold-Inventory=32 只派 4 架，
        // 而实际需求 MaxInventory-Inventory=40 应派 5 架）。
        int32 SupplyRemaining = 0;
        int32 DemandRemaining = 0;

        if (!EntityManagerPtr)
        {
            SupplyIds.RemoveAt(0);
            DemandIds.RemoveAt(0);
            continue;
        }

        // Supply 侧实时可发货量
        {
            const FMassDspLogisticsTowerFragment* STF =
                EntityManagerPtr->GetFragmentDataPtr<FMassDspLogisticsTowerFragment>(Supply->SourceEntity);
            const FMassDspStorageFragment* SSF =
                EntityManagerPtr->GetFragmentDataPtr<FMassDspStorageFragment>(Supply->SourceEntity);
            if (STF && SSF)
            {
                const int32 InTransitFrom = ComputeInTransitFromEntity(Supply->SourceEntity);
                SupplyRemaining = FMath::Max(0,
                    SSF->InventoryCount - STF->RequestThreshold - InTransitFrom);
            }
        }
        if (SupplyRemaining <= 0)
        {
            SupplyIds.RemoveAt(0);
            DemandIds.RemoveAt(0);
            continue;
        }

        // Demand 侧实时可接收量（填满至 MaxInventory）
        {
            const FMassDspStorageFragment* DSF =
                EntityManagerPtr->GetFragmentDataPtr<FMassDspStorageFragment>(Demand->SourceEntity);
            if (DSF)
            {
                const int32 InTransitToDemand = ComputeInTransitToEntity(Demand->SourceEntity);
                DemandRemaining = FMath::Max(0,
                    DSF->MaxInventory - DSF->InventoryCount - InTransitToDemand);
            }
        }
        if (DemandRemaining <= 0)
        {
            SupplyIds.RemoveAt(0);
            DemandIds.RemoveAt(0);
            continue;
        }

        // 前置构建归属候选列表
        TArray<int32> AffiliatedCandidates;
        {
            auto AddAffiliatedIdle = [&](FMassEntityHandle TowerEnt)
            {
                if (const FLogisticsTowerRuntimeData* RTD = TowerRuntimeData.Find(TowerEnt))
                    for (const FDroneHandle& H : RTD->AffiliatedDroneHandles)
                        if (H.IsValid() && IdleDroneIndexSet.Contains(H.Index))
                            AffiliatedCandidates.AddUnique(H.Index);
            };
            AddAffiliatedIdle(Supply->PreferredTowerEntity);
            AddAffiliatedIdle(Demand->PreferredTowerEntity);
        }

        while (SupplyRemaining > 0 && DemandRemaining > 0 && !IdleDroneIndices.IsEmpty())
        {
            const int32 BatchQty = FMath::Min3(SupplyRemaining, DemandRemaining, CargoPerDrone);

            FLogisticsTask Task;
            Task.SupplyRequestId  = SupplyId;
            Task.DemandRequestId  = DemandId;
            Task.State            = ELogisticsTaskState::Pending;
            Task.PickupEntity     = Supply->SourceEntity;
            Task.DeliveryEntity   = Demand->SourceEntity;
            Task.PickupLocation   = PickupLoc;
            Task.DeliveryLocation = DeliveryLoc;
            Task.TransferQuantity = BatchQty;
            Task.DeviceType       = ELogisticsDeviceType::Drone;
            // Bug #2/8 修复：缓存塔实体，使 RemoveActiveFromTower 和 CleanExpiredRequests
            // 不再依赖 AllRequests（后者可能因 CancelRequest 提前删除而失效）
            Task.SupplyTowerEntity = Supply->PreferredTowerEntity;
            Task.DemandTowerEntity = Demand->PreferredTowerEntity;

            // 先分配稳定 TaskId，再传入 TryDispatchTask，确保 InitDeviceForTask
            // 写入 Drone->CurrentTaskId 时拿到的是真实 ID 而非默认 -1
            const int32 TaskId = AllTasks.Add(Task);
            AllTasks[TaskId].TaskId = TaskId;
            Task.TaskId = TaskId; // 同步给局部变量，供 TryDispatchTask 使用

            const TArray<int32>& Candidates =
                AffiliatedCandidates.IsEmpty() ? IdleDroneIndices : AffiliatedCandidates;
            if (!TryDispatchTask(Task, Candidates))
            {
                AllTasks.RemoveAt(TaskId); // 回滚，不泄漏 slot
                break;
            }

            // TryDispatchTask 修改了 Task.State / Task.DevicePoolIndex，同步回 AllTasks
            AllTasks[TaskId].State           = Task.State;
            AllTasks[TaskId].DevicePoolIndex = Task.DevicePoolIndex;

            // 注：已移除错峰起飞（stagger）延迟。
            // stagger 本为视觉优化（避免群起群落），但会制造结构性滞后：
            // 最后一架每轮总是晚 N×0.1s 出发，其他架交货回家再次被派遣时那架仍未完成，
            // 造成「始终有1架在Stagger」的永久循环，需求端也因此少计1架入库。
            // 如需视觉错峰，日后可在渲染层用相位偏移实现，与调度逻辑解耦。

            // 已派出的无人机从归属候选中移除
            AffiliatedCandidates.RemoveSwap(Task.DevicePoolIndex);

            // 挂入两个塔的 ActiveTaskIds
            if (FLogisticsTowerRuntimeData* SRTD = TowerRuntimeData.Find(Supply->PreferredTowerEntity))
                SRTD->ActiveTaskIds.AddUnique(TaskId);
            if (FLogisticsTowerRuntimeData* DRTD = TowerRuntimeData.Find(Demand->PreferredTowerEntity))
                DRTD->ActiveTaskIds.AddUnique(TaskId);

            SupplyRemaining -= BatchQty;
            DemandRemaining -= BatchQty;
        }

        // 无论是否派遣了无人机，都 **保留** 双方的 PendingRequestId。
        // 原因：
        //   - DispatchMatchedPairs 已改为实时重算 SR/DR，请求的 Quantity 快照不再决定派遣量。
        //   - 请求若在 SR=0 时被删除，下一轮必须等 Processor ScanInterval（0.5s）重新提交，
        //     期间归巢的无人机找不到请求而闲置，造成「4派6闲」定型循环。
        //   - 保留请求后，MatchPendingRequests 每帧运行，SR>0 时立即派遣，彻底消除空窗期。
        //   - 请求的自然退出路径：CleanExpiredRequests（30s无活动）或 CancelRequest（主动取消）。
        SupplyIds.RemoveAt(0);
        DemandIds.RemoveAt(0);
    }
}

bool UMassDspLogisticsSubsystem::TryDispatchTask(FLogisticsTask& Task, const TArray<int32>& CandidateIndices)
{
    const TUniquePtr<FLogisticsDeviceDispatchStrategy>* StrategyPtr =
        DispatchStrategies.Find(Task.DeviceType);

    if (!StrategyPtr || !(*StrategyPtr))
    {
        UE_LOG(LogTemp, Warning, TEXT("[Logistics] TryDispatchTask FAIL: no strategy for DeviceType=%d"), (int32)Task.DeviceType);
        return false;
    }

    if (CandidateIndices.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[Logistics] TryDispatchTask FAIL: no idle drones (pool=%d)"), DronePool.Num());
        return false;
    }

    const int32 SelectedIdx = (*StrategyPtr)->SelectBestDeviceIndex(CandidateIndices, Task);
    if (SelectedIdx < 0) return false;

    FDroneData& Drone = DronePool[SelectedIdx];

    Task.DevicePoolIndex = SelectedIdx;
    Task.State = ELogisticsTaskState::Dispatched;

    (*StrategyPtr)->InitDeviceForTask(&Drone, Task);
    Task.State = ELogisticsTaskState::InTransit_Pickup;

    IdleDroneIndices.RemoveSwap(SelectedIdx);
    IdleDroneIndexSet.Remove(SelectedIdx);

    return true;
}

// 
//  私有：设备状态机推进（热路径，无虚调用）
// 

void UMassDspLogisticsSubsystem::UpdateDrones(float DeltaTime)
{
    if (DronePool.Num() == 0) return;

    // ═══════════════════════════════════════════════════════════════════════════
    //  Phase A: 并行计时推进（仅修改数值，禁止 ISM / GameThread API 调用）
    // ═══════════════════════════════════════════════════════════════════════════
    struct FArrivalEvent { int32 DroneIdx; ELogisticsDeviceState ArrivedFrom; };
    TArray<FArrivalEvent> ArrivalEvents;
    FCriticalSection ArrivalLock;

    ParallelFor(DronePool.GetMaxIndex(), [&](int32 DroneIdx)
    {
        if (!DronePool.IsValidIndex(DroneIdx)) return;
        FDroneData& Drone = DronePool[DroneIdx];

        switch (Drone.State)
        {
        case ELogisticsDeviceState::Idle:
            // Idle 状态：CPU 更新计时器供螺旋动画使用，ISM 在 Phase B 中写入
            Drone.ElapsedTime += DeltaTime;
            break;

        case ELogisticsDeviceState::Cooldown:
            Drone.CooldownRemaining -= DeltaTime;
            if (Drone.CooldownRemaining <= 0.f)
            {
                Drone.CooldownRemaining = 0.f;
                FScopeLock Lock(&ArrivalLock);
                ArrivalEvents.Add({DroneIdx, ELogisticsDeviceState::Cooldown});
            }
            break;

        case ELogisticsDeviceState::ReturningHome:
        case ELogisticsDeviceState::MovingToPickup:
        case ELogisticsDeviceState::MovingToDeliver:
        {
            // Bug #1 核心修复：每当 ElapsedTime 首次越过 TotalFlightTime 时投递到达事件。
            // Phase C 在同帧内处理事件并切换状态，下一帧 Phase A 不再进入此分支，不会重复投递。
            const ELogisticsDeviceState ArrivedFromState = Drone.State;
            if (Drone.TotalFlightTime <= SMALL_NUMBER)
            {
                FScopeLock Lock(&ArrivalLock);
                ArrivalEvents.Add({DroneIdx, ArrivedFromState});
                break;
            }
            Drone.ElapsedTime += DeltaTime;
            if (Drone.ElapsedTime >= Drone.TotalFlightTime)
            {
                Drone.ElapsedTime = Drone.TotalFlightTime; // 钳制
                FScopeLock Lock(&ArrivalLock);
                ArrivalEvents.Add({DroneIdx, ArrivedFromState}); // ← 关键修复：到达时投递事件
            }
            break;
        }

        default: break;
        }
    });

    // ═══════════════════════════════════════════════════════════════════════════
    //  Phase B: GPU WPO 路径 —— 仅更新 Custom Data，实例 transform 永驻 HomeLocation
    //  （CPU 螺旋/贝塞尔 UpdateInstanceTransform 已移除，由 WPO shader 全权驱动）
    // ═══════════════════════════════════════════════════════════════════════════
    {
        const float GameTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
        for (auto It = DronePool.CreateIterator(); It; ++It)
        {
            FDroneData& Drone = *It;
            if (Drone.ISMInstanceIndex < 0 || !DroneISM) continue;
            if (Drone.ISMInstanceIndex >= DroneISM->GetInstanceCount()) continue;
            WriteDroneCustomData(Drone, GameTime);
        }
    }

    // ═══════════════════════════════════════════════════════════════════════════
    //  Phase C: 主线程到达事件处理（状态切换 + 派生 ISM 写入）
    // ═══════════════════════════════════════════════════════════════════════════
    for (const FArrivalEvent& Event : ArrivalEvents)
    {
        if (!DronePool.IsValidIndex(Event.DroneIdx)) continue;
        FDroneData& Drone = DronePool[Event.DroneIdx];

        // 仅处理真正到达（ElapsedTime 已被 Phase A 钳制到 TotalFlightTime）
        if ((Event.ArrivedFrom == ELogisticsDeviceState::MovingToPickup  ||
             Event.ArrivedFrom == ELogisticsDeviceState::MovingToDeliver ||
             Event.ArrivedFrom == ELogisticsDeviceState::ReturningHome)
            && Drone.ElapsedTime < Drone.TotalFlightTime - SMALL_NUMBER)
            continue; // 未真正到达，下帧再判

        switch (Event.ArrivedFrom)
        {
        case ELogisticsDeviceState::Cooldown:
            HandleCooldownEnded(Event.DroneIdx);
            break;

        case ELogisticsDeviceState::MovingToPickup:
            OnDroneArrivedAtPickup(Event.DroneIdx);
            break;

        case ELogisticsDeviceState::MovingToDeliver:
            OnDroneArrivedAtDelivery(Event.DroneIdx);
            break;

        case ELogisticsDeviceState::ReturningHome:
            HandleDroneArrivedHome(Event.DroneIdx);
            break;

        default: break;
        }
    }

    // 统一触发渲染刷新（合并所有 UpdateInstanceTransform 为一次 GPU 上传）
    if (DroneISM)
        DroneISM->MarkRenderStateDirty();
}

void UMassDspLogisticsSubsystem::UpdateVehicles(float DeltaTime)
{
    // TODO[VEHICLE]: 地面小车状态机推进
    // - 按 CurrentWaypointIdx 在 VehiclePaths[Idx] 中线性移动
    // - 到达取货点  装货  前往交货点  卸货  Cooldown  Idle
    // - 调用 VehicleISM->UpdateInstanceTransform 同步渲染位置
    (void)DeltaTime;
}

void UMassDspLogisticsSubsystem::UpdateTrains(float DeltaTime)
{
    // TODO[TRAIN]: 火车状态机推进
    // - DistanceAlongTrack += MoveSpeed * DeltaTime（考虑 bForwardDirection）
    // - 到达轨道段端点  区间信号灯判断（闭塞区间调度）
    // - 在 TrainTrackLUTs[Idx] 中查表插值（复用 FBeltTrajectory::GetTransformAtDistance）
    // - 调用 TrainISM->UpdateInstanceTransform 同步渲染位置
    (void)DeltaTime;
}

// 
//  私有：无人机到达回调
// 

void UMassDspLogisticsSubsystem::OnDroneArrivedAtPickup(int32 DronePoolIndex)
{
    FDroneData& Drone = DronePool[DronePoolIndex];

    // 从取货建筑搬走物品
    bool bPickupSuccess = false;
    if (UWorld* World = GetWorld())
    {
        FMassEntityManager* EMPtr = GetEntityManagerSafe(World);
        if (EMPtr)
            if (FMassDspStorageFragment* StorageFrag =
                EMPtr->GetFragmentDataPtr<FMassDspStorageFragment>(Drone.PickupEntity))
            {
                // 先记录物品类型（扣完后 StoredItemType 可能被清 None）
                const EItemType ItemType = StorageFrag->StoredItemType;
                if (ItemType != EItemType::None)
                {
                    // 取货数量 = min(无人机容量, 任务请求量)
                    int32 WantQty = Drone.CarryCapacity;
                    if (AllTasks.IsValidIndex(Drone.CurrentTaskId))
                        WantQty = FMath::Min(WantQty, AllTasks[Drone.CurrentTaskId].TransferQuantity);

                    const int32 Taken = StorageFrag->TryProvideItems(WantQty);
                    if (Taken > 0)
                    {
                        Drone.CarriedItemType = ItemType;
                        Drone.CarriedQuantity = Taken;
                        // Bug #6 修复：同步实际取货量回任务，确保 ComputeInTransitToEntity 准确
                        if (AllTasks.IsValidIndex(Drone.CurrentTaskId))
                            AllTasks[Drone.CurrentTaskId].TransferQuantity = Taken;
                        bPickupSuccess = true;
                    }
                }
            }
    }

    // Bug #7 修复：取货失败（Supply 端库存为空）时中止任务，不再空飞到交货点。
    // 注意：失败时进入 Cooldown（而非直接 Idle），给矿机时间补充货物。
    // 若直接 Idle，下帧立刻再次被派遣→再次取货失败，形成死循环（「1架永远在家」的根因之一）。
    if (!bPickupSuccess)
    {
        const int32 FailedTaskId = Drone.CurrentTaskId;

        // 清除无人机任务关联
        Drone.CarriedItemType = EItemType::None;
        Drone.CarriedQuantity = 0;
        Drone.CurrentTaskId   = -1;

        // 进入短暂 Cooldown，给矿机时间补货（0.5s 约等于矿机几个出货周期）
        Drone.State             = ELogisticsDeviceState::Cooldown;
        Drone.CooldownRemaining = 0.5f;

        // 从两个协调塔的 ActiveTaskIds 中清除
        if (AllTasks.IsValidIndex(FailedTaskId))
        {
            AllTasks[FailedTaskId].State = ELogisticsTaskState::Failed;
            const FLogisticsTask& FailedTask = AllTasks[FailedTaskId];
            if (FLogisticsTowerRuntimeData* RTD = TowerRuntimeData.Find(FailedTask.SupplyTowerEntity))
                RTD->ActiveTaskIds.Remove(FailedTaskId);
            if (FLogisticsTowerRuntimeData* RTD = TowerRuntimeData.Find(FailedTask.DemandTowerEntity))
                RTD->ActiveTaskIds.Remove(FailedTaskId);
        }
        return;
    }

    // 切换到飞往交货点，同步更新任务状态
    Drone.State = ELogisticsDeviceState::MovingToDeliver;
    Drone.ElapsedTime = 0.f;
    if (AllTasks.IsValidIndex(Drone.CurrentTaskId))
        AllTasks[Drone.CurrentTaskId].State = ELogisticsTaskState::InTransit_Deliver;

    // 重新生成贝塞尔曲线（取货点 → 交货点），向自身行进方向右偏形成回程航道
    const FVector P0 = Drone.P3; // 当前位置（上一段终点）
    const FVector P3 = Drone.DeliveryLocation;
    const float Arc = FGameConst::DroneFlightArcHeight;
    const float Lane = FGameConst::DroneFlightLaneOffset;

    // 弦长自适应：防止短弦时控制点远超弦长造成循环扁曲
    const float Dist = FVector::Dist(P0, P3);
    const float EffArc = FMath::Min(Arc, Dist * 0.4f);
    const float EffLane = FMath::Min(Lane, Dist * 0.3f);

    const FVector Dir2D = (P3 - P0).GetSafeNormal2D();
    // 对 Dir2D 做 90° 顺时针旋转（俯视）得到右方向：(dx,dy) → (dy,-dx)
    // 保证去程/回程都偏向各自行进方向的右侧，两条航道在世界空间中分离
    const FVector RightXY = Dir2D.IsNearlyZero()
                                ? FVector::RightVector
                                : FVector(Dir2D.Y, -Dir2D.X, 0.f);

    Drone.P0 = P0;
    Drone.P1 = P0 + FVector(0.f, 0.f, EffArc) + RightXY * EffLane;
    Drone.P2 = P3 + FVector(0.f, 0.f, EffArc) + RightXY * EffLane;
    Drone.P3 = P3;
    Drone.TotalFlightTime = Dist / FMath::Max(1.f, Drone.FlightSpeed);
}

void UMassDspLogisticsSubsystem::OnDroneArrivedAtDelivery(int32 DronePoolIndex)
{
    FDroneData& Drone = DronePool[DronePoolIndex];

    // 将携带物品批量交给目标建筑
    if (UWorld* World = GetWorld())
    {
        FMassEntityManager* EMPtr = GetEntityManagerSafe(World);
        if (EMPtr)
            if (FMassDspStorageFragment* StorageFrag =
                EMPtr->GetFragmentDataPtr<FMassDspStorageFragment>(Drone.DeliveryEntity))
            {
                StorageFrag->TryConsumeItems(Drone.CarriedItemType, Drone.CarriedQuantity);
            }
    }

    // 先保存 TaskId，再清空无人机状态（避免提前清零导致找不到任务）
    const int32 CompletedTaskId = Drone.CurrentTaskId;

    Drone.CarriedItemType = EItemType::None;
    Drone.CarriedQuantity = 0;
    Drone.CurrentTaskId   = -1;
    Drone.State           = ELogisticsDeviceState::Cooldown;
    Drone.CooldownRemaining = Drone.CooldownDuration;

    // O(1) 直接用 TaskId 查找并标记完成（不再全表扫描）
    if (!AllTasks.IsValidIndex(CompletedTaskId)) return;
    {
        FLogisticsTask* Task = &AllTasks[CompletedTaskId];
        Task->State = ELogisticsTaskState::Completed;

        // 任务完成后把 Supply/Demand 请求重新加回协调塔的 PendingRequestIds，
        // 确保冷却结束时 MatchPendingRequests 有内容可匹配（不等 Processor 下次扫描）
        const float NowTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
        FMassEntityManager* EMForRequeue = GetWorld() ? GetEntityManagerSafe(GetWorld()) : nullptr;

        // 在途容量感知的重入队逻辑
        auto RequeueRequest = [&](int32 ReqId)
        {
            if (!AllRequests.IsValidIndex(ReqId)) return;
            FLogisticsRequest& Req = AllRequests[ReqId];
            Req.RequestTime = NowTime;

            // 优先通过 Fragment 计算实际有效数量
            int32 EffectiveQty = 0;
            if (EMForRequeue)
            {
                const FMassDspLogisticsTowerFragment* TFrag =
                    EMForRequeue->GetFragmentDataPtr<FMassDspLogisticsTowerFragment>(Req.SourceEntity);
                const FMassDspStorageFragment* SFrag =
                    EMForRequeue->GetFragmentDataPtr<FMassDspStorageFragment>(Req.SourceEntity);
                if (TFrag && SFrag)
                {
                    if (Req.Type == ELogisticsRequestType::Supply)
                    {
                        const int32 InTransitFrom = ComputeInTransitFromEntity(Req.SourceEntity);
                        EffectiveQty = FMath::Max(0, SFrag->InventoryCount - TFrag->RequestThreshold - InTransitFrom);
                    }
                    else
                    {
                        // Demand 重入队：目标是填满至 MaxInventory，而非仅到 RequestThreshold
                        const int32 InTransitTo = ComputeInTransitToEntity(Req.SourceEntity);
                        EffectiveQty = FMath::Max(0, SFrag->MaxInventory - SFrag->InventoryCount - InTransitTo);
                    }
                }
            }
            else
            {
                EffectiveQty = Req.Quantity;
            }

            if (EffectiveQty <= 0) return;
            Req.Quantity = EffectiveQty;

            const FMassEntityHandle CoordTower = Req.PreferredTowerEntity;
            if (!CoordTower.IsValid()) return;
            FLogisticsTowerRuntimeData* RTD = TowerRuntimeData.Find(CoordTower);
            if (!RTD) return;
            if (!RTD->PendingRequestIds.Contains(ReqId))
                RTD->PendingRequestIds.Add(ReqId);

            if (EMForRequeue)
                if (FMassDspLogisticsTowerFragment* TFrag2 =
                    EMForRequeue->GetFragmentDataPtr<FMassDspLogisticsTowerFragment>(CoordTower))
                {
                    TFrag2->bDirty = true;
                    EnqueueDirtyTower(CoordTower);
                }
        };
        RequeueRequest(Task->SupplyRequestId);
        RequeueRequest(Task->DemandRequestId);

        // 仅从涉及的两个塔中移除 ActiveTaskIds，避免 O(NumAllTowers) 全量遍历
        // Bug #2/8 修复：直接使用 Task 中缓存的塔实体，不再依赖可能已被 CancelRequest 删除的 AllRequests
        auto RemoveActiveFromTower = [&](FMassEntityHandle TowerEntity)
        {
            if (!TowerEntity.IsValid()) return;
            if (FLogisticsTowerRuntimeData* RTD = TowerRuntimeData.Find(TowerEntity))
                RTD->ActiveTaskIds.Remove(CompletedTaskId);
        };
        RemoveActiveFromTower(Task->SupplyTowerEntity);
        RemoveActiveFromTower(Task->DemandTowerEntity);
    }
}

void UMassDspLogisticsSubsystem::OnDroneTaskFailed(int32 DronePoolIndex)
{
    FDroneData& Drone = DronePool[DronePoolIndex];
    Drone.CarriedItemType = EItemType::None;
    Drone.CarriedQuantity = 0;
    const int32 OldTaskId = Drone.CurrentTaskId;
    Drone.CurrentTaskId   = -1;
    Drone.State           = ELogisticsDeviceState::Idle;
    // Bug #10 修复：使用 Set 去重，防止 TArray 中出现重复索引导致同一无人机被派遣两次
    if (!IdleDroneIndexSet.Contains(DronePoolIndex))
    {
        IdleDroneIndices.Add(DronePoolIndex);
        IdleDroneIndexSet.Add(DronePoolIndex);
    }

    if (AllTasks.IsValidIndex(OldTaskId))
        AllTasks[OldTaskId].State = ELogisticsTaskState::Failed;
}

// 
//  私有：ISM 同步
// 

void UMassDspLogisticsSubsystem::UpdateDroneISMInstance(FDroneData& Drone) const
{
    if (!DroneISM || Drone.ISMInstanceIndex < 0) return;
    // ISM 实例数量越界保护
    if (Drone.ISMInstanceIndex >= DroneISM->GetInstanceCount()) return;

    const float t = (Drone.TotalFlightTime > 0.f)
                        ? FMath::Clamp(Drone.ElapsedTime / Drone.TotalFlightTime, 0.f, 1.f)
                        : 0.f;
    const FVector Pos = Drone.EvalBezier(t);

    // 朝向：贝塞尔一阶导数（切线方向）
    const float Dt = 0.01f;
    const float tFwd = FMath::Clamp(t + Dt, 0.f, 1.f);
    const FVector Fwd = (Drone.EvalBezier(tFwd) - Pos).GetSafeNormal();
    const FQuat Rot = Fwd.IsNearlyZero() ? FQuat::Identity : FRotationMatrix::MakeFromX(Fwd).ToQuat();

    const FTransform InstanceTransform(Rot, Pos, FVector::OneVector);
    // bMarkRenderStateDirty=false：由 UpdateDrones() 结尾统一调用 MarkRenderStateDirty() 一次性刷新
    DroneISM->UpdateInstanceTransform(Drone.ISMInstanceIndex, InstanceTransform,
                                      /*bWorldSpace=*/true, /*bMarkRenderStateDirty=*/false);
}

int32 UMassDspLogisticsSubsystem::AllocateDroneISMInstance(const FVector& InitialLocation)
{
    if (!DroneISM)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Logistics] AllocateDroneISMInstance: DroneISM is null!"));
        return -1;
    }
    const int32 Idx = DroneISM->AddInstance(
        FTransform(FQuat::Identity, InitialLocation, FVector::OneVector));
    UE_LOG(LogTemp, Verbose, TEXT("[Logistics] ISM instance allocated: idx=%d total=%d"),
           Idx, DroneISM->GetInstanceCount());
    return Idx;
}

int32 UMassDspLogisticsSubsystem::AllocateVehicleISMInstance(const FVector& InitialLocation)
{
    if (!VehicleISM) return -1;
    return VehicleISM->AddInstance(FTransform(FQuat::Identity, InitialLocation, FVector::OneVector));
}

int32 UMassDspLogisticsSubsystem::AllocateTrainISMInstance(const FVector& InitialLocation)
{
    if (!TrainISM) return -1;
    return TrainISM->AddInstance(FTransform(FQuat::Identity, InitialLocation, FVector::OneVector));
}

void UMassDspLogisticsSubsystem::FreeDroneISMInstance(int32 InstanceIndex)
{
    if (!DroneISM || InstanceIndex < 0) return;

    // Bug 渲染 #3 修复：RemoveInstance 会把最后一个实例 swap 到 InstanceIndex 位置，
    // 必须先更新对应无人机的 ISMInstanceIndex，否则其 Custom Data 写入会错位。
    const int32 LastISMIndex = DroneISM->GetInstanceCount() - 1;
    if (LastISMIndex > InstanceIndex)
    {
        // 找出 ISMInstanceIndex == LastISMIndex 的无人机，将其更新为 InstanceIndex（swap 目标）
        for (auto It = DronePool.CreateIterator(); It; ++It)
        {
            if (It->ISMInstanceIndex == LastISMIndex)
            {
                It->ISMInstanceIndex = InstanceIndex;
                break;
            }
        }
    }

    DroneISM->RemoveInstance(InstanceIndex);
}

void UMassDspLogisticsSubsystem::FreeVehicleISMInstance(int32 InstanceIndex)
{
    if (VehicleISM && InstanceIndex >= 0)
        VehicleISM->RemoveInstance(InstanceIndex);
}

void UMassDspLogisticsSubsystem::FreeTrainISMInstance(int32 InstanceIndex)
{
    if (TrainISM && InstanceIndex >= 0)
        TrainISM->RemoveInstance(InstanceIndex);
}

// 
//  私有：超时清理 & 工具
// 

void UMassDspLogisticsSubsystem::CleanExpiredRequests()
{
    UWorld* World = GetWorld();
    if (!World) return;

    const float Now = World->GetTimeSeconds();
    TArray<int32> ToRemove;

    for (int32 i = 0; i < AllRequests.GetMaxIndex(); ++i)
    {
        if (!AllRequests.IsValidIndex(i)) continue;
        const FLogisticsRequest& Req = AllRequests[i];
        if (Now - Req.RequestTime > Req.ExpiryDuration)
            ToRemove.Add(i);
    }

    for (int32 Id : ToRemove)
        CancelRequest(Id);

    // 清理 AllTasks 中的终态条目，防止历史记录无限增长
    TArray<int32> TasksToRemove;
    for (int32 i = 0; i < AllTasks.GetMaxIndex(); ++i)
    {
        if (!AllTasks.IsValidIndex(i)) continue;
        const ELogisticsTaskState S = AllTasks[i].State;
        if (S == ELogisticsTaskState::Completed ||
            S == ELogisticsTaskState::Failed    ||
            S == ELogisticsTaskState::Cancelled)
            TasksToRemove.Add(i);
    }
    for (int32 Id : TasksToRemove)
    {
        // Bug #3 修复：删除任务前先清理两端协调塔的 ActiveTaskIds，防止残留无效 ID
        // 导致 ComputeInTransitToEntity 统计错误 + TSparseArray 槽复用时读到旧任务数据
        const FLogisticsTask& DeadTask = AllTasks[Id];
        if (FLogisticsTowerRuntimeData* RTD = TowerRuntimeData.Find(DeadTask.SupplyTowerEntity))
            RTD->ActiveTaskIds.Remove(Id);
        if (FLogisticsTowerRuntimeData* RTD = TowerRuntimeData.Find(DeadTask.DemandTowerEntity))
            RTD->ActiveTaskIds.Remove(Id);
        AllTasks.RemoveAt(Id);
    }
}

UMassDspManager* UMassDspLogisticsSubsystem::GetDspManager()
{
    if (!CachedDspManager.IsValid())
    {
        if (UWorld* World = GetWorld())
            CachedDspManager = World->GetSubsystem<UMassDspManager>();
    }
    return CachedDspManager.Get();
}

void UMassDspLogisticsSubsystem::UpdateDroneInGrid(
    int32 DronePoolIndex, const FVector& OldPos, const FVector& NewPos)
{
    const int32 OldCX = FMath::FloorToInt(OldPos.X / FGameConst::DroneGridCellSize);
    const int32 OldCY = FMath::FloorToInt(OldPos.Y / FGameConst::DroneGridCellSize);
    const int32 NewCX = FMath::FloorToInt(NewPos.X / FGameConst::DroneGridCellSize);
    const int32 NewCY = FMath::FloorToInt(NewPos.Y / FGameConst::DroneGridCellSize);

    if (OldCX == NewCX && OldCY == NewCY) return; // 格子未变，跳过

    // 从旧格移除
    const uint64 OldKey = MakeDroneCellKey(OldCX, OldCY);
    if (TArray<int32>* OldCell = DroneGridCells.Find(OldKey))
        OldCell->Remove(DronePoolIndex);

    // 加入新格
    const uint64 NewKey = MakeDroneCellKey(NewCX, NewCY);
    DroneGridCells.FindOrAdd(NewKey).AddUnique(DronePoolIndex);
}

bool UMassDspLogisticsSubsystem::ExecuteItemTransfer(
    FMassEntityHandle PickupEntity, FMassEntityHandle DeliveryEntity,
    EItemType& InOutItemType, int32& InOutQuantity)
{
    // 此方法供 TODO[VEHICLE] / TODO[TRAIN] 复用
    UWorld* World = GetWorld();
    if (!World) return false;

    FMassEntityManager* EMPtr = GetEntityManagerSafe(World);
    if (!EMPtr) return false;
    FMassEntityManager& EM = *EMPtr;

    FMassDspStorageFragment* PickFrag = EM.GetFragmentDataPtr<FMassDspStorageFragment>(PickupEntity);
    FMassDspStorageFragment* DelFrag = EM.GetFragmentDataPtr<FMassDspStorageFragment>(DeliveryEntity);

    if (!PickFrag || !DelFrag) return false;

    const EItemType Provided = PickFrag->TryProvideItemToSlot(0);
    if (Provided == EItemType::None) return false;

    InOutItemType = Provided;
    InOutQuantity = 1;

    if (!DelFrag->TryConsumeItemFromSlot(Provided))
    {
        // 交货失败（目标已满），回退
        PickFrag->TryConsumeItemFromSlot(Provided); // 实际是还回去
        return false;
    }

    return true;
}

// 
//  新增：无人机 ISM GPU Custom Data 写入（为 GPU WPO 材质预留）
// 

void UMassDspLogisticsSubsystem::WriteDroneCustomData(const FDroneData& Drone, float GameTime) const
{
    // NOTE: 本函数预留给 GPU WPO 阶段（Phase 5）使用。
    if (!DroneISM || Drone.ISMInstanceIndex < 0) return;
    if (DroneISM->NumCustomDataFloats < 18) return; // Custom Data floats 尚未就绪

    const int32 Idx  = Drone.ISMInstanceIndex;
    // 错峰起飞修复：待命止点期间（ElapsedTime < 0）无人机尚未离塔，强制以 Idle 模式输出
    // （GPU 在 HomeLocation 做螺旋动画，而不是冻结在 P0 = 供应塔起点）
    const bool bStaggerWaiting = (Drone.ElapsedTime < -SMALL_NUMBER);
    const bool bIdle = (Drone.State == ELogisticsDeviceState::Idle) || bStaggerWaiting;

    // Bug #11 修复：错峰起飞时 ElapsedTime 为负（stagger 延迟），用 max(0,ElapsedTime) 参与计算，
    // 确保 GPU 端 t = (currentGameTime - TimeAtDispatch) / TotalFlightTime >= 0，避免贝塞尔外推。
    // 当 ElapsedTime < 0 时无人机在 GPU 上停在 P0 位置，stagger 倒计时结束后再起飞。
    const float ClampedElapsed = FMath::Max(0.f, Drone.ElapsedTime);
    DroneISM->SetCustomDataValue(Idx, 0,  bIdle ? 0.f : (GameTime - ClampedElapsed)); // TimeAtDispatch
    DroneISM->SetCustomDataValue(Idx, 1,  bIdle ? 0.f : Drone.TotalFlightTime);           // TotalFlightTime
    DroneISM->SetCustomDataValue(Idx, 2,  Drone.P0.X);
    DroneISM->SetCustomDataValue(Idx, 3,  Drone.P0.Y);
    DroneISM->SetCustomDataValue(Idx, 4,  Drone.P0.Z);
    DroneISM->SetCustomDataValue(Idx, 5,  Drone.P1.X);
    DroneISM->SetCustomDataValue(Idx, 6,  Drone.P1.Y);
    DroneISM->SetCustomDataValue(Idx, 7,  Drone.P1.Z);
    DroneISM->SetCustomDataValue(Idx, 8,  Drone.P2.X);
    DroneISM->SetCustomDataValue(Idx, 9,  Drone.P2.Y);
    DroneISM->SetCustomDataValue(Idx, 10, Drone.P2.Z);
    // [11-13]: 永远是 HomeLocation（Idle 螺旋圆心）
    DroneISM->SetCustomDataValue(Idx, 11, Drone.HomeLocation.X);
    DroneISM->SetCustomDataValue(Idx, 12, Drone.HomeLocation.Y);
    DroneISM->SetCustomDataValue(Idx, 13, Drone.HomeLocation.Z);
    DroneISM->SetCustomDataValue(Idx, 14, Drone.IdlePhaseOffset);
    // [15-17]: 飞行终点 P3（仅飞行时有意义，Idle 时写 0）
    const FVector P3Val = bIdle ? FVector::ZeroVector : Drone.P3;
    DroneISM->SetCustomDataValue(Idx, 15, P3Val.X);
    DroneISM->SetCustomDataValue(Idx, 16, P3Val.Y);
    DroneISM->SetCustomDataValue(Idx, 17, P3Val.Z);
}

// 
//  新增：脏塔入队（O(1) TSet 去重 + TArray 保序）
// 

void UMassDspLogisticsSubsystem::EnqueueDirtyTower(FMassEntityHandle TowerEntity)
{
    if (!TowerEntity.IsValid()) return;
    if (!DirtyTowerSet.Contains(TowerEntity))
    {
        DirtyTowerSet.Add(TowerEntity);
        DirtyTowerQueue.Add(TowerEntity);
    }
}

// 
//  新增：CoolDown 结束处理（从 UpdateDrones Phase C 中提取）
// 

void UMassDspLogisticsSubsystem::HandleCooldownEnded(int32 DroneIdx)
{
    if (!DronePool.IsValidIndex(DroneIdx)) return;
    FDroneData& Drone = DronePool[DroneIdx];

    // 提前提交归属塔的请求（在返航途中就让系统准备好配对），
    // 落地变 Idle 后 bDirty 会再次触发 MatchPendingRequests 完成派遣。
    if (Drone.AffiliatedTowerEntity.IsValid())
    {
        if (UWorld* W = GetWorld())
        {
            if (FMassEntityManager* EMPtr = GetEntityManagerSafe(W))
            {
                const FMassEntityHandle TowerEnt = Drone.AffiliatedTowerEntity;
                const FMassDspLogisticsTowerFragment* TFrag =
                    EMPtr->GetFragmentDataPtr<FMassDspLogisticsTowerFragment>(TowerEnt);
                const FMassDspStorageFragment* SFrag =
                    EMPtr->GetFragmentDataPtr<FMassDspStorageFragment>(TowerEnt);
                if (TFrag && SFrag && TFrag->bAcceptsRequests && TFrag->ItemType != EItemType::None)
                {
                    if (TFrag->TowerMode == ELogisticsTowerMode::Supply)
                    {
                        const int32 InTransitFrom = ComputeInTransitFromEntity(TowerEnt);
                        const int32 EffQty = FMath::Max(0,
                            SFrag->InventoryCount - TFrag->RequestThreshold - InTransitFrom);
                        if (EffQty > 0)
                            SubmitSupplyRequest(TowerEnt, TFrag->ItemType, EffQty,
                                                ELogisticsRequestPriority::Normal, TowerEnt);
                    }
                    else if (TFrag->TowerMode == ELogisticsTowerMode::Demand)
                    {
                        // Demand 模式冷却后重新请求：触发条件与 Processor 一致（库存低于阈值），
                        // 请求数量填满至 MaxInventory
                        if (SFrag->InventoryCount < TFrag->RequestThreshold)
                        {
                            const int32 InTransitTo = ComputeInTransitToEntity(TowerEnt);
                            const int32 EffQty = FMath::Max(0,
                                SFrag->MaxInventory - SFrag->InventoryCount - InTransitTo);
                            if (EffQty > 0)
                                SubmitDemandRequest(TowerEnt, TFrag->ItemType, EffQty,
                                                    ELogisticsRequestPriority::Normal, TowerEnt);
                        }
                    }
                }
            }
        }
    }

    // ── 开始返回归属塔 ──────────────────────────────────────────────────────────
    const FVector HomePos = Drone.HomeLocation;
    const FVector CurPos  = Drone.P3; // 当前停留位置（上一段贝塞尔终点）
    const float HomeDist  = FVector::Dist(CurPos, HomePos);

    if (Drone.AffiliatedTowerEntity.IsValid() && HomeDist > 50.f)
    {
        const float Arc    = FGameConst::DroneFlightArcHeight;
        const float EffArc = FMath::Min(Arc, HomeDist * 0.4f);

        Drone.P0 = CurPos;
        Drone.P1 = CurPos  + FVector(0.f, 0.f, EffArc);
        Drone.P2 = HomePos + FVector(0.f, 0.f, EffArc);
        Drone.P3 = HomePos;
        Drone.ElapsedTime     = 0.f;
        Drone.TotalFlightTime = HomeDist / FMath::Max(1.f, Drone.FlightSpeed);
        Drone.State           = ELogisticsDeviceState::ReturningHome;
    }
    else
    {
        // 已在家或无归属塔，直接 Idle
        HandleDroneArrivedHome(DroneIdx);
    }
}

// 
//  新增：无人机到家处理（ReturningHome 到达 + Cooldown 已在家时共用）
// 

void UMassDspLogisticsSubsystem::HandleDroneArrivedHome(int32 DroneIdx)
{
    if (!DronePool.IsValidIndex(DroneIdx)) return;
    FDroneData& Drone = DronePool[DroneIdx];

    Drone.State       = ELogisticsDeviceState::Idle;
    Drone.ElapsedTime = 0.f; // 重置，使螺旋动画从初始相位平滑开始

    // Bug #10 修复：使用 Set 去重，防止 TArray 中出现重复索引导致同一无人机被派遣两次
    if (!IdleDroneIndexSet.Contains(DroneIdx))
    {
        IdleDroneIndices.Add(DroneIdx);
        IdleDroneIndexSet.Add(DroneIdx);
    }

    // 置脏让 MatchPendingRequests 下一帧处理
    if (Drone.AffiliatedTowerEntity.IsValid())
    {
        if (UWorld* W = GetWorld())
        {
            if (FMassEntityManager* EMPtr = GetEntityManagerSafe(W))
            {
                if (FMassDspLogisticsTowerFragment* TFrag =
                    EMPtr->GetFragmentDataPtr<FMassDspLogisticsTowerFragment>(Drone.AffiliatedTowerEntity))
                {
                    TFrag->bDirty = true;
                    EnqueueDirtyTower(Drone.AffiliatedTowerEntity);
                }
            }
        }
    }
}
