#include "Subsystems/MassDspManager.h"

#include "GameConst.h"
#include "MassCommonFragments.h"

#include "Fragments/BeltItemFragment.h"

#include "Actors/MassDspBuilding.h"
#include "Fragments/MassDspBuildingFragment.h"

#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "MassEntityConfigAsset.h"
#include "MassObserverNotificationTypes.h"
#include "Components/SplineMeshComponent.h"
#include "Components/SplineComponent.h"
#include "MassExecutor.h"

void UMassDspManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    if (UWorld* World = GetWorld())
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

        BeltsContainerActor = World->SpawnActor<AActor>(AActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
        if (BeltsContainerActor)
        {
#if WITH_EDITOR
            BeltsContainerActor->SetActorLabel(TEXT("BeltsContainer"));
#endif
            USceneComponent* Root = NewObject<USceneComponent>(BeltsContainerActor, TEXT("Root"));
            BeltsContainerActor->SetRootComponent(Root);
            Root->RegisterComponent();
        }
    }
}


void UMassDspManager::Deinitialize()
{
    if (BeltsContainerActor)
    {
        BeltsContainerActor->Destroy();
        BeltsContainerActor = nullptr;
    }

    Super::Deinitialize();
}


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

    return EntityHandle;
}

FBeltHandle UMassDspManager::CreateRuntimeBelt(const TArray<FVector>& ControlPoints, UStaticMesh* BeltMesh, int32 SegmentsPerSection)
{
    if (ControlPoints.Num() < 2 || !BeltsContainerActor) return FBeltHandle();

    // --- Spline 生成 ---
    USplineComponent* NewSpline = NewObject<USplineComponent>(BeltsContainerActor);
    NewSpline->SetupAttachment(BeltsContainerActor->GetRootComponent());
    NewSpline->SetClosedLoop(false);
    NewSpline->ClearSplinePoints(false);

    // 将 ControlPoints 添加为 SplinePoints
    for (const FVector& Pt : ControlPoints)
    {
        NewSpline->AddSplinePoint(Pt, ESplineCoordinateSpace::World, false);
        NewSpline->SetSplinePointType(NewSpline->GetNumberOfSplinePoints() - 1, ESplinePointType::Linear, false);
    }
    NewSpline->UpdateSpline();
    NewSpline->RegisterComponent();

    // --- 创建 Handle 并存储 ---
    FBeltTrajectory NewTrajectory;
    NewTrajectory.SplineComponent = NewSpline;
    NewTrajectory.TotalLength = NewSpline->GetSplineLength();
    NewTrajectory.Speed = 400.f;

    int32 Index = BeltTrajectories.Add(NewTrajectory);
    FBeltHandle NewHandle;
    NewHandle.Index = Index;
    NewHandle.Generation = 0; // TODO Implement generation check if needed

    // --- Spline Mesh 生成 (为了可视化) ---
    if (BeltMesh)
    {
        // 我们依然沿着 Spline 采样生成 Mesh，但现在可以直接用 Spline 接口 
        // 为了兼容旧逻辑（ControlPoints 之间生成 Mesh），我们遍历 Point

        int32 NumPoints = NewSpline->GetNumberOfSplinePoints();
        for (int32 i = 0; i < NumPoints - 1; ++i)
        {
            // 对于每个段，我们可以在两点之间生成一个 Mesh，并使用 Start/End Tangent
            // 细分：如果SegmentsPerSection > 1，则需要在中间再生成点。
            // 简单起见，我们按照之前的逻辑，在 Control Points 之间做细分采样

            // 下面的逻辑将复用 NewSpline 的插值能力
            float DistStart = NewSpline->GetDistanceAlongSplineAtSplinePoint(i);
            float DistEnd = NewSpline->GetDistanceAlongSplineAtSplinePoint(i + 1);
            float SegmentLen = DistEnd - DistStart;
            float StepLen = SegmentLen / SegmentsPerSection;

            for (int32 j = 0; j < SegmentsPerSection; ++j)
            {
                float D1 = DistStart + StepLen * j;
                float D2 = DistStart + StepLen * (j + 1);

                FVector P1 = NewSpline->GetLocationAtDistanceAlongSpline(D1, ESplineCoordinateSpace::World);
                FVector T1 = NewSpline->GetTangentAtDistanceAlongSpline(D1, ESplineCoordinateSpace::World);
                FVector P2 = NewSpline->GetLocationAtDistanceAlongSpline(D2, ESplineCoordinateSpace::World);
                FVector T2 = NewSpline->GetTangentAtDistanceAlongSpline(D2, ESplineCoordinateSpace::World);

                USplineMeshComponent* Smc = NewObject<USplineMeshComponent>(BeltsContainerActor);
                Smc->SetStaticMesh(BeltMesh);
                Smc->SetMobility(EComponentMobility::Movable);
                Smc->SetForwardAxis(ESplineMeshAxis::X);

                float Len = FVector::Dist(P1, P2);

                Smc->SetStartAndEnd(P1, T1.GetSafeNormal() * Len, P2, T2.GetSafeNormal() * Len);

                Smc->SetStartScale(FVector2D(1.0f, 0.2f));
                Smc->SetEndScale(FVector2D(1.0f, 0.2f));

                Smc->SetupAttachment(BeltsContainerActor->GetRootComponent());
                Smc->RegisterComponent();
            }
        }
    }

    return NewHandle;
}

