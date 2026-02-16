// Fill out your copyright notice in the Description page of Project Settings.


#include "Subsystems/MassDspManager.h"
#include "Fragments/BeltItemFragment.h"

// 引入新定义的类
#include "Actors/MassDspBuilding.h"
#include "Fragments/MassDspBuildingFragment.h"

#include "ZoneGraphSubsystem.h"
#include "ZoneGraphData.h"
#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "MassZoneGraphNavigationFragments.h"
#include "MassCommonFragments.h"
#include "MassEntityConfigAsset.h"
#include "MassObserverNotificationTypes.h"
#include "Components/SplineMeshComponent.h"
#include "MassActorSubsystem.h" // 如果需要 Actor 桥接的话
#include "MassExecutor.h" // 确保有执行上下文相关引用

FMassEntityHandle UMassDspManager::RegisterBuildingEntity(AMassDspBuilding* BuildingActor)
{
    if (!BuildingActor)
    {
        return FMassEntityHandle();
    }

    // 获取 Mass 实体子系统
    UMassEntitySubsystem* EntitySubsystem = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
    if (!EntitySubsystem)
    {
        return FMassEntityHandle();
    }

    // 1. 创建一个新的实体
    FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
    
    // 直接创建包含所需 Fragment 的 Archetype
    const FMassArchetypeHandle ArchetypeHandle = EntityManager.CreateArchetype({
        FMassDspBuildingFragment::StaticStruct(),
        FMassDspBuildingSlotsFragment::StaticStruct()
    });

    FMassEntityHandle EntityHandle = EntityManager.CreateEntity(ArchetypeHandle);

    // 2. 初始化建筑基础 Fragment
    FMassDspBuildingFragment* BuildingFragment = EntityManager.GetFragmentDataPtr<FMassDspBuildingFragment>(EntityHandle);
    if (BuildingFragment)
    {
        BuildingFragment->BuildingActor = BuildingActor;
        BuildingFragment->State = 0; // 默认闲置
    }

    // 3. 处理槽口信息并添加到 Fragment
    // 不需要再 AddFragmentFromEntity 了，因为 Entity 已经有了
    FMassDspBuildingSlotsFragment* SlotsFragment = EntityManager.GetFragmentDataPtr<FMassDspBuildingSlotsFragment>(EntityHandle);
    
    if (SlotsFragment)
    {
        const FTransform ActorTransform = BuildingActor->GetActorTransform();
        
        for (const FBuildingSlotDef& SlotDef : BuildingActor->Slots)
        {
            FBuildingSlotState NewSlotState;
            // 计算世界空间变换
            FTransform WorldSlotTransform = SlotDef.LocalTransform * ActorTransform;
            
            NewSlotState.WorldLocation = WorldSlotTransform.GetLocation();
            NewSlotState.WorldRotation = WorldSlotTransform.GetRotation();
            NewSlotState.Type = SlotDef.SlotType;
            NewSlotState.bConnected = false; 
            
            SlotsFragment->AddSlot(NewSlotState);
        }
    }

    // 4. (可选) 如果你希望使用 MassActorSubsystem 来管理 Actor 生命周期同步
    // EntityManager.AddFragment<FMassActorFragment>(EntityHandle); 
    // FMassActorFragment* ActorFragment = EntityManager.GetFragmentDataPtr<FMassActorFragment>(EntityHandle);
    // ActorFragment->Set(BuildingActor);

    return EntityHandle;
}

FMassEntityHandle UMassDspManager::CreateEntityWithFragments(const FMassEntityTemplate& EntityTemplate)
{
    // 实现通用的创建逻辑
    UMassEntitySubsystem* EntitySubsystem = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
    if (!EntitySubsystem) return FMassEntityHandle();
    
    // 这里仅作示例，实际通常是通过 Template 这里的 Archetype 来创建
    // FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
    // return EntityManager.CreateEntity(EntityTemplate.GetArchetype());
    
    return FMassEntityHandle();
}

