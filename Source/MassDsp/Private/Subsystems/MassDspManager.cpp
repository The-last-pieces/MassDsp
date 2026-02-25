#include "Subsystems/MassDspManager.h"

#include "GameConst.h"
#include "MassCommonFragments.h"
#include "MassDspGameMode.h"

#include "Actors/MassDspBuilding.h"

#include "Fragments/BeltItemFragment.h"
#include "Fragments/MassDspBuildingSlotsFragment.h"

#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "MassEntityConfigAsset.h"
#include "MassExecutor.h"
#include "MassRepresentationFragments.h"

#include "Components/SplineMeshComponent.h"
#include "Components/SplineComponent.h"

void UMassDspManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    if (UWorld* World = GetWorld())
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        SpawnParams.ObjectFlags |= RF_Transient;

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

FMassEntityHandle UMassDspManager::RegisterBuildingEntity(const AMassDspBuilding* BuildingActor) const
{
    if (!BuildingActor)
    {
        return FMassEntityHandle();
    }

    UMassEntitySubsystem* EntitySubsystem = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
    if (!EntitySubsystem)
    {
        return FMassEntityHandle();
    }

    FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();

    FMassArchetypeHandle ArchetypeHandle = EntityManager.CreateArchetype(BuildingActor->GetStaticStructs());

    FMassEntityHandle EntityHandle = EntityManager.CreateEntity(ArchetypeHandle);

    BuildingActor->InitFragmentForEntity(EntityManager, EntityHandle);

    return EntityHandle;
}

