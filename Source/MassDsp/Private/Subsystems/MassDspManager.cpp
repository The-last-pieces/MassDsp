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
#include "MassRepresentationFragments.h"
#include "MassLODFragments.h"

#include "ProceduralMeshComponent.h"
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

TWeakObjectPtr<AMassDspGameMode> UMassDspManager::TryGetGameMode()
{
    if (!GameMode.IsValid())
    {
        GameMode = Cast<AMassDspGameMode>(GetWorld()->GetAuthGameMode());
    }

    return GameMode;
}

FBeltHandle UMassDspManager::CreateRuntimeBelt(const TFunction<void(USplineComponent*)>& InitSpline, UMaterialInterface* Material, int32 SegmentsPerSection)
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

    // TODO 大规模测试时性能有很大问题
    if (Material)
    {
        TArray<FVector> Vertices;
        TArray<int32> Triangles;
        TArray<FVector> Normals;
        TArray<FVector2D> UVs;
        const float Width = 110.0f;
        const float BeltHeight = 20.0f;
        const float BeltThickness = 20.0f;
        const float DistEnd = NewSpline->GetSplineLength();
        const float StepLen = DistEnd / SegmentsPerSection;

        // 预先生成所有关键点的顶点
        TArray<FVector> ControlPoints;
        TArray<FVector> UpVectors;
        TArray<FVector> RightVectors;
        TArray<float> UCoords;

        for (int32 i = 0; i <= SegmentsPerSection; ++i)
        {
            float Distance = FMath::Min(StepLen * i, DistEnd);
            FVector Point = NewSpline->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
            Point.Z += BeltHeight;

            FVector Up = NewSpline->GetUpVectorAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
            FVector Tangent = NewSpline->GetTangentAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World).GetSafeNormal();
            FVector Right = FVector::CrossProduct(Up, Tangent).GetSafeNormal();

            ControlPoints.Add(Point);
            UpVectors.Add(Up);
            RightVectors.Add(Right);
            UCoords.Add((float)i / SegmentsPerSection);
        }

        // 为每个控制点生成顶面和底面的顶点
        TArray<int32> TopLeftIndices;
        TArray<int32> TopRightIndices;
        TArray<int32> BotLeftIndices;
        TArray<int32> BotRightIndices;

        for (int32 i = 0; i <= SegmentsPerSection; ++i)
        {
            FVector Point = ControlPoints[i];
            FVector Up = UpVectors[i];
            FVector Right = RightVectors[i];
            float U = UCoords[i];

            // 顶面左右顶点
            TopLeftIndices.Add(Vertices.Num());
            Vertices.Add(Point - Right * Width * 0.5f);
            Normals.Add(Up);
            UVs.Add(FVector2D(U, 0.0f));

            TopRightIndices.Add(Vertices.Num());
            Vertices.Add(Point + Right * Width * 0.5f);
            Normals.Add(Up);
            UVs.Add(FVector2D(U, 1.0f));

            // 底面左右顶点
            BotLeftIndices.Add(Vertices.Num());
            Vertices.Add(Point - Right * Width * 0.5f - Up * BeltThickness);
            Normals.Add(-Up);
            UVs.Add(FVector2D(U, 0.0f));

            BotRightIndices.Add(Vertices.Num());
            Vertices.Add(Point + Right * Width * 0.5f - Up * BeltThickness);
            Normals.Add(-Up);
            UVs.Add(FVector2D(U, 1.0f));
        }

        // 构建三角形（使用共享顶点）
        for (int32 j = 0; j < SegmentsPerSection; ++j)
        {
            int32 i0 = j;
            int32 i1 = j + 1;

            // 顶面
            Triangles.Add(TopLeftIndices[i1]);
            Triangles.Add(TopLeftIndices[i0]);
            Triangles.Add(TopRightIndices[i0]);
            Triangles.Add(TopLeftIndices[i1]);
            Triangles.Add(TopRightIndices[i0]);
            Triangles.Add(TopRightIndices[i1]);

            // 底面
            Triangles.Add(BotLeftIndices[i0]);
            Triangles.Add(BotLeftIndices[i1]);
            Triangles.Add(BotRightIndices[i0]);
            Triangles.Add(BotRightIndices[i0]);
            Triangles.Add(BotLeftIndices[i1]);
            Triangles.Add(BotRightIndices[i1]);

            // 左侧面 - 需要独立顶点（不同法线）
            int32 LeftSideBase = Vertices.Num();
            FVector LeftNormal = -RightVectors[j];

            Vertices.Add(ControlPoints[i0] - RightVectors[i0] * Width * 0.5f);
            Normals.Add(LeftNormal);
            UVs.Add(FVector2D(UCoords[i0], 0.0f));

            Vertices.Add(ControlPoints[i1] - RightVectors[i1] * Width * 0.5f);
            Normals.Add(LeftNormal);
            UVs.Add(FVector2D(UCoords[i1], 0.0f));

            Vertices.Add(ControlPoints[i0] - RightVectors[i0] * Width * 0.5f - UpVectors[i0] * BeltThickness);
            Normals.Add(LeftNormal);
            UVs.Add(FVector2D(UCoords[i0], 1.0f));

            Vertices.Add(ControlPoints[i1] - RightVectors[i1] * Width * 0.5f - UpVectors[i1] * BeltThickness);
            Normals.Add(LeftNormal);
            UVs.Add(FVector2D(UCoords[i1], 1.0f));

            Triangles.Add(LeftSideBase + 0);
            Triangles.Add(LeftSideBase + 1);
            Triangles.Add(LeftSideBase + 2);
            Triangles.Add(LeftSideBase + 2);
            Triangles.Add(LeftSideBase + 1);
            Triangles.Add(LeftSideBase + 3);

            // 右侧面
            int32 RightSideBase = Vertices.Num();
            FVector RightNormal = RightVectors[j];

            Vertices.Add(ControlPoints[i0] + RightVectors[i0] * Width * 0.5f);
            Normals.Add(RightNormal);
            UVs.Add(FVector2D(UCoords[i0], 0.0f));

            Vertices.Add(ControlPoints[i1] + RightVectors[i1] * Width * 0.5f);
            Normals.Add(RightNormal);
            UVs.Add(FVector2D(UCoords[i1], 0.0f));

            Vertices.Add(ControlPoints[i0] + RightVectors[i0] * Width * 0.5f - UpVectors[i0] * BeltThickness);
            Normals.Add(RightNormal);
            UVs.Add(FVector2D(UCoords[i0], 1.0f));

            Vertices.Add(ControlPoints[i1] + RightVectors[i1] * Width * 0.5f - UpVectors[i1] * BeltThickness);
            Normals.Add(RightNormal);
            UVs.Add(FVector2D(UCoords[i1], 1.0f));

            Triangles.Add(RightSideBase + 0);
            Triangles.Add(RightSideBase + 2);
            Triangles.Add(RightSideBase + 1);
            Triangles.Add(RightSideBase + 1);
            Triangles.Add(RightSideBase + 2);
            Triangles.Add(RightSideBase + 3);
        }


        if (Vertices.Num() == 0)
        {
            UE_LOG(LogTemp, Warning, TEXT("CreateRuntimeBelt: No valid vertices generated"));
            return NewHandle;
        }

        if (!BeltProceduralMesh)
        {
            BeltProceduralMesh = NewObject<UProceduralMeshComponent>(BeltsContainerActor, TEXT("BeltProceduralMesh"));
            BeltProceduralMesh->SetupAttachment(BeltsContainerActor->GetRootComponent());
            BeltProceduralMesh->SetVisibility(true);
            BeltProceduralMesh->SetCastShadow(false);
            BeltProceduralMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); // 禁用碰撞以避免Chaos错误
            BeltProceduralMesh->RegisterComponent();
        }

        BeltProceduralMesh->CreateMeshSection_LinearColor(
            NextSectionIndex,
            Vertices,
            Triangles,
            Normals,
            UVs,
            TArray<FLinearColor>(),
            TArray<FProcMeshTangent>(),
            false // bCreateCollision = false
        );

        if (Material)
        {
            BeltProceduralMesh->SetMaterial(NextSectionIndex, Material);
        }
        else
        {
            UMaterial* BaseMat = UMaterial::GetDefaultMaterial(MD_Surface);
            UMaterialInstanceDynamic* DynMat = UMaterialInstanceDynamic::Create(BaseMat, BeltProceduralMesh);
            BeltProceduralMesh->SetMaterial(NextSectionIndex, DynMat);
        }

        NextSectionIndex++;
    }

    return NewHandle;
}

