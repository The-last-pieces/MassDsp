#include "Subsystems/MassDspLogisticsSubsystem.h"

#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassCommonFragments.h"    // FTransformFragment
#include "Subsystems/MassDspManager.h"
#include "Fragments/MassDspStorageFragment.h"
#include "Fragments/MassDspLogisticsTowerFragment.h"

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
    AllRequests.Reserve(512);
    AllTasks.Reserve(256);
    TowerRuntimeData.Reserve(64);
    IdleDroneIndices.Reserve(1024);
    IdleVehicleIndices.Reserve(128);
    IdleTrainIndices.Reserve(64);
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
}

// 
//  Tick：三步流水线
// 

void UMassDspLogisticsSubsystem::Tick(float DeltaTime)
{
    // Step 1：推进所有设备状态机 + 批量同步 ISM
    UpdateDrones(DeltaTime);
    UpdateVehicles(DeltaTime);
    UpdateTrains(DeltaTime);

    // Step 2：处理脏塔的请求匹配与任务派发
    MatchPendingRequests();

    // Step 3：清理超时请求
    CleanExpiredRequests();

    // 调试：每秒输出一次控制台状态
    static float DbgAccum = 0.f;
    DbgAccum += DeltaTime;
    if (DbgAccum >= 1.f)
    {
        DbgAccum = 0.f;
        UE_LOG(LogTemp, Log,
            TEXT("[Logistics Tick] Drones=%d Idle=%d Requests=%d Tasks=%d TowerRTD=%d"),
            DronePool.Num(), IdleDroneIndices.Num(),
            AllRequests.Num(), AllTasks.Num(), TowerRuntimeData.Num());
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(42, 2.f, FColor::Cyan,
                FString::Printf(TEXT("[Logistics] Drones:%d Idle:%d Req:%d Tasks:%d Strategies:%d"),
                    DronePool.Num(), IdleDroneIndices.Num(),
                    AllRequests.Num(), AllTasks.Num(), DispatchStrategies.Num()));
        }
    }
}

// 
//  请求接口
// 

FGuid UMassDspLogisticsSubsystem::SubmitSupplyRequest(
    FMassEntityHandle SourceEntity, EItemType ItemType, int32 Quantity,
    ELogisticsRequestPriority Priority, FMassEntityHandle PreferredTowerEntity)
{
    return SubmitRequestInternal(ELogisticsRequestType::Supply,
        SourceEntity, ItemType, Quantity, Priority, PreferredTowerEntity);
}

FGuid UMassDspLogisticsSubsystem::SubmitDemandRequest(
    FMassEntityHandle SourceEntity, EItemType ItemType, int32 Quantity,
    ELogisticsRequestPriority Priority, FMassEntityHandle PreferredTowerEntity)
{
    return SubmitRequestInternal(ELogisticsRequestType::Demand,
        SourceEntity, ItemType, Quantity, Priority, PreferredTowerEntity);
}

