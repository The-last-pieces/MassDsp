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

TArray<UMassDspManager::FSamplePoint> UMassDspManager::GenerateAdaptiveSamples(const USplineComponent* Spline, int32 MinSegments, int32 MaxSegments)
{
    TArray<FSamplePoint> Samples;
    if (!Spline) return Samples;

    const float TotalLength = Spline->GetSplineLength();
    if (TotalLength <= 0.0f) return Samples;

    // 初始采样点
    Samples.Reserve(MaxSegments);
    Samples.Add(CreateSamplePoint(Spline, 0.0f));

    float CurrentDist = 0.0f;
    int32 SegmentCount = 0;

    while (CurrentDist < TotalLength && SegmentCount < MaxSegments)
    {
        const FSamplePoint& LastSample = Samples.Last();

        // 计算前向采样点的曲率
        float ProbeDistance = FMath::Min(CurrentDist + 50.0f, TotalLength);
        FVector ProbeTangent = Spline->GetTangentAtDistanceAlongSpline(ProbeDistance, ESplineCoordinateSpace::World).GetSafeNormal();

        // 曲率估算：切线方向变化率
        float DeltaAngle = FMath::Acos(FMath::Clamp(FVector::DotProduct(LastSample.Tangent, ProbeTangent), -1.0f, 1.0f));
        float Curvature = DeltaAngle / 50.0f; // 弧度/单位距离

        // 根据曲率自适应调整步长
        // 曲率大（急转弯）→ 步长小（密集采样）
        // 曲率小（直线）→ 步长大（稀疏采样）
        float AdaptiveStep = FMath::Clamp(
            200.0f / FMath::Max(Curvature * 1000.0f + 1.0f, 1.0f), // 曲率越大步长越小
            TotalLength / MaxSegments, // 最小步长（避免过密）
            TotalLength / MinSegments // 最大步长（保证最少段数）
        );

        CurrentDist = FMath::Min(CurrentDist + AdaptiveStep, TotalLength);

        FSamplePoint NewSample = CreateSamplePoint(Spline, CurrentDist);
        NewSample.Curvature = Curvature;
        Samples.Add(NewSample);

        SegmentCount++;
    }

    // 确保终点被采样
    if (FMath::Abs(Samples.Last().Distance - TotalLength) > 1.0f)
    {
        Samples.Add(CreateSamplePoint(Spline, TotalLength));
    }

    return Samples;
}

UMassDspManager::FSamplePoint UMassDspManager::CreateSamplePoint(const USplineComponent* Spline, float Distance)
{
    FSamplePoint Sample;
    Sample.Distance = Distance;
    Sample.Location = Spline->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
    Sample.Tangent = Spline->GetTangentAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World).GetSafeNormal();
    Sample.Up = Spline->GetUpVectorAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
    Sample.Right = FVector::CrossProduct(Sample.Up, Sample.Tangent).GetSafeNormal();
    Sample.Curvature = 0.0f;
    return Sample;
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
    NewTrajectory.Speed = FGameConst::ItemSpace * 2; // 1秒6个物品

    int32 Index = BeltTrajectories.Add(NewTrajectory);
    FBeltHandle NewHandle;
    NewHandle.Index = Index;
    NewHandle.Generation = 0; // TODO Implement generation check if needed

    // TODO 大规模测试时性能有很大问题
    if (Material)
    {
        TArray<FSamplePoint> SamplePoints = GenerateAdaptiveSamples(NewSpline, 100, 500);

        TArray<FVector> Vertices;
        TArray<int32> Triangles;
        TArray<FVector> Normals;
        TArray<FVector2D> UVs;

        if (!BeltProceduralMesh)
        {
            BeltProceduralMesh = NewObject<UProceduralMeshComponent>(BeltsContainerActor, TEXT("BeltProceduralMesh"));
            BeltProceduralMesh->SetupAttachment(BeltsContainerActor->GetRootComponent());
            BeltProceduralMesh->SetVisibility(true);
            BeltProceduralMesh->SetCastShadow(false);
            BeltProceduralMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); // 禁用碰撞以避免Chaos错误
            BeltProceduralMesh->RegisterComponent();
        }

        auto DynMaterial = UMaterialInstanceDynamic::Create(Material, this);


        constexpr float Width = 110.0f;
        constexpr float BeltThickness = 20.0f;
        constexpr float UVScale = 100.0f;
        constexpr float MatTiling = 5.0f;

        DynMaterial->SetVectorParameterValue(TEXT("BaseColor"), FColor::Blue);
        DynMaterial->SetScalarParameterValue(TEXT("Tiling"), MatTiling);

        // 公式推导: 
        // 材质相位变化率 = Time * MatSpeed
        // 空间相位变化率 = (Dist / UVScale) * Tiling
        // 令 Time * MatSpeed = (Dist / UVScale) * Tiling
        // => Dist/Time = PhysicalSpeed
        // => MatSpeed = PhysicalSpeed * Tiling / UVScale
        float CorrectedMatSpeed = NewTrajectory.Speed * MatTiling / UVScale;

        DynMaterial->SetScalarParameterValue(TEXT("Speed"), CorrectedMatSpeed);

        GenerateConveyorMesh(BeltProceduralMesh, NewSpline, DynMaterial, Width, BeltThickness, UVScale, 5.0f);

        // TODO 直线分割有问题. 材质一直闪烁
    }

    return NewHandle;
}