FBeltHandle UMassDspManager::CreateAndLinkBeltForSlot(
    FMassEntityHandle SBuilding, int32 StartSlotIndex, FMassEntityHandle EBuilding, int32 EndSlotIndex, UMaterialInterface* Material
)
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
    }, Material, 50);

    if (!BeltHandle.IsValid()) return FBeltHandle();

    const FBeltTrajectory& Trajectory = BeltTrajectories[BeltHandle.Index];

    StartSlot->ConnectedLaneHandle = EndSlot->ConnectedLaneHandle = BeltHandle;
    StartSlot->BeltSpeed = EndSlot->BeltSpeed = Trajectory.Speed;

    return BeltHandle;
}

bool UMassDspManager::ProvideItemToBelt(FMassCommandBuffer& CommandBuffer, FBeltHandle BeltHandle, const TFunction<EItemType()>& GetItemFunc)
{
    TryGetGameMode();
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

        FMassArchetypeCompositionDescriptor Composition = EntityTemplate.GetCompositionDescriptor();
        Composition.Add<FBeltItemFragment>();

        FMassArchetypeHandle CustomArchetype = InEntityManager.CreateArchetype(Composition);

        auto Entity = InEntityManager.CreateEntity(CustomArchetype, EntityTemplate.GetSharedFragmentValues());
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

// ===== 新增：Building Entity批量创建系统 =====

FMassEntityHandle UMassDspManager::SpawnBuildingFromClass(FMassCommandBuffer& CommandBuffer, TSubclassOf<AMassDspBuilding> BuildingClass, const FTransform& WorldTransform,
                                                          EBuildingType BuildingType)
{
    if (!BuildingClass)
    {
        UE_LOG(LogTemp, Warning, TEXT("SpawnBuildingFromClass: Invalid BuildingClass"));
        return FMassEntityHandle();
    }

    FBuildingSpawnData SpawnData(BuildingClass, WorldTransform, BuildingType);

    FMassEntityHandle ResultHandle;

    CommandBuffer.PushCommand<FMassDeferredCreateCommand>([this, SpawnData, &ResultHandle](FMassEntityManager& EntityManager)
    {
        ResultHandle = CreateBuildingEntityInternal(EntityManager, SpawnData);
    });

    // TODO 这块有问题

    return ResultHandle;
}

TArray<FMassEntityHandle> UMassDspManager::BatchSpawnBuildings(const TArray<FBuildingSpawnData>& SpawnDataList)
{
    TArray<FMassEntityHandle> CreatedEntities;
    CreatedEntities.Reserve(SpawnDataList.Num());

    UMassEntitySubsystem* EntitySubsystem = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
    if (!EntitySubsystem)
    {
        UE_LOG(LogTemp, Error, TEXT("BatchSpawnBuildings: MassEntitySubsystem not found"));
        return CreatedEntities;
    }

    FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();

    // 批量创建（同步）
    for (const FBuildingSpawnData& SpawnData : SpawnDataList)
    {
        if (FMassEntityHandle EntityHandle = CreateBuildingEntityInternal(EntityManager, SpawnData); EntityHandle.IsValid())
        {
            CreatedEntities.Add(EntityHandle);
        }
    }

    UE_LOG(LogTemp, Log, TEXT("BatchSpawnBuildings: Created %d building entities"), CreatedEntities.Num());

    return CreatedEntities;
}

FMassEntityHandle UMassDspManager::CreateBuildingEntityInternal(FMassEntityManager& EntityManager, const FBuildingSpawnData& SpawnData)
{
    TryGetGameMode();
    if (!GameMode.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("CreateBuildingEntityInternal: GameMode is null!"));
        return FMassEntityHandle();
    }
    if (!SpawnData.BuildingClass)
    {
        UE_LOG(LogTemp, Error, TEXT("CreateBuildingEntityInternal: BuildingClass is null!"));
        return FMassEntityHandle();
    }

    // 从CDO获取Building配置
    const AMassDspBuilding* BuildingCDO = GetDefault<AMassDspBuilding>(SpawnData.BuildingClass);
    if (!BuildingCDO)
    {
        UE_LOG(LogTemp, Warning, TEXT("CreateBuildingEntityInternal: Failed to get CDO for class %s"), *SpawnData.BuildingClass->GetName());
        return FMassEntityHandle();
    }

    auto ItemConfig = GameMode->BeltItemConfigAsset;

    const FMassEntityTemplate& EntityTemplate = ItemConfig->GetConfig().GetOrCreateEntityTemplate(*GetWorld());

    FMassArchetypeCompositionDescriptor Composition = EntityTemplate.GetCompositionDescriptor();
    for (const UScriptStruct* FragmentType : BuildingCDO->GetStaticStructs())
    {
        Composition.GetContainer<FMassFragment>().Add(*FragmentType);
    }

    FMassArchetypeHandle CustomArchetype = EntityManager.CreateArchetype(Composition);

    auto EntityHandle = EntityManager.CreateEntity(CustomArchetype, EntityTemplate.GetSharedFragmentValues());
    EntityManager.SetEntityFragmentValues(EntityHandle, EntityTemplate.GetInitialFragmentValues());

    // 初始化Building特定Fragment数据（从CDO读取配置）
    BuildingCDO->InitFragmentForEntity(EntityManager, EntityHandle, SpawnData.WorldTransform);

    // 设置Transform
    if (FTransformFragment* TransformFrag = EntityManager.GetFragmentDataPtr<FTransformFragment>(EntityHandle))
    {
        TransformFrag->SetTransform(SpawnData.WorldTransform);
    }

    // 设置Representation（ISM渲染）
    if (FMassRepresentationFragment* RepFrag = EntityManager.GetFragmentDataPtr<FMassRepresentationFragment>(EntityHandle))
    {
        if (const FBuildingTypeConfig* BuildingConfig = GameMode->GameConfig->GetBuildingConfig(SpawnData.BuildingType))
        {
            RepFrag->StaticMeshDescHandle = BuildingConfig->GetOrCreateMeshHandle(GetWorld());
            RepFrag->CurrentRepresentation = EMassRepresentationType::StaticMeshInstance;
            RepFrag->PrevRepresentation = EMassRepresentationType::None;
        }
    }

    return EntityHandle;
}