FBeltHandle UMassDspManager::CreateRuntimeBelt(const TFunction<void(USplineComponent*)>& InitSpline, UStaticMesh* BeltMesh, int32 SegmentsPerSection)
{
    if (!BeltsContainerActor) return FBeltHandle();

    // --- Spline 生成 ---
    USplineComponent* NewSpline = NewObject<USplineComponent>(BeltsContainerActor);
    NewSpline->SetupAttachment(BeltsContainerActor->GetRootComponent());
    NewSpline->SetClosedLoop(false);
    NewSpline->ClearSplinePoints(false);

    InitSpline(NewSpline);

    NewSpline->UpdateSpline();
    NewSpline->RegisterComponent();

    // --- 创建 Handle 并存储 ---
    FBeltTrajectory NewTrajectory;
    NewTrajectory.SplineComponent = NewSpline;
    NewTrajectory.TotalLength = NewSpline->GetSplineLength();
    NewTrajectory.Speed = FGameConst::ItemSpace * 6; // 1秒6个物品

    int32 Index = BeltTrajectories.Add(NewTrajectory);
    FBeltHandle NewHandle;
    NewHandle.Index = Index;
    NewHandle.Generation = 0; // TODO Implement generation check if needed

    if (BeltMesh)
    {
        float DistStart = 0;
        float DistEnd = NewSpline->GetSplineLength();
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

    return NewHandle;
}

FBeltHandle UMassDspManager::CreateAndLinkBeltForSlot(FMassEntityHandle SBuilding, int32 StartSlotIndex, FMassEntityHandle EBuilding, int32 EndSlotIndex, UStaticMesh* BeltMesh)
{
    if (!(SBuilding.IsValid() && EBuilding.IsValid())) return FBeltHandle();

    FMassEntityManager& EntityManager = GetWorld()->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();

    FBuildingSlotState *StartSlot = nullptr, *EndSlot = nullptr;

    if (FMassDspBuildingSlotsFragment* MinerSlots = EntityManager.GetFragmentDataPtr<FMassDspBuildingSlotsFragment>(SBuilding))
    {
        for (auto& Slot : MinerSlots->GetOutputSlots())
        {
            if (StartSlotIndex-- == 0)
            {
                StartSlot = &Slot;
                break;
            }
        }
    }

    if (FMassDspBuildingSlotsFragment* StorageSlots = EntityManager.GetFragmentDataPtr<FMassDspBuildingSlotsFragment>(EBuilding))
    {
        for (auto& Slot : StorageSlots->GetInputSlots())
        {
            if (EndSlotIndex-- == 0)
            {
                EndSlot = &Slot;
                break;
            }
        }
    }

    if (!StartSlot || !EndSlot) return FBeltHandle();

    FVector A = StartSlot->WorldLocation - StartSlot->WorldRotation * FVector(StartSlot->SlotExtend, 0, 0);
    FVector B = StartSlot->WorldLocation;
    FVector C = EndSlot->WorldLocation;
    FVector D = EndSlot->WorldLocation - EndSlot->WorldRotation * FVector(EndSlot->SlotExtend, 0, 0);

    TArray BeltPoints = {A, B, C, D};

    FVector AB_Direction = (B - A).GetSafeNormal();
    FVector CD_Direction = (D - C).GetSafeNormal();

    float AB_Length = FVector::Dist(A, B);
    float CD_Length = FVector::Dist(C, D);
    float BC_Length = FVector::Dist(B, C);

    constexpr float TangentScale = 5.f;

    float BTangentLen = FMath::Max(AB_Length * 1.0f, BC_Length * 0.4f) * TangentScale;
    FVector StartTangent = AB_Direction * BTangentLen;

    float CTangentLen = FMath::Max(CD_Length * 1.0f, BC_Length * 0.4f) * TangentScale;
    FVector EndTangent = CD_Direction * CTangentLen;

    // TODO 考虑引入中间拐点来减少曲线段的占比

    FBeltHandle BeltHandle = CreateRuntimeBelt([&BeltPoints, StartTangent,EndTangent](USplineComponent* NewSpline)
    {
        for (const auto& Pt : BeltPoints)
        {
            NewSpline->AddSplinePoint(Pt, ESplineCoordinateSpace::World, false);
        }

        NewSpline->SetSplinePointType(0, ESplinePointType::Linear, false);

        NewSpline->SetSplinePointType(1, ESplinePointType::CurveCustomTangent, false);
        NewSpline->SetTangentsAtSplinePoint(1, FVector::ZeroVector, StartTangent, ESplineCoordinateSpace::World, false);

        NewSpline->SetSplinePointType(2, ESplinePointType::CurveCustomTangent, false);
        NewSpline->SetTangentsAtSplinePoint(2, EndTangent, FVector::ZeroVector, ESplineCoordinateSpace::World, false);

        NewSpline->SetSplinePointType(3, ESplinePointType::Linear, false);
    }, BeltMesh, 50);

    if (!BeltHandle.IsValid()) return FBeltHandle();

    const FBeltTrajectory& Trajectory = BeltTrajectories[BeltHandle.Index];

    StartSlot->ConnectedLaneHandle = EndSlot->ConnectedLaneHandle = BeltHandle;
    StartSlot->BeltSpeed = EndSlot->BeltSpeed = Trajectory.Speed;

    return BeltHandle;
}

bool UMassDspManager::ProvideItemToBelt(FMassCommandBuffer& CommandBuffer, FBeltHandle BeltHandle, const TFunction<EItemType()>& GetItemFunc)
{
    if (!GameMode.IsValid())
    {
        GameMode = Cast<AMassDspGameMode>(GetWorld()->GetAuthGameMode());
    }
    if (!GameMode.IsValid()) return false;
    if (!BeltHandle.IsValid()) return false;

    auto ItemConfig = GameMode->BeltItemConfigAsset;
    if (!ItemConfig) return false;

    UWorld* World = GetWorld();
    UMassEntitySubsystem* MassSubsystem = World->GetSubsystem<UMassEntitySubsystem>();

    // 检查这条车道的最后一个物品是否还在附近
    if (auto Items = BeltEntityRegistry.Find(BeltHandle); Items && !Items->Entities.IsEmpty())
    {
        if (const FBeltItemFragment* Item = MassSubsystem->GetEntityManager().GetFragmentDataPtr<FBeltItemFragment>(Items->Entities.Last());
            Item && Item->DistanceAlongBelt <= FGameConst::HalfLength * 3 + FGameConst::MinSpacing)
        {
            return false;
        }
    }

    EItemType ItemType = GetItemFunc();

    if (ItemType == EItemType::None) return false;

    CommandBuffer.PushCommand<FMassDeferredCreateCommand>([this, World, ItemConfig, BeltHandle, ItemType](FMassEntityManager& InEntityManager)
    {
        if (!BeltTrajectories.IsValidIndex(BeltHandle.Index)) return;

        const FBeltTrajectory& Trajectory = BeltTrajectories[BeltHandle.Index];

        const FMassEntityTemplate& EntityTemplate = ItemConfig->GetConfig().GetOrCreateEntityTemplate(*World);

        auto Entity = InEntityManager.CreateEntity(EntityTemplate.GetArchetype(), EntityTemplate.GetSharedFragmentValues());
        InEntityManager.SetEntityFragmentValues(Entity, EntityTemplate.GetInitialFragmentValues());

        if (FMassRepresentationFragment* RepFrag = InEntityManager.GetFragmentDataPtr<FMassRepresentationFragment>(Entity))
        {
            if (const FItemConfigData* ItemConfigData = GameMode->GameConfig->GetItemConfig(ItemType))
            {
                RepFrag->StaticMeshDescHandle = ItemConfigData->GetOrCreateMeshHandle(World);
                RepFrag->CurrentRepresentation = EMassRepresentationType::StaticMeshInstance;
                RepFrag->PrevRepresentation = EMassRepresentationType::None;
            }
        }

        constexpr float InitialDistance = FGameConst::HalfLength;

        FBeltItemFragment& Item = InEntityManager.GetFragmentDataChecked<FBeltItemFragment>(Entity);

        Item.DistanceAlongBelt = InitialDistance;
        Item.BeltHandle = BeltHandle;
        Item.ItemType = ItemType;
        Item.bIsBlocked = false;

        FTransformFragment& TransformFrag = InEntityManager.GetFragmentDataChecked<FTransformFragment>(Entity);
        Trajectory.ApplyTransform(TransformFrag, InitialDistance);

        BeltEntityRegistry.FindOrAdd(BeltHandle).Entities.EmplaceLast(Entity);
    });

    return true;
}

EItemType UMassDspManager::ConsumeItemFromBelt(FMassCommandBuffer& CommandBuffer, FBeltHandle BeltHandle, const TFunction<bool(EItemType)>& ValidateItemFunc)
{
    // 检查注册表
    if (FBeltEntityArray* BeltItems = BeltEntityRegistry.Find(BeltHandle))
    {
        auto& Entities = BeltItems->Entities;
        if (Entities.IsEmpty()) return EItemType::None;

        UMassEntitySubsystem* EntitySubsystem = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
        FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();

        FMassEntityHandle Entity = Entities.First();

        if (FBeltItemFragment& ItemFrag = EntityManager.GetFragmentDataChecked<FBeltItemFragment>(Entity); ItemFrag.bIsBlocked && ValidateItemFunc(ItemFrag.ItemType))
        {
            CommandBuffer.DestroyEntity(Entity);
            Entities.PopFirst();
            return ItemFrag.ItemType;
        }
    }
    return EItemType::None;
}
