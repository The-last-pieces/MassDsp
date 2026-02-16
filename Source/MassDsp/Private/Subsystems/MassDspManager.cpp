#include "Subsystems/MassDspManager.h"

#include "GameConst.h"

#include "Fragments/BeltItemFragment.h"

#include "Actors/MassDspBuilding.h"
#include "Fragments/MassDspBuildingFragment.h"

#include "ZoneGraphSubsystem.h"
#include "ZoneGraphData.h"
#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "MassZoneGraphNavigationFragments.h"
#include "MassEntityConfigAsset.h"
#include "MassObserverNotificationTypes.h"
#include "Components/SplineMeshComponent.h"
#include "MassExecutor.h"


FMassEntityHandle UMassDspManager::RegisterBuildingEntity(AMassDspBuilding* BuildingActor) const
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
    if (FMassDspBuildingFragment* BuildingFragment = EntityManager.GetFragmentDataPtr<FMassDspBuildingFragment>(EntityHandle))
    {
        BuildingFragment->BuildingActor = BuildingActor;
        BuildingFragment->State = 0; // 默认闲置
    }

    // 3. 处理槽口信息并添加到 Fragment
    // 不需要再 AddFragmentFromEntity 了，因为 Entity 已经有了

    if (FMassDspBuildingSlotsFragment* SlotsFragment = EntityManager.GetFragmentDataPtr<FMassDspBuildingSlotsFragment>(EntityHandle))
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

            SlotsFragment->AddSlot(NewSlotState);
        }
    }

    // 4. (可选) 如果你希望使用 MassActorSubsystem 来管理 Actor 生命周期同步
    // EntityManager.AddFragment<FMassActorFragment>(EntityHandle); 
    // FMassActorFragment* ActorFragment = EntityManager.GetFragmentDataPtr<FMassActorFragment>(EntityHandle);
    // ActorFragment->Set(BuildingActor);

    return EntityHandle;
}

// TODO 传送带参数可以进一步丰富，比如宽度(常量)、材质、速度等
FZoneGraphDataHandle UMassDspManager::CreateRuntimeBelt(const TArray<FVector>& ControlPoints, UStaticMesh* BeltMesh, int32 SegmentsPerSection) const
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
        for (int32 j = 0; j < SegmentsPerSection; ++j)
        {
            float Alpha = static_cast<float>(j) / static_cast<float>(SegmentsPerSection);
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
    Storage.Bounds = FBox(SampledPoints); //.ExpandBy(200.0f);

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

            SMC->SetStartScale(FVector2D(1.0f, 0.2f));
            SMC->SetEndScale(FVector2D(1.0f, 0.2f));

            SMC->SetupAttachment(Root);
            SMC->RegisterComponent();
        }
    }

    return RegisteredHandle;
}

bool UMassDspManager::ProvideItemToBelt(FMassCommandBuffer& CommandBuffer, FZoneGraphLaneHandle LaneHandle, UMassEntityConfigAsset* ItemConfig)
{
    if (!ItemConfig) return false;
    UWorld* World = GetWorld();
    UMassEntitySubsystem* MassSubsystem = World->GetSubsystem<UMassEntitySubsystem>();

    // 检查这条车道的最后一个物品是否还在0附近
    if (auto Items = LaneRegistry.Find(LaneHandle); Items && !Items->Entities.IsEmpty())
    {
        if (const FBeltItemFragment* Item = MassSubsystem->GetEntityManager().GetFragmentDataPtr<FBeltItemFragment>(Items->Entities.Last());
            Item && Item->DistanceAlongBelt <= FGameConst::HalfLength * 3 + FGameConst::MinSpacing)
        {
            return false;
        }
    }

    CommandBuffer.PushCommand<FMassDeferredCreateCommand>([this, World, ItemConfig, LaneHandle](FMassEntityManager& InEntityManager)
    {
        const FMassEntityTemplate& EntityTemplate = ItemConfig->GetConfig().GetOrCreateEntityTemplate(*World);
        TArray<FMassEntityHandle> NewEntities;

        auto CreationContext = InEntityManager.BatchCreateEntities(EntityTemplate.GetArchetype(), EntityTemplate.GetSharedFragmentValues(), 1, NewEntities);
        InEntityManager.BatchSetEntityFragmentValues(CreationContext->GetEntityCollections(InEntityManager), EntityTemplate.GetInitialFragmentValues());

        for (int32 i = 0; i < NewEntities.Num(); ++i)
        {
            FMassEntityHandle Entity = NewEntities[i];

            // --- 逻辑数据初始化 ---
            FBeltItemFragment& Item = InEntityManager.GetFragmentDataChecked<FBeltItemFragment>(Entity);
            Item.DistanceAlongBelt = FGameConst::HalfLength;

            FMassZoneGraphLaneLocationFragment& LaneLoc = InEntityManager.GetFragmentDataChecked<FMassZoneGraphLaneLocationFragment>(Entity);
            LaneLoc.LaneHandle = LaneHandle;

            // 更新 Registry
            LaneRegistry.FindOrAdd(LaneLoc.LaneHandle).Entities.EmplaceLast(Entity);
        }
    });

    return true;
}

bool UMassDspManager::ConsumeItemFromBelt(FMassCommandBuffer& CommandBuffer, FZoneGraphLaneHandle LaneHandle)
{
    // 检查注册表
    if (FBeltEntityArray* BeltItems = LaneRegistry.Find(LaneHandle))
    {
        if (BeltItems->Entities.IsEmpty()) return false;

        UMassEntitySubsystem* EntitySubsystem = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
        FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();

        for (int32 i = 0; i < BeltItems->Entities.Num(); ++i)
        {
            FMassEntityHandle Entity = BeltItems->Entities.First();
            if (!EntityManager.IsEntityValid(Entity))
            {
                CommandBuffer.DestroyEntity(Entity);
                BeltItems->Entities.PopFirst();
                continue;
            }

            if (FBeltItemFragment* ItemFrag = EntityManager.GetFragmentDataPtr<FBeltItemFragment>(Entity))
            {
                if (ItemFrag->bIsBlocked)
                {
                    CommandBuffer.DestroyEntity(Entity);
                    BeltItems->Entities.PopFirst();
                    return true;
                }
                return false;
            }
        }
    }
    return false;
}
