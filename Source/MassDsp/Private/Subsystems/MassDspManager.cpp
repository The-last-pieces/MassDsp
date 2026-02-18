#include "Subsystems/MassDspManager.h"

#include "GameConst.h"
#include "MassCommonFragments.h"

#include "Fragments/BeltItemFragment.h"

#include "Actors/MassDspBuilding.h"
#include "Actors/MassDspMiner.h"
#include "Actors/MassDspStorage.h"
#include "Actors/MassDspAssembler.h"
#include "Fragments/MassDspBuildingFragment.h"
#include "Fragments/MassDspMinerFragment.h"
#include "Fragments/MassDspStorageFragment.h"
#include "Fragments/MassDspAssemblerFragment.h"

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

    // 根据建筑类型创建不同的Archetype
    FMassArchetypeHandle ArchetypeHandle;

    if (AMassDspMiner* MinerActor = Cast<AMassDspMiner>(BuildingActor))
    {
        // 矿机：包含BuildingFragment + MinerFragment + SlotsFragment
        ArchetypeHandle = EntityManager.CreateArchetype({
            FMassDspBuildingFragment::StaticStruct(),
            FMassDspMinerFragment::StaticStruct(),
            FMassDspBuildingSlotsFragment::StaticStruct()
        });
    }
    else if (AMassDspStorage* StorageActor = Cast<AMassDspStorage>(BuildingActor))
    {
        // 仓库：包含BuildingFragment + StorageFragment + SlotsFragment
        ArchetypeHandle = EntityManager.CreateArchetype({
            FMassDspBuildingFragment::StaticStruct(),
            FMassDspStorageFragment::StaticStruct(),
            FMassDspBuildingSlotsFragment::StaticStruct()
        });
    }
    else if (AMassDspAssembler* AssemblerActor = Cast<AMassDspAssembler>(BuildingActor))
    {
        // 合成台：包含BuildingFragment + AssemblerFragment + SlotsFragment
        ArchetypeHandle = EntityManager.CreateArchetype({
            FMassDspBuildingFragment::StaticStruct(),
            FMassDspAssemblerFragment::StaticStruct(),
            FMassDspBuildingSlotsFragment::StaticStruct()
        });
    }
    else
    {
        // 默认建筑：只包含BuildingFragment + SlotsFragment
        ArchetypeHandle = EntityManager.CreateArchetype({
            FMassDspBuildingFragment::StaticStruct(),
            FMassDspBuildingSlotsFragment::StaticStruct()
        });
    }

    FMassEntityHandle EntityHandle = EntityManager.CreateEntity(ArchetypeHandle);

    // 2. 初始化建筑基础 Fragment
    if (FMassDspBuildingFragment* BuildingFragment = EntityManager.GetFragmentDataPtr<FMassDspBuildingFragment>(EntityHandle))
    {
        BuildingFragment->BuildingActor = BuildingActor;
        BuildingFragment->State = 0; // 默认闲置
    }

    // 3. 根据建筑类型初始化特定Fragment
    if (AMassDspMiner* MinerActor = Cast<AMassDspMiner>(BuildingActor))
    {
        if (FMassDspMinerFragment* MinerFragment = EntityManager.GetFragmentDataPtr<FMassDspMinerFragment>(EntityHandle))
        {
            MinerFragment->ProductionInterval = MinerActor->ProductionInterval;
            MinerFragment->ProductionProgress = 0.0f;
        }
    }
    else if (AMassDspStorage* StorageActor = Cast<AMassDspStorage>(BuildingActor))
    {
        if (FMassDspStorageFragment* StorageFragment = EntityManager.GetFragmentDataPtr<FMassDspStorageFragment>(EntityHandle))
        {
            StorageFragment->Capacity = StorageActor->Capacity;
        }

        // 更新BuildingFragment的MaxInventory
        if (FMassDspBuildingFragment* BuildingFragment = EntityManager.GetFragmentDataPtr<FMassDspBuildingFragment>(EntityHandle))
        {
            BuildingFragment->MaxInventory = StorageActor->Capacity;
        }
    }
    else if (AMassDspAssembler* AssemblerActor = Cast<AMassDspAssembler>(BuildingActor))
    {
        if (FMassDspAssemblerFragment* AssemblerFragment = EntityManager.GetFragmentDataPtr<FMassDspAssemblerFragment>(EntityHandle))
        {
            //AssemblerFragment->CurrentRecipe = AssemblerActor->CurrentRecipe;
            AssemblerFragment->CraftingSpeedMultiplier = AssemblerActor->CraftingSpeedMultiplier;
            AssemblerFragment->InputBufferCapacity = AssemblerActor->InputBufferCapacity;
            AssemblerFragment->OutputBufferCapacity = AssemblerActor->OutputBufferCapacity;
            AssemblerFragment->CraftingProgress = 0.0f;
            AssemblerFragment->OutputBufferCount = 0;
        }
    }

    // 4. 处理槽口信息并添加到 Fragment
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
            NewSlotState.SlotExtend = SlotDef.SlotExtend;
            NewSlotState.Type = SlotDef.SlotType;

            SlotsFragment->AddSlot(NewSlotState);
        }
    }

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
    NewTrajectory.Speed = 400.f;

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
            // TODO 存物品类型

            if (FTransformFragment* TransformFrag = InEntityManager.GetFragmentDataPtr<FTransformFragment>(Entity))
            {
                Trajectory.ApplyTransform(TransformFrag, InitialDistance);
            }

            BeltEntityRegistry.FindOrAdd(BeltHandle).Entities.EmplaceLast(Entity);
        }
    });

    return true;
}

// TODO 返回物品类型
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