FBeltHandle UMassDspManager::CreateAndLinkBeltForSlot(
    const AMassDspBuilding* SBuilding, int32 StartSlotIndex, const AMassDspBuilding* EBuilding, int32 EndSlotIndex, UStaticMesh* BeltMesh
)
{
    if (!(SBuilding && SBuilding->MassHandle.IsValid() && EBuilding && EBuilding->MassHandle.IsValid())) return FBeltHandle();

    FMassEntityManager& EntityManager = GetWorld()->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();

    FBuildingSlotState *StartSlot = nullptr, *EndSlot = nullptr;

    if (FMassDspBuildingSlotsFragment* MinerSlots = EntityManager.GetFragmentDataPtr<FMassDspBuildingSlotsFragment>(SBuilding->MassHandle))
    {
        for (auto& Slot : MinerSlots->GetSlots())
        {
            if (Slot.Type == EBuildingSlotType::Output)
            {
                if (StartSlotIndex-- == 0)
                {
                    StartSlot = &Slot;
                    break;
                }
            }
        }
    }

    if (FMassDspBuildingSlotsFragment* StorageSlots = EntityManager.GetFragmentDataPtr<FMassDspBuildingSlotsFragment>(EBuilding->MassHandle))
    {
        for (auto& Slot : StorageSlots->GetSlots())
        {
            if (Slot.Type == EBuildingSlotType::Input)
            {
                if (EndSlotIndex-- == 0)
                {
                    EndSlot = &Slot;
                    break;
                }
            }
        }
    }

    if (!StartSlot || !EndSlot) return FBeltHandle();

    TArray<FVector> BeltPoints;

    BeltPoints.Add(SBuilding->GetActorTransform().GetLocation());
    BeltPoints.Add(StartSlot->WorldLocation);
    BeltPoints.Add(EndSlot->WorldLocation);
    BeltPoints.Add(EBuilding->GetActorTransform().GetLocation());

    FBeltHandle BeltHandle = CreateRuntimeBelt(BeltPoints, BeltMesh, 10);
    if (!BeltHandle.IsValid()) return FBeltHandle();

    StartSlot->ConnectedLaneHandle = EndSlot->ConnectedLaneHandle = BeltHandle;

    return BeltHandle;
}

bool UMassDspManager::ProvideItemToBelt(FMassCommandBuffer& CommandBuffer, FBeltHandle BeltHandle, UMassEntityConfigAsset* ItemConfig)
{
    if (!ItemConfig || !BeltHandle.IsValid()) return false;
    UWorld* World = GetWorld();
    UMassEntitySubsystem* MassSubsystem = World->GetSubsystem<UMassEntitySubsystem>();

    // 检查这条车道的最后一个物品是否还在0附近
    if (auto Items = BeltEntityRegistry.Find(BeltHandle); Items && !Items->Entities.IsEmpty())
    {
        if (const FBeltItemFragment* Item = MassSubsystem->GetEntityManager().GetFragmentDataPtr<FBeltItemFragment>(Items->Entities.Last());
            Item && Item->DistanceAlongBelt <= FGameConst::HalfLength * 3 + FGameConst::MinSpacing)
        {
            return false;
        }
    }

    CommandBuffer.PushCommand<FMassDeferredCreateCommand>([this, World, ItemConfig, BeltHandle](FMassEntityManager& InEntityManager)
    {
        if (!BeltTrajectories.IsValidIndex(BeltHandle.Index)) return;
        const FBeltTrajectory& Trajectory = BeltTrajectories[BeltHandle.Index];

        const FMassEntityTemplate& EntityTemplate = ItemConfig->GetConfig().GetOrCreateEntityTemplate(*World);
        TArray<FMassEntityHandle> NewEntities;

        auto CreationContext = InEntityManager.BatchCreateEntities(EntityTemplate.GetArchetype(), EntityTemplate.GetSharedFragmentValues(), 1, NewEntities);
        InEntityManager.BatchSetEntityFragmentValues(CreationContext->GetEntityCollections(InEntityManager), EntityTemplate.GetInitialFragmentValues());

        constexpr float InitialDistance = FGameConst::HalfLength;

        for (int32 i = 0; i < NewEntities.Num(); ++i)
        {
            FMassEntityHandle Entity = NewEntities[i];

            // --- 逻辑数据初始化 ---
            FBeltItemFragment* Item = InEntityManager.GetFragmentDataPtr<FBeltItemFragment>(Entity);
            if (!Item)
            {
                continue;
            }

            Item->DistanceAlongBelt = InitialDistance;
            Item->BeltHandle = BeltHandle;

            if (FTransformFragment* TransformFrag = InEntityManager.GetFragmentDataPtr<FTransformFragment>(Entity))
            {
                Trajectory.ApplyTransform(TransformFrag, InitialDistance);
            }

            BeltEntityRegistry.FindOrAdd(BeltHandle).Entities.EmplaceLast(Entity);
        }
    });

    return true;
}

bool UMassDspManager::ConsumeItemFromBelt(FMassCommandBuffer& CommandBuffer, FBeltHandle BeltHandle)
{
    // 检查注册表
    if (FBeltEntityArray* BeltItems = BeltEntityRegistry.Find(BeltHandle))
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