FGuid UMassDspLogisticsSubsystem::SubmitRequestInternal(
    ELogisticsRequestType Type,
    FMassEntityHandle SourceEntity, EItemType ItemType, int32 Quantity,
    ELogisticsRequestPriority Priority,
    FMassEntityHandle PreferredTowerEntity)
{
    // 找目标塔
    FMassEntityHandle TowerEntity = PreferredTowerEntity.IsValid()
        ? PreferredTowerEntity
        : FindNearestEligibleTower(SourceEntity);

    if (!TowerEntity.IsValid()) return FGuid();

    // 检查塔是否接受请求
    UWorld* World = GetWorld();
    if (!World) return FGuid();

    FMassEntityManager* EntityManagerPtr = GetEntityManagerSafe(World);
    if (!EntityManagerPtr) return FGuid();
    FMassEntityManager& EntityManager = *EntityManagerPtr;
    if (!EntityManager.IsEntityValid(TowerEntity)) return FGuid();

    FMassDspLogisticsTowerFragment* TowerFrag =
        EntityManager.GetFragmentDataPtr<FMassDspLogisticsTowerFragment>(TowerEntity);
    if (!TowerFrag || !TowerFrag->bAcceptsRequests) return FGuid();

    // ── 去重：同一 (SourceEntity, ItemType, Type, Tower) 已有请求则更新数量而非新增 ──
    FLogisticsTowerRuntimeData& RuntimeDataRef = TowerRuntimeData.FindOrAdd(TowerEntity);
    for (auto& [ExistId, ExistReq] : AllRequests)
    {
        if (ExistReq.Type                == Type
         && ExistReq.SourceEntity        == SourceEntity
         && ExistReq.ItemType            == ItemType
         && ExistReq.PreferredTowerEntity == TowerEntity)
        {
            // 更新数量和时间戳
            ExistReq.Quantity    = FMath::Max(1, Quantity);
            ExistReq.RequestTime = World->GetTimeSeconds();
            // 关键：若请求已被配对从 PendingRequestIds 移除，且当前无活跃任务引用它，
            // 才重新加回；否则无人机首次送货后再无新任务可以匹配
            const bool bAlreadyPending = RuntimeDataRef.PendingRequestIds.Contains(ExistId);
            if (!bAlreadyPending)
            {
                // 检查是否有活跃任务正在使用该请求（运输中不重新入队，避免重复派遣）
                bool bHasActiveTask = false;
                for (const FGuid& ActiveId : RuntimeDataRef.ActiveTaskIds)
                {
                    if (const FLogisticsTask* T = AllTasks.Find(ActiveId))
                    {
                        if (T->SupplyRequestId == ExistId || T->DemandRequestId == ExistId)
                        {
                            bHasActiveTask = true;
                            break;
                        }
                    }
                }
                if (!bHasActiveTask)
                {
                    RuntimeDataRef.PendingRequestIds.Add(ExistId);
                    UE_LOG(LogTemp, Verbose,
                        TEXT("[Logistics] Re-queue request %s (prev task completed)"),
                        *ExistId.ToString());
                }
            }
            TowerFrag->bDirty = true;
            return ExistId;
        }
    }

    // 构造请求
    FLogisticsRequest Req;
    Req.RequestId              = FGuid::NewGuid();
    Req.Type                   = Type;
    Req.SourceEntity           = SourceEntity;
    Req.ItemType               = ItemType;
    Req.Quantity               = FMath::Max(1, Quantity);
    Req.Priority               = Priority;
    Req.PreferredTowerEntity   = TowerEntity;
    Req.RequestTime            = World->GetTimeSeconds();
    Req.ExpiryDuration         = 30.f;

    AllRequests.Add(Req.RequestId, Req);

    // 挂入塔的运行时数据
    FLogisticsTowerRuntimeData& RuntimeData = TowerRuntimeData.FindOrAdd(TowerEntity);
    RuntimeData.PendingRequestIds.Add(Req.RequestId);

    // 事件推送：置脏，下一帧 Tick 优先处理
    TowerFrag->bDirty = true;
    UE_LOG(LogTemp, Log,
        TEXT("[Logistics] SubmitRequest OK | Type=%s Item=%d Qty=%d Tower=[%d,%d] PendingNow=%d"),
        Type == ELogisticsRequestType::Supply ? TEXT("Supply") : TEXT("Demand"),
        (int32)ItemType, Req.Quantity,
        TowerEntity.Index, TowerEntity.SerialNumber,
        RuntimeData.PendingRequestIds.Num());
    return Req.RequestId;
}

bool UMassDspLogisticsSubsystem::CancelRequest(const FGuid& RequestId)
{
    FLogisticsRequest* Req = AllRequests.Find(RequestId);
    if (!Req) return false;

    // 如果已被配对进任务，也取消对应任务
    for (auto& [TaskId, Task] : AllTasks)
    {
        if (Task.SupplyRequestId == RequestId || Task.DemandRequestId == RequestId)
        {
            Task.State = ELogisticsTaskState::Cancelled;
            // 重置对应设备
            if (Task.DeviceType == ELogisticsDeviceType::Drone && DronePool.IsValidIndex(Task.DevicePoolIndex))
            {
                FDroneData& Drone = DronePool[Task.DevicePoolIndex];
                Drone.State       = ELogisticsDeviceState::Idle;
                Drone.CurrentTaskId = FGuid();
                IdleDroneIndices.AddUnique(Task.DevicePoolIndex);
            }
            // TODO[VEHICLE]: 重置小车
            // TODO[TRAIN]:   重置火车
        }
    }

    // 从塔运行时数据中移除
    if (Req->PreferredTowerEntity.IsValid())
    {
        if (FLogisticsTowerRuntimeData* RuntimeData = TowerRuntimeData.Find(Req->PreferredTowerEntity))
        {
            RuntimeData->PendingRequestIds.Remove(RequestId);
        }
    }

    AllRequests.Remove(RequestId);
    return true;
}