// TODO 传送带参数可以进一步丰富，比如宽度(常量)、材质、速度等
FZoneGraphDataHandle UMassDspManager::CreateRuntimeBelt(const TArray<FVector>& ControlPoints, UStaticMesh* BeltMesh, int32 SegmentsPerSection)
{
    if (ControlPoints.Num() < 2) return FZoneGraphDataHandle();

    UWorld* World = GetWorld();
    UZoneGraphSubsystem* ZGSubsystem = World->GetSubsystem<UZoneGraphSubsystem>();
    if (!ZGSubsystem) return FZoneGraphDataHandle();

    // 1. 依然需要 Spawn 一个 AZoneGraphData 来存储数据，但它现在由 Subsystem 管理
    FActorSpawnParameters SpawnParams;
    AZoneGraphData* BeltDataActor = World->SpawnActor<AZoneGraphData>(AZoneGraphData::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    if (!BeltDataActor) return FZoneGraphDataHandle();

    USceneComponent* Root = NewObject<USceneComponent>(BeltDataActor, TEXT("Root"));
    BeltDataActor->SetRootComponent(Root);
    Root->SetMobility(EComponentMobility::Movable);
    Root->RegisterComponent();

    ZGSubsystem->UnregisterZoneGraphData(*BeltDataActor);

    FZoneGraphStorage& Storage = BeltDataActor->GetStorageMutable();
    TArray<FVector> SampledPoints;
    TArray<FVector> SampledTangents;
    TArray<float> Progressions;
    float TotalDistance = 0.0f;

    // --- Catmull-Rom 采样逻辑 (见之前代码，略) ---
    for (int i = 0; i < ControlPoints.Num() - 1; ++i)
    {
        const FVector P0 = ControlPoints[FMath::Max(i - 1, 0)];
        const FVector P1 = ControlPoints[i];
        const FVector P2 = ControlPoints[i + 1];
        const FVector P3 = ControlPoints[FMath::Min(i + 2, ControlPoints.Num() - 1)];
        const FVector T1 = (P2 - P0) * 0.5f;
        const FVector T2 = (P3 - P1) * 0.5f;
        for (int32 j = 0; j < SegmentsPerSection; ++j) {
            float Alpha = (float)j / (float)SegmentsPerSection;
            FVector Pos = FMath::CubicInterp(P1, T1, P2, T2, Alpha);
            if (SampledPoints.Num() > 0) TotalDistance += FVector::Dist(SampledPoints.Last(), Pos);
            SampledPoints.Add(Pos);
            Progressions.Add(TotalDistance);
            FVector NextPos = FMath::CubicInterp(P1, T1, P2, T2, Alpha + 0.01f);
            SampledTangents.Add((NextPos - Pos).GetSafeNormal());
        }
    }
    // 拷贝最后一点
    const FVector FinalP = ControlPoints.Last();
    const FVector FinalT = SampledTangents.Last();
    Progressions.Add(TotalDistance + FVector::Dist(SampledPoints.Last(), FinalP));
    SampledPoints.Add(FinalP);
    SampledTangents.Add(FinalT);

    Storage.LanePoints.Append(SampledPoints);
    Storage.LaneTangentVectors.Append(SampledTangents);
    Storage.LanePointProgressions.Append(Progressions);

    FZoneLaneData NewLane;
    NewLane.PointsBegin = 0;
    NewLane.PointsEnd = Storage.LanePoints.Num();
    NewLane.Width = 120.0f;
    NewLane.Tags.Add(FZoneGraphTag(0));
    Storage.Lanes.Add(NewLane);
    Storage.Bounds = FBox(SampledPoints).ExpandBy(200.0f);

    FZoneGraphDataHandle RegisteredHandle = ZGSubsystem->RegisterZoneGraphData(*BeltDataActor);

    // --- Spline Mesh 生成 ---
    if (BeltMesh)
    {
        for (int32 i = 0; i < SampledPoints.Num() - 1; ++i)
        {
            USplineMeshComponent* SMC = NewObject<USplineMeshComponent>(BeltDataActor);
            SMC->SetStaticMesh(BeltMesh);
            SMC->SetMobility(EComponentMobility::Movable);
            SMC->SetForwardAxis(ESplineMeshAxis::X);
            float SegLen = FVector::Dist(SampledPoints[i], SampledPoints[i + 1]);
            SMC->SetStartAndEnd(SampledPoints[i], SampledTangents[i] * SegLen, SampledPoints[i + 1], SampledTangents[i + 1] * SegLen);

            SMC->SetStartScale(FVector2D(1.25f, 0.2f));
            SMC->SetEndScale(FVector2D(1.25f, 0.2f));

            SMC->SetupAttachment(Root);
            SMC->RegisterComponent();
        }
    }

    return RegisteredHandle;
}

bool UMassDspManager::SpawnItemsOnBelt(FZoneGraphDataHandle DataHandle, UMassEntityConfigAsset* ItemConfig)
{
    if (!ItemConfig) return false;
    UWorld* World = GetWorld();
    UMassEntitySubsystem* MassSubsystem = World->GetSubsystem<UMassEntitySubsystem>();
    UZoneGraphSubsystem* ZGSubsystem = World->GetSubsystem<UZoneGraphSubsystem>();
    const AZoneGraphData* ZoneDataActor = ZGSubsystem->GetZoneGraphData(DataHandle);
    if (!ZoneDataActor) return false;
    const FZoneGraphStorage* StoragePtr = &ZoneDataActor->GetStorage();

    // 检查这条车道的最后一个物品是否还在0附近
    auto Items = LaneRegistry.Find(FZoneGraphLaneHandle(0, DataHandle));
    if (Items && Items->Entities.Num() > 0) {
        const FBeltItemFragment& Item = MassSubsystem->GetEntityManager().GetFragmentDataChecked<FBeltItemFragment>(Items->Entities[0]);
        const float HalfLength = Item.HalfLength;
        const float MinSpacing = 20.0f;
        // TODO 这俩参数后面都放mgr里做常量
        if (Item.DistanceAlongBelt <= HalfLength * 2 + MinSpacing) {
            return false;
        }
    }

    MassSubsystem->GetMutableEntityManager().Defer().PushCommand<FMassDeferredCreateCommand>([this, World, ItemConfig, DataHandle, StoragePtr](FMassEntityManager& InEntityManager) {
        const FMassEntityTemplate& EntityTemplate = ItemConfig->GetConfig().GetOrCreateEntityTemplate(*World);
        TArray<FMassEntityHandle> NewEntities;

        auto CreationContext = InEntityManager.BatchCreateEntities(EntityTemplate.GetArchetype(), EntityTemplate.GetSharedFragmentValues(), 1, NewEntities);
        InEntityManager.BatchSetEntityFragmentValues(CreationContext->GetEntityCollections(InEntityManager), EntityTemplate.GetInitialFragmentValues());

        for (int32 i = 0; i < NewEntities.Num(); ++i)
        {
            FMassEntityHandle Entity = NewEntities[i];

            // --- 逻辑数据初始化 ---
            FBeltItemFragment& Item = InEntityManager.GetFragmentDataChecked<FBeltItemFragment>(Entity);
            Item.DistanceAlongBelt = 0;

            FMassZoneGraphLaneLocationFragment& LaneLoc = InEntityManager.GetFragmentDataChecked<FMassZoneGraphLaneLocationFragment>(Entity);
            LaneLoc.LaneHandle = FZoneGraphLaneHandle(0, DataHandle);

            // 更新 Registry
            LaneRegistry.FindOrAdd(LaneLoc.LaneHandle).Entities.Add(Entity);
        }
        });

    return true;
}

bool UMassDspManager::SpawnItemOnLane(FZoneGraphLaneHandle LaneHandle, float Distance, FMassCommandBuffer& CommandBuffer)
{
    if (!DefaultItemConfig) return false;

    UWorld* World = GetWorld();
    if (!World) return false;

    // 注意：在 Command 的 Lambda 内部，DefaultItemConfig 可能已经被 GC 或者处于不安全状态
    // 最安全的是捕获 SharedPtr 或者使用 WeakObjectPtr，但这里假设 ConfigAsset 生命周期足够长
    // 为了线程安全，我们需要在执行 Command 时才去把 Config 转成 Template
    // 但是 GetOrCreateEntityTemplate 只能在 GameThread 跑 (如果还没创建的话)
    // 所以我们这里只能假设 Template 已经创建好了，或者冒险在 Command 里跑

    // 更好的方式：把 EntityConfig 转换成 Template 的工作前置，或者使用 SoftObjectPath
    // 这里为了简单，我们捕获 this，并假设 Manager 和 Config 都有效

    // CommandBuffer.PushCommand 将命令放入调用者提供的安全缓冲区中
    CommandBuffer.PushCommand<FMassDeferredCreateCommand>([this, World, LaneHandle, Distance](FMassEntityManager& InEntityManager)
        {
            // 这个 Lambda 将在主线程安全点执行
            if (!DefaultItemConfig || !World) return;

            const FMassEntityTemplate& EntityTemplate = DefaultItemConfig->GetConfig().GetOrCreateEntityTemplate(*World);
            TArray<FMassEntityHandle> NewEntities;

            auto CreationContext = InEntityManager.BatchCreateEntities(EntityTemplate.GetArchetype(), EntityTemplate.GetSharedFragmentValues(), 1, NewEntities);
            InEntityManager.BatchSetEntityFragmentValues(CreationContext->GetEntityCollections(InEntityManager), EntityTemplate.GetInitialFragmentValues());

            if (NewEntities.Num() > 0)
            {
                FMassEntityHandle NewItem = NewEntities[0];

                // 设置距离
                if (FBeltItemFragment* ItemFrag = InEntityManager.GetFragmentDataPtr<FBeltItemFragment>(NewItem))
                {
                    ItemFrag->DistanceAlongBelt = Distance;
                }

                // 设置 LaneLocation
                if (FMassZoneGraphLaneLocationFragment* LaneLoc = InEntityManager.GetFragmentDataPtr<FMassZoneGraphLaneLocationFragment>(NewItem))
                {
                    LaneLoc->LaneHandle = LaneHandle;
                }

                // 注册到 LaneRegistry
                // 注意：LaneRegistry 是 TMap，这是非线程安全的容器。
                // 但因为我们是在 DeferredCommand 中（主线程串行执行），所以这里写它是安全的！
                FBeltEntityArray& LaneData = LaneRegistry.FindOrAdd(LaneHandle);
                LaneData.Entities.Add(NewItem);
            }
        });

    return true;
}

bool UMassDspManager::ConsumeItemFromLane(FZoneGraphLaneHandle LaneHandle, FMassCommandBuffer& CommandBuffer)
{
    // 检查注册表
    if (FBeltEntityArray* BeltItems = LaneRegistry.Find(LaneHandle))
    {
        if (BeltItems->Entities.Num() == 0) return false;

        UMassEntitySubsystem* EntitySubsystem = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
        FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();

        // 简单策略：移除第一个有效的实体 (通常是最早生成的)
        // 更好的做法是维护排序或者是双端队列，但 TArray 在头部移除开销大。
        // 为了演示，我们遍历找最远距离的（假设最远就是最靠近传送带末端的）。
        
        int32 BestIndex = -1;
        float MaxDist = 2000 - 250; // TODO
        
        for (int32 i = 0; i < BeltItems->Entities.Num(); ++i)
        {
            FMassEntityHandle Entity = BeltItems->Entities[i];
            if (!EntityManager.IsEntityValid(Entity)) continue;

            if (FBeltItemFragment* ItemFrag = EntityManager.GetFragmentDataPtr<FBeltItemFragment>(Entity))
            {
                if (ItemFrag->DistanceAlongBelt > MaxDist)
                {
                    MaxDist = ItemFrag->DistanceAlongBelt;
                    BestIndex = i;
                }
            }
        }

        if (BestIndex != -1)
        {
            FMassEntityHandle ItemToDestroy = BeltItems->Entities[BestIndex];
            CommandBuffer.DestroyEntity(ItemToDestroy);
            
            // SwapRemove 高效，但会打乱顺序，不过我们上面每次都重新找最远的，所以顺序不重要
            BeltItems->Entities.RemoveAt(BestIndex); 
            
            return true;
        }
    }
    return false;
}

bool UMassDspManager::FindAndConnectLaneForSlot(FBuildingSlotState& SlotState, float SearchRadius)
{
    // 暂时存根，暂不实现自动空间查询连接
    return false; 
}
