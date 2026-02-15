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