const FLogisticsRequest* UMassDspLogisticsSubsystem::GetRequest(const FGuid& RequestId) const
{
    return AllRequests.Find(RequestId);
}

const FLogisticsTask* UMassDspLogisticsSubsystem::GetTask(const FGuid& TaskId) const
{
    return AllTasks.Find(TaskId);
}

// 
//  设备创建 / 销毁
// 

FDroneHandle UMassDspLogisticsSubsystem::CreateDrone(
    FMassEntityHandle AffiliatedTowerEntity, float FlightSpeed, int32 CarryCapacity)
{
    FDroneData Data;
    Data.AffiliatedTowerEntity = AffiliatedTowerEntity;
    Data.FlightSpeed           = FlightSpeed;
    Data.CarryCapacity         = CarryCapacity;
    Data.State                 = ELogisticsDeviceState::Idle;

    const int32 Idx = DronePool.Add(Data);

    // 分配 ISM 实例（起点放在原点，等待第一个任务后才真正飞行）
    DronePool[Idx].ISMInstanceIndex = AllocateDroneISMInstance(FVector::ZeroVector);
    DronePool[Idx].Generation       = 0;

    // 注册到归属塔
    if (AffiliatedTowerEntity.IsValid())
    {
        FLogisticsTowerRuntimeData& RuntimeData = TowerRuntimeData.FindOrAdd(AffiliatedTowerEntity);
        FDroneHandle Handle { Idx, DronePool[Idx].Generation };
        RuntimeData.AffiliatedDroneHandles.Add(Handle);
    }

    // 加入空闲池
    IdleDroneIndices.Add(Idx);

    // 同步扩展 VehiclePaths / TrainTrackLUTs 对齐（无人机不需要，但保持数组长度一致性）
    return FDroneHandle { Idx, DronePool[Idx].Generation };
}

FVehicleHandle UMassDspLogisticsSubsystem::CreateVehicle(const FVector& SpawnLocation, int32 CarryCapacity)
{
    FVehicleData Data;
    Data.CurrentLocation = SpawnLocation;
    Data.CarryCapacity   = CarryCapacity;

    const int32 Idx = VehiclePool.Add(Data);

    // 扩展路径数组对齐
    while (VehiclePaths.Num() <= Idx) VehiclePaths.AddDefaulted();

    VehiclePool[Idx].ISMInstanceIndex = AllocateVehicleISMInstance(SpawnLocation);
    IdleVehicleIndices.Add(Idx);

    return FVehicleHandle { Idx, VehiclePool[Idx].Generation };
}

FTrainHandle UMassDspLogisticsSubsystem::CreateTrain(int32 TrackSegmentIndex, int32 CarryCapacity)
{
    FTrainData Data;
    Data.TrackSegmentIndex = TrackSegmentIndex;
    Data.CarryCapacity     = CarryCapacity;

    const int32 Idx = TrainPool.Add(Data);

    while (TrainTrackLUTs.Num() <= Idx) TrainTrackLUTs.AddDefaulted();

    TrainPool[Idx].ISMInstanceIndex = AllocateTrainISMInstance(FVector::ZeroVector);
    IdleTrainIndices.Add(Idx);

    return FTrainHandle { Idx, TrainPool[Idx].Generation };
}