// 内部使用的切片结构体
struct FConveyorSlice
{
    FVector Location;
    FVector Right;
    FVector Up;
    FVector Tangent;
    float Distance;
};

void UMassDspManager::GenerateConveyorMesh(
    UProceduralMeshComponent* TargetMesh,
    const USplineComponent* Spline,
    UMaterialInterface* Material,
    float Width,
    float Thickness,
    float UVScale,
    float AngleThreshold)
{
    if (!TargetMesh || !Spline || Spline->GetNumberOfSplinePoints() < 2) return;

    // --- 1. 计算自适应切片 (Adaptive Slicing) ---
    TArray<FConveyorSlice> Slices;
    const float SplineLength = Spline->GetSplineLength();
    constexpr float CheckStep = 10.0f; // 采样精度 10cm
    constexpr float MaxSegmentLength = 100.0f; // 强制分段最大距离

    // 起点
    Slices.Add({
        Spline->GetLocationAtDistanceAlongSpline(0, ESplineCoordinateSpace::Local),
        Spline->GetRightVectorAtDistanceAlongSpline(0, ESplineCoordinateSpace::Local),
        Spline->GetUpVectorAtDistanceAlongSpline(0, ESplineCoordinateSpace::Local),
        Spline->GetTangentAtDistanceAlongSpline(0, ESplineCoordinateSpace::Local).GetSafeNormal(),
        0.0f
    });

    FVector LastTangent = Slices[0].Tangent;
    float LastSliceDist = 0.0f;

    // 遍历
    for (float Dist = CheckStep; Dist < SplineLength; Dist += CheckStep)
    {
        FVector CurrentTangent = Spline->GetTangentAtDistanceAlongSpline(Dist, ESplineCoordinateSpace::Local).GetSafeNormal();

        // 计算角度变化
        float Dot = FVector::DotProduct(LastTangent, CurrentTangent);
        float Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Dot, -1.0f, 1.0f)));
        float DistSinceLast = Dist - LastSliceDist;

        if (Angle >= AngleThreshold || DistSinceLast >= MaxSegmentLength)
        {
            Slices.Add({
                Spline->GetLocationAtDistanceAlongSpline(Dist, ESplineCoordinateSpace::Local),
                Spline->GetRightVectorAtDistanceAlongSpline(Dist, ESplineCoordinateSpace::Local),
                Spline->GetUpVectorAtDistanceAlongSpline(Dist, ESplineCoordinateSpace::Local),
                CurrentTangent,
                Dist
            });
            LastTangent = CurrentTangent;
            LastSliceDist = Dist;
        }
    }

    // 终点
    if (LastSliceDist < SplineLength)
    {
        Slices.Add({
            Spline->GetLocationAtDistanceAlongSpline(SplineLength, ESplineCoordinateSpace::Local),
            Spline->GetRightVectorAtDistanceAlongSpline(SplineLength, ESplineCoordinateSpace::Local),
            Spline->GetUpVectorAtDistanceAlongSpline(SplineLength, ESplineCoordinateSpace::Local),
            Spline->GetTangentAtDistanceAlongSpline(SplineLength, ESplineCoordinateSpace::Local).GetSafeNormal(),
            SplineLength
        });
    }

    // --- 2. 构建几何体 (Box Extrusion) ---
    if (Slices.Num() < 2) return;


    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    TArray<FVector> Normals;
    TArray<FVector2D> UVs;
    TArray<FProcMeshTangent> Tangents;
    TArray<FLinearColor> Colors;

    const float HalfWidth = Width * 0.5f;
    const float HalfThick = Thickness * 0.5f;

    // 辅助 Lambda：添加四边形 (两个三角形)
    auto AddQuad = [&](int32 V0, int32 V1, int32 V2, int32 V3)
    {
        Triangles.Add(V0);
        Triangles.Add(V1);
        Triangles.Add(V2);
        Triangles.Add(V2);
        Triangles.Add(V1);
        Triangles.Add(V3);
    };

    // 我们将分别生成 Top, Bottom, Left, Right 四个面
    // 这样做是为了让每个面有独立的法线 (Hard Edges)

    int32 NumSlices = Slices.Num();
    int32 VertexOffset = 0;

    // --- A. 顶面 (Top Face) ---
    for (int32 i = 0; i < NumSlices; i++)
    {
        const auto& Slice = Slices[i];
        // 顶面稍微向上偏移 HalfThick
        FVector Center = Slice.Location + (Slice.Up * HalfThick);

        Vertices.Add(Center - (Slice.Right * HalfWidth)); // Left
        Vertices.Add(Center + (Slice.Right * HalfWidth)); // Right

        Normals.Add(Slice.Up); // 法线向上
        Normals.Add(Slice.Up);

        // UV: X=0/1, Y=Distance
        UVs.Add(FVector2D(0.0f, Slice.Distance / UVScale));
        UVs.Add(FVector2D(1.0f, Slice.Distance / UVScale));

        Tangents.Add(FProcMeshTangent(Slice.Tangent, false));
        Tangents.Add(FProcMeshTangent(Slice.Tangent, false));

        Colors.Add(FLinearColor::White);
        Colors.Add(FLinearColor::White);
    }

    // 顶面索引
    for (int32 i = 0; i < NumSlices - 1; i++)
    {
        int32 Base = VertexOffset + (i * 2);
        AddQuad(Base, Base + 1, Base + 2, Base + 3);
    }
    VertexOffset += NumSlices * 2;

    // --- B. 底面 (Bottom Face) ---
    for (int32 i = 0; i < NumSlices; i++)
    {
        const auto& Slice = Slices[i];
        FVector Center = Slice.Location - (Slice.Up * HalfThick);

        Vertices.Add(Center - (Slice.Right * HalfWidth));
        Vertices.Add(Center + (Slice.Right * HalfWidth));

        Normals.Add(-Slice.Up); // 法线向下
        Normals.Add(-Slice.Up);

        UVs.Add(FVector2D(0.0f, Slice.Distance / UVScale));
        UVs.Add(FVector2D(1.0f, Slice.Distance / UVScale));

        Tangents.Add(FProcMeshTangent(Slice.Tangent, false));
        Tangents.Add(FProcMeshTangent(Slice.Tangent, false));

        Colors.Add(FLinearColor::Gray);
        Colors.Add(FLinearColor::Gray);
    }

    // 底面索引 (注意顺序，底面要朝下，所以顶点顺序要反过来或者交换)
    for (int32 i = 0; i < NumSlices - 1; i++)
    {
        int32 Base = VertexOffset + (i * 2);
        // 交换 V1 和 V2 的位置以翻转法线方向
        AddQuad(Base + 1, Base, Base + 3, Base + 2);
    }
    VertexOffset += NumSlices * 2;

    // --- C. 左侧面 (Left Face) ---
    for (int32 i = 0; i < NumSlices; i++)
    {
        const auto& Slice = Slices[i];
        FVector TopL = Slice.Location + (Slice.Up * HalfThick) - (Slice.Right * HalfWidth);
        FVector BotL = Slice.Location - (Slice.Up * HalfThick) - (Slice.Right * HalfWidth);

        Vertices.Add(TopL);
        Vertices.Add(BotL);

        Normals.Add(-Slice.Right); // 法线向左
        Normals.Add(-Slice.Right);

        // 侧面 UV 简单映射
        UVs.Add(FVector2D(Slice.Distance / UVScale, 0.0f));
        UVs.Add(FVector2D(Slice.Distance / UVScale, 1.0f));

        Tangents.Add(FProcMeshTangent(Slice.Tangent, false));
        Tangents.Add(FProcMeshTangent(Slice.Tangent, false));

        Colors.Add(FLinearColor::Gray);
        Colors.Add(FLinearColor::Gray);
    }

    for (int32 i = 0; i < NumSlices - 1; i++)
    {
        int32 Base = VertexOffset + (i * 2);
        AddQuad(Base, Base + 2, Base + 1, Base + 3);
    }
    VertexOffset += NumSlices * 2;

    // --- D. 右侧面 (Right Face) ---
    for (int32 i = 0; i < NumSlices; i++)
    {
        const auto& Slice = Slices[i];
        FVector TopR = Slice.Location + (Slice.Up * HalfThick) + (Slice.Right * HalfWidth);
        FVector BotR = Slice.Location - (Slice.Up * HalfThick) + (Slice.Right * HalfWidth);

        Vertices.Add(TopR);
        Vertices.Add(BotR);

        Normals.Add(Slice.Right); // 法线向右
        Normals.Add(Slice.Right);

        UVs.Add(FVector2D(Slice.Distance / UVScale, 0.0f));
        UVs.Add(FVector2D(Slice.Distance / UVScale, 1.0f));

        Tangents.Add(FProcMeshTangent(Slice.Tangent, false));
        Tangents.Add(FProcMeshTangent(Slice.Tangent, false));

        Colors.Add(FLinearColor::Gray);
        Colors.Add(FLinearColor::Gray);
    }

    for (int32 i = 0; i < NumSlices - 1; i++)
    {
        int32 Base = VertexOffset + (i * 2);
        AddQuad(Base + 2, Base, Base + 3, Base + 1);
    }

    // --- 3. 提交数据 --- 
    TargetMesh->CreateMeshSection_LinearColor(
        NextSectionIndex,
        Vertices,
        Triangles,
        Normals,
        UVs,
        Colors,
        Tangents,
        true // 开启碰撞
    );

    if (Material)
    {
        TargetMesh->SetMaterial(NextSectionIndex, Material);
    }

    NextSectionIndex++;
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
    for (FVector& Pt : BeltPoints)
    {
        Pt.Z += 20.0f;
    }

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