void UMassDspLogisticsSubsystem::DestroyDrone(FDroneHandle Handle)
{
    if (!Handle.IsValid() || !DronePool.IsValidIndex(Handle.Index)) return;
    FDroneData& Drone = DronePool[Handle.Index];
    if (Drone.Generation != Handle.Generation) return; // 悬空句柄

    FreeDroneISMInstance(Drone.ISMInstanceIndex);
    IdleDroneIndices.Remove(Handle.Index);
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
//  私有：请求匹配
// 

void UMassDspLogisticsSubsystem::MatchPendingRequests()
{
    UWorld* World = GetWorld();
    if (!World) return;

    FMassEntityManager* EntityManagerPtr = GetEntityManagerSafe(World);
    if (!EntityManagerPtr) return;
    FMassEntityManager& EntityManager = *EntityManagerPtr;

    for (auto& [TowerEntity, RuntimeData] : TowerRuntimeData)
    {
        if (!EntityManager.IsEntityValid(TowerEntity)) continue;

        FMassDspLogisticsTowerFragment* TowerFrag =
            EntityManager.GetFragmentDataPtr<FMassDspLogisticsTowerFragment>(TowerEntity);

        if (!TowerFrag || !TowerFrag->bDirty) continue;

        UE_LOG(LogTemp, Log,
            TEXT("[Logistics] MatchPending: tower=[%d,%d] pendingReqs=%d"),
            TowerEntity.Index, TowerEntity.SerialNumber,
            RuntimeData.PendingRequestIds.Num());

        // 统计供/需各多少
        int32 SupplyCnt = 0, DemandCnt = 0;
        for (const FGuid& RId : RuntimeData.PendingRequestIds)
        {
            if (const FLogisticsRequest* R = AllRequests.Find(RId))
                (R->Type == ELogisticsRequestType::Supply ? SupplyCnt : DemandCnt)++;
        }
        UE_LOG(LogTemp, Log,
            TEXT("[Logistics]   Supply=%d Demand=%d IdleDrones=%d Strategies=%d"),
            SupplyCnt, DemandCnt, IdleDroneIndices.Num(), DispatchStrategies.Num());

        TryMatchAndDispatchForTower(TowerEntity, RuntimeData);

        TowerFrag->bDirty = false;
    }
}

void UMassDspLogisticsSubsystem::TryMatchAndDispatchForTower(
    FMassEntityHandle TowerEntity, FLogisticsTowerRuntimeData& RuntimeData)
{
    // 分拣：按物品类型分桶 Supply/Demand
    TMap<EItemType, TArray<FGuid>> SupplyByType;
    TMap<EItemType, TArray<FGuid>> DemandByType;

    for (const FGuid& ReqId : RuntimeData.PendingRequestIds)
    {
        const FLogisticsRequest* Req = AllRequests.Find(ReqId);
        if (!Req) continue;
        if (Req->Type == ELogisticsRequestType::Supply)
            SupplyByType.FindOrAdd(Req->ItemType).Add(ReqId);
        else
            DemandByType.FindOrAdd(Req->ItemType).Add(ReqId);
    }

    // 对每种物品类型匹配 Supply + Demand
    for (auto& [ItemType, SupplyIds] : SupplyByType)
    {
        TArray<FGuid>* DemandIds = DemandByType.Find(ItemType);
        if (!DemandIds || DemandIds->IsEmpty()) continue;

        // 逐个配对，且对同一对 Supply/Demand 尽量批量派遣多架无人机
        while (!SupplyIds.IsEmpty() && !DemandIds->IsEmpty())
        {
            const FGuid SupplyId = SupplyIds[0];
            const FGuid DemandId = (*DemandIds)[0];

            FLogisticsRequest* Supply = AllRequests.Find(SupplyId);
            FLogisticsRequest* Demand = AllRequests.Find(DemandId);
            if (!Supply || !Demand) { SupplyIds.RemoveAt(0); DemandIds->RemoveAt(0); continue; }

            // 获取取货/送货世界位置（只查询一次，复用到所有批次任务）
            FVector PickupLoc  = FVector::ZeroVector;
            FVector DeliveryLoc= FVector::ZeroVector;
            if (UWorld* W = GetWorld())
            {
                FMassEntityManager* EMPtr = GetEntityManagerSafe(W);
                if (EMPtr)
                {
                    if (const FTransformFragment* PTF = EMPtr->GetFragmentDataPtr<FTransformFragment>(Supply->SourceEntity))
                        PickupLoc = PTF->GetTransform().GetLocation();
                    if (const FTransformFragment* DTF = EMPtr->GetFragmentDataPtr<FTransformFragment>(Demand->SourceEntity))
                        DeliveryLoc = DTF->GetTransform().GetLocation();
                }
            }

            // 批量派遣：一对 Supply/Demand 允许同时派出多架无人机
            // 每批次运量 = min(CarryCapacity, Supply剩余, Demand剩余)
            int32 SupplyRemaining = Supply->Quantity;
            int32 DemandRemaining = Demand->Quantity;
            bool  bAnyDispatched  = false;
            int32 StaggerIndex    = 0; // 第 N 架无人机延迟 N*Stagger 秒起飞

            while (SupplyRemaining > 0 && DemandRemaining > 0 && !IdleDroneIndices.IsEmpty())
            {
                const int32 BatchQty = FMath::Min3(SupplyRemaining, DemandRemaining,
                                                   FGameConst::DroneCarryCapacity);

                FLogisticsTask Task;
                Task.TaskId           = FGuid::NewGuid();
                Task.SupplyRequestId  = SupplyId;
                Task.DemandRequestId  = DemandId;
                Task.State            = ELogisticsTaskState::Pending;
                Task.PickupEntity     = Supply->SourceEntity;
                Task.DeliveryEntity   = Demand->SourceEntity;
                Task.PickupLocation   = PickupLoc;
                Task.DeliveryLocation = DeliveryLoc;
                Task.TransferQuantity = BatchQty;
                Task.DeviceType       = ELogisticsDeviceType::Drone;

                if (!TryDispatchTask(Task)) break; // 无更多空闲无人机

                // 错峰起飞：第 0 架立即起飞，第 N 架延迟 N*Stagger 秒
                if (StaggerIndex > 0)
                {
                    FDroneData& DispatchedDrone = DronePool[Task.DevicePoolIndex];
                    DispatchedDrone.ElapsedTime =
                        -(StaggerIndex * FGameConst::DroneDispatchStaggerInterval);
                }

                AllTasks.Add(Task.TaskId, Task);
                RuntimeData.ActiveTaskIds.Add(Task.TaskId);

                SupplyRemaining -= BatchQty;
                DemandRemaining -= BatchQty;
                bAnyDispatched   = true;
                ++StaggerIndex;
            }

            // 无论派出多少架，本轮请求对均视为已处理（从待匹配队列摘出）
            if (bAnyDispatched)
            {
                RuntimeData.PendingRequestIds.Remove(SupplyId);
                RuntimeData.PendingRequestIds.Remove(DemandId);
            }

            SupplyIds.RemoveAt(0);
            DemandIds->RemoveAt(0);
        }
    }
}

bool UMassDspLogisticsSubsystem::TryDispatchTask(FLogisticsTask& Task)
{
    const TUniquePtr<FLogisticsDeviceDispatchStrategy>* StrategyPtr =
        DispatchStrategies.Find(Task.DeviceType);

    if (!StrategyPtr || !(*StrategyPtr))
    {
        UE_LOG(LogTemp, Warning, TEXT("[Logistics] TryDispatchTask FAIL: no strategy for DeviceType=%d"), (int32)Task.DeviceType);
        return false;
    }

    if (IdleDroneIndices.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[Logistics] TryDispatchTask FAIL: no idle drones (pool=%d)"), DronePool.Num());
        return false;
    }

    const int32 SelectedIdx = (*StrategyPtr)->SelectBestDeviceIndex(IdleDroneIndices, Task);
    if (SelectedIdx < 0) return false;

    FDroneData& Drone = DronePool[SelectedIdx];

    Task.DevicePoolIndex = SelectedIdx;
    Task.State           = ELogisticsTaskState::Dispatched;

    (*StrategyPtr)->InitDeviceForTask(&Drone, Task);
    Task.State = ELogisticsTaskState::InTransit_Pickup;

    IdleDroneIndices.Remove(SelectedIdx);

    UE_LOG(LogTemp, Log,
        TEXT("[Logistics] Task dispatched! DroneIdx=%d P0=%s P3=%s TotalTime=%.2f"),
        SelectedIdx,
        *Drone.P0.ToString(), *Drone.P3.ToString(), Drone.TotalFlightTime);

    return true;
}

// 
//  私有：设备状态机推进（热路径，无虚调用）
// 

void UMassDspLogisticsSubsystem::UpdateDrones(float DeltaTime)
{
    // 10w 无人机：TSparseArray 直接遍历，连续内存，无虚调用
    // TODO[PERF]: 将所有 ISM 的 FTransform 先收集到 TArray<FTransform>，
    //             最后调用一次 UInstancedStaticMeshComponent::BatchUpdateInstancesTransforms

    for (auto It = DronePool.CreateIterator(); It; ++It)
    {
        FDroneData& Drone = *It;
        const int32 DroneIdx = It.GetIndex();

        switch (Drone.State)
        {
        case ELogisticsDeviceState::Idle:
            break; // 空闲，无需更新

        case ELogisticsDeviceState::Cooldown:
            Drone.CooldownRemaining -= DeltaTime;
            if (Drone.CooldownRemaining <= 0.f)
            {
                Drone.CooldownRemaining = 0.f;
                Drone.State             = ELogisticsDeviceState::Idle;
                IdleDroneIndices.AddUnique(DroneIdx);
            }
            break;

        case ELogisticsDeviceState::MovingToPickup:
        case ELogisticsDeviceState::MovingToDeliver:
        {
            // TotalFlightTime == 0 表示取货/交货点与当前位置重合，立即触发到达回调
            if (Drone.TotalFlightTime <= SMALL_NUMBER)
            {
                if (Drone.State == ELogisticsDeviceState::MovingToPickup)
                    OnDroneArrivedAtPickup(DroneIdx);
                else
                    OnDroneArrivedAtDelivery(DroneIdx);
                break;
            }

            Drone.ElapsedTime += DeltaTime;
            const float t = FMath::Clamp(Drone.ElapsedTime / Drone.TotalFlightTime, 0.f, 1.f);

            // 贝塞尔插值，更新 ISM 位置
            UpdateDroneISMInstance(Drone);

            if (t >= 1.f)
            {
                if (Drone.State == ELogisticsDeviceState::MovingToPickup)
                    OnDroneArrivedAtPickup(DroneIdx);
                else
                    OnDroneArrivedAtDelivery(DroneIdx);
            }
            break;
        }

        case ELogisticsDeviceState::AtPickup:
        case ELogisticsDeviceState::AtDeliver:
            // 瞬时状态，OnDroneArrived* 负责切换
            break;
        }
    }
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
                if (const FLogisticsTask* T = AllTasks.Find(Drone.CurrentTaskId))
                    WantQty = FMath::Min(WantQty, T->TransferQuantity);

                const int32 Taken = StorageFrag->TryProvideItems(WantQty);
                if (Taken > 0)
                {
                    Drone.CarriedItemType = ItemType;
                    Drone.CarriedQuantity = Taken;
                }
            }
        }
    }

    // 切换到飞往交货点，同步更新任务状态
    Drone.State       = ELogisticsDeviceState::MovingToDeliver;
    Drone.ElapsedTime = 0.f;
    if (FLogisticsTask* Task = AllTasks.Find(Drone.CurrentTaskId))
        Task->State = ELogisticsTaskState::InTransit_Deliver;

    // 重新生成贝塞尔曲线（取货点 → 交货点），向自身行进方向右偏形成回程航道
    const FVector P0   = Drone.P3; // 当前位置（上一段终点）
    const FVector P3   = Drone.DeliveryLocation;
    const float   Arc  = FGameConst::DroneFlightArcHeight;
    const float   Lane = FGameConst::DroneFlightLaneOffset;

    const FVector Dir2D   = (P3 - P0).GetSafeNormal2D();
    // 对 Dir2D 做 90° 顺时针旋转（俯视）得到右方向：(dx,dy) → (dy,-dx)
    // 保证去程/回程都偏向各自行进方向的右侧，两条航道在世界空间中分离
    const FVector RightXY = Dir2D.IsNearlyZero()
        ? FVector::RightVector
        : FVector(Dir2D.Y, -Dir2D.X, 0.f);

    Drone.P0 = P0;
    Drone.P1 = P0 + FVector(0.f, 0.f, Arc) + RightXY * Lane;
    Drone.P2 = P3 + FVector(0.f, 0.f, Arc) + RightXY * Lane;
    Drone.P3 = P3;
    Drone.TotalFlightTime = FVector::Dist(P0, P3) / FMath::Max(1.f, Drone.FlightSpeed);
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
    const FGuid CompletedTaskId = Drone.CurrentTaskId;

    Drone.CarriedItemType   = EItemType::None;
    Drone.CarriedQuantity   = 0;
    Drone.CurrentTaskId     = FGuid();
    Drone.State             = ELogisticsDeviceState::Cooldown;
    Drone.CooldownRemaining = Drone.CooldownDuration;

    // O(1) 直接用 TaskId 查找并标记完成（不再全表扫描）
    if (FLogisticsTask* Task = AllTasks.Find(CompletedTaskId))
    {
        Task->State = ELogisticsTaskState::Completed;
        for (auto& [TowerEnt, RTD] : TowerRuntimeData)
            RTD.ActiveTaskIds.Remove(CompletedTaskId);
    }
}

void UMassDspLogisticsSubsystem::OnDroneTaskFailed(int32 DronePoolIndex)
{
    FDroneData& Drone          = DronePool[DronePoolIndex];
    Drone.CarriedItemType      = EItemType::None;
    Drone.CarriedQuantity      = 0;
    const FGuid OldTaskId      = Drone.CurrentTaskId;
    Drone.CurrentTaskId        = FGuid();
    Drone.State                = ELogisticsDeviceState::Idle;
    IdleDroneIndices.AddUnique(DronePoolIndex);

    // 将请求重新放回待匹配队列
    if (FLogisticsTask* Task = AllTasks.Find(OldTaskId))
    {
        Task->State = ELogisticsTaskState::Failed;
        // TODO: 重新提交请求（给塔一次重试机会）
    }
}

// 
//  私有：ISM 同步
// 

void UMassDspLogisticsSubsystem::UpdateDroneISMInstance(FDroneData& Drone) const
{
    if (!DroneISM || Drone.ISMInstanceIndex < 0) return;
    // ISM 实例数量越界保护
    if (Drone.ISMInstanceIndex >= DroneISM->GetInstanceCount()) return;

    const float t   = (Drone.TotalFlightTime > 0.f)
        ? FMath::Clamp(Drone.ElapsedTime / Drone.TotalFlightTime, 0.f, 1.f)
        : 0.f;
    const FVector Pos = Drone.EvalBezier(t);

    // 朝向：贝塞尔一阶导数（切线方向）
    const float Dt    = 0.01f;
    const float tFwd  = FMath::Clamp(t + Dt, 0.f, 1.f);
    const FVector Fwd = (Drone.EvalBezier(tFwd) - Pos).GetSafeNormal();
    const FQuat   Rot = Fwd.IsNearlyZero() ? FQuat::Identity : FRotationMatrix::MakeFromX(Fwd).ToQuat();

    const FTransform InstanceTransform(Rot, Pos, FVector::OneVector);
    // bMarkRenderStateDirty=true 确保每帧位置更新就刷新到 GPU
    DroneISM->UpdateInstanceTransform(Drone.ISMInstanceIndex, InstanceTransform,
        /*bWorldSpace=*/true, /*bMarkRenderStateDirty=*/true);
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
    if (DroneISM && InstanceIndex >= 0)
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
    TArray<FGuid> ToRemove;

    for (auto& [ReqId, Req] : AllRequests)
    {
        if (Now - Req.RequestTime > Req.ExpiryDuration)
            ToRemove.Add(ReqId);
    }

    for (const FGuid& Id : ToRemove)
        CancelRequest(Id);
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
    FMassDspStorageFragment* DelFrag  = EM.GetFragmentDataPtr<FMassDspStorageFragment>(DeliveryEntity);

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
