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
#include "Components/InstancedStaticMeshComponent.h"

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
    NewTrajectory.Speed = FGameConst::ItemSpace * 2; // 1秒6个物品

    int32 Index = BeltTrajectories.Add(NewTrajectory);

    // 预烘焙 Spline 为 LUT：只在传送带创建时采样一次（50cm 间距）
    // 之后 GetTransformAtDistance 走 O(1) 数组查表，不再访问 USplineComponent
    BeltTrajectories[Index].BakeLUT(50.0f);

    FBeltHandle NewHandle;
    NewHandle.Index = Index;
    NewHandle.Generation = 0; // TODO Implement generation check if needed

    // 预登记传送带数据（后续 ProvideItemToBelt 用）
    FBeltData& BeltData = BeltEntityRegistry.Add(NewHandle);
    BeltData.BeltLength = NewTrajectory.TotalLength;
    BeltData.BeltSpeed = NewTrajectory.Speed;

    // TODO 大规模测试时性能有很大问题
    if (Material)
    {
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

// TODO 通过枚举管理传送带类型，支持不同的材质实例
void UMassDspManager::GenerateConveyorMesh(
    UProceduralMeshComponent* TargetMesh,
    const USplineComponent* Spline,
    UMaterialInterface* Material,
    float Width,
    float Thickness,
    float UVScale,
    float AngleThreshold,
    float BeltSpeed) // 新增Speed参数
{
    if (!TargetMesh || !Spline || Spline->GetNumberOfSplinePoints() < 2) return;

    // --- 1. 计算自适应切片 (Adaptive Slicing) ---
    TArray<FConveyorSlice> Slices;
    const float SplineLength = Spline->GetSplineLength();
    constexpr float CheckStep = 10.0f; // 采样精度 10cm
    constexpr float MaxSegmentLength = 100.0f; // 强制分段最大距离

    // 方向帧采样时偏离两端的安全距离（避开 UE 样条端点方向帧翻转的问题）
    constexpr float EndpointBias = 0.1f;
    const float SafeStart = FMath::Min(EndpointBias, SplineLength * 0.01f);
    const float SafeEnd = FMath::Max(SplineLength - EndpointBias, SplineLength * 0.99f);

    // 起点：位置取 0，方向帧从 SafeStart 处采样
    Slices.Add({
        Spline->GetLocationAtDistanceAlongSpline(0.0f, ESplineCoordinateSpace::Local),
        Spline->GetRightVectorAtDistanceAlongSpline(SafeStart, ESplineCoordinateSpace::Local),
        Spline->GetUpVectorAtDistanceAlongSpline(SafeStart, ESplineCoordinateSpace::Local),
        Spline->GetTangentAtDistanceAlongSpline(SafeStart, ESplineCoordinateSpace::Local).GetSafeNormal(),
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

    // 终点：位置取 SplineLength，方向帧从 SafeEnd 处采样，避开端点翻转
    if (LastSliceDist < SplineLength)
    {
        Slices.Add({
            Spline->GetLocationAtDistanceAlongSpline(SplineLength, ESplineCoordinateSpace::Local),
            Spline->GetRightVectorAtDistanceAlongSpline(SafeEnd, ESplineCoordinateSpace::Local),
            Spline->GetUpVectorAtDistanceAlongSpline(SafeEnd, ESplineCoordinateSpace::Local),
            Spline->GetTangentAtDistanceAlongSpline(SafeEnd, ESplineCoordinateSpace::Local).GetSafeNormal(),
            SplineLength
        });
    }

    // --- 2. 构建几何体，追加到PendingBeltMesh ---

    // 关键：IndexOffset = 已有顶点数
    int32 IndexOffset = PendingBeltMesh.Vertices.Num();

    const float HalfWidth = Width * 0.5f;
    const float HalfThick = Thickness * 0.5f;

    // Speed编码到顶点色R通道
    FLinearColor TopColor(BeltSpeed / 1000.f, 0, 0, 1);
    FLinearColor SideColor(0, 0, 0, 1);

    auto AddQuad = [&](int32 V0, int32 V1, int32 V2, int32 V3)
    {
        PendingBeltMesh.Triangles.Add(IndexOffset + V0);
        PendingBeltMesh.Triangles.Add(IndexOffset + V1);
        PendingBeltMesh.Triangles.Add(IndexOffset + V2);
        PendingBeltMesh.Triangles.Add(IndexOffset + V2);
        PendingBeltMesh.Triangles.Add(IndexOffset + V1);
        PendingBeltMesh.Triangles.Add(IndexOffset + V3);
    };

    // 顶点局部IndexOffset（相对本次追加的起点）
    int32 LocalOffset = 0;
    int32 NumSlices = Slices.Num();

    // --- A. 顶面 ---
    for (int32 i = 0; i < NumSlices; i++)
    {
        const auto& Slice = Slices[i];
        FVector Center = Slice.Location + (Slice.Up * HalfThick);
        PendingBeltMesh.Vertices.Add(Center - (Slice.Right * HalfWidth));
        PendingBeltMesh.Vertices.Add(Center + (Slice.Right * HalfWidth));
        PendingBeltMesh.Normals.Add(Slice.Up);
        PendingBeltMesh.Normals.Add(Slice.Up);
        PendingBeltMesh.UVs.Add(FVector2D(0.0f, Slice.Distance / UVScale));
        PendingBeltMesh.UVs.Add(FVector2D(1.0f, Slice.Distance / UVScale));
        PendingBeltMesh.Tangents.Add(FProcMeshTangent(Slice.Tangent, false));
        PendingBeltMesh.Tangents.Add(FProcMeshTangent(Slice.Tangent, false));
        PendingBeltMesh.Colors.Add(TopColor);
        PendingBeltMesh.Colors.Add(TopColor);
    }
    for (int32 i = 0; i < NumSlices - 1; i++)
    {
        int32 Base = LocalOffset + (i * 2);
        AddQuad(Base, Base + 1, Base + 2, Base + 3);
    }
    LocalOffset += NumSlices * 2;

    // --- B/C/D 底面、左侧、右侧（结构完全同原来，只改两点）---
    // 1. 所有 Vertices/Normals 等 改成 PendingBeltMesh.Vertices 等
    // 2. 所有 Colors 改成 SideColor
    // 3. AddQuad里的Base用LocalOffset而非VertexOffset
    for (int32 i = 0; i < NumSlices; i++)
    {
        const auto& Slice = Slices[i];
        FVector Center = Slice.Location - (Slice.Up * HalfThick);
        PendingBeltMesh.Vertices.Add(Center - (Slice.Right * HalfWidth));
        PendingBeltMesh.Vertices.Add(Center + (Slice.Right * HalfWidth));
        PendingBeltMesh.Normals.Add(-Slice.Up);
        PendingBeltMesh.Normals.Add(-Slice.Up);
        PendingBeltMesh.UVs.Add(FVector2D(0.0f, Slice.Distance / UVScale));
        PendingBeltMesh.UVs.Add(FVector2D(1.0f, Slice.Distance / UVScale));
        PendingBeltMesh.Tangents.Add(FProcMeshTangent(Slice.Tangent, false));
        PendingBeltMesh.Tangents.Add(FProcMeshTangent(Slice.Tangent, false));
        PendingBeltMesh.Colors.Add(SideColor);
        PendingBeltMesh.Colors.Add(SideColor);
    }
    for (int32 i = 0; i < NumSlices - 1; i++)
    {
        int32 Base = LocalOffset + (i * 2);
        AddQuad(Base, Base + 2, Base + 1, Base + 3);
    }
    LocalOffset += NumSlices * 2;

    // --- D. 右侧面 (Right Face) ---
    for (int32 i = 0; i < NumSlices; i++)
    {
        const auto& Slice = Slices[i];
        FVector TopR = Slice.Location + (Slice.Up * HalfThick) + (Slice.Right * HalfWidth);
        FVector BotR = Slice.Location - (Slice.Up * HalfThick) + (Slice.Right * HalfWidth);

        PendingBeltMesh.Vertices.Add(TopR);
        PendingBeltMesh.Vertices.Add(BotR);

        PendingBeltMesh.Normals.Add(Slice.Right); // 法线向右
        PendingBeltMesh.Normals.Add(Slice.Right);

        PendingBeltMesh.UVs.Add(FVector2D(Slice.Distance / UVScale, 0.0f));
        PendingBeltMesh.UVs.Add(FVector2D(Slice.Distance / UVScale, 1.0f));

        PendingBeltMesh.Tangents.Add(FProcMeshTangent(Slice.Tangent, false));
        PendingBeltMesh.Tangents.Add(FProcMeshTangent(Slice.Tangent, false));

        PendingBeltMesh.Colors.Add(FLinearColor::Gray);
        PendingBeltMesh.Colors.Add(FLinearColor::Gray);
    }

    for (int32 i = 0; i < NumSlices - 1; i++)
    {
        int32 Base = LocalOffset + (i * 2);
        AddQuad(Base + 2, Base, Base + 3, Base + 1);
    }
}

// TODO 优化成异步的(如果要开启碰撞)
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
        // in-tangent 取 A→B 方向，避免首段 Hermite 曲线在 B 点切线为零导致变形
        FVector EntryTangent = BeltPoints[1] - BeltPoints[0];
        NewSpline->SetTangentsAtSplinePoint(1, EntryTangent, StartTangent, ESplineCoordinateSpace::World, false);

        NewSpline->SetSplinePointType(2, ESplinePointType::CurveCustomTangent, false);
        // out-tangent 取 C→D 方向，避免尾段 Hermite 曲线在 C 点切线为零导致直线传送带末端扭曲
        FVector ExitTangent = BeltPoints[3] - BeltPoints[2];
        NewSpline->SetTangentsAtSplinePoint(2, EndTangent, ExitTangent, ESplineCoordinateSpace::World, false);

        NewSpline->SetSplinePointType(3, ESplinePointType::Linear, false);
    }, Material, 50);

    if (!BeltHandle.IsValid()) return FBeltHandle();

    const FBeltTrajectory& Trajectory = BeltTrajectories[BeltHandle.Index];

    StartSlot->ConnectedLaneHandle = EndSlot->ConnectedLaneHandle = BeltHandle;
    StartSlot->BeltSpeed = EndSlot->BeltSpeed = Trajectory.Speed;

    return BeltHandle;
}

bool UMassDspManager::ProvideItemToBelt(FBeltHandle BeltHandle, const TFunction<EItemType()>& GetItemFunc)
{
    if (!BeltHandle.IsValid()) return false;

    // 检查入口是否有空间（最新的物品是数组末尾）
    if (const FBeltData* BeltData = BeltEntityRegistry.Find(BeltHandle))
    {
        if (!BeltData->ItemCache.IsEmpty())
        {
            if (BeltData->ItemCache.Last().DistanceAlongBelt <= FGameConst::HalfLength * 3 + FGameConst::MinSpacing)
                return false;
        }
    }

    const EItemType ItemType = GetItemFunc();
    if (ItemType == EItemType::None) return false;

    FBeltData& BeltData = BeltEntityRegistry.FindOrAdd(BeltHandle);

    // 如果还没有初始化传送带参数，从轨迹同步
    if (BeltData.BeltLength <= 0.f && BeltTrajectories.IsValidIndex(BeltHandle.Index))
    {
        const FBeltTrajectory& Belt = BeltTrajectories[BeltHandle.Index];
        BeltData.BeltLength = Belt.TotalLength;
        BeltData.BeltSpeed = Belt.Speed;
    }

    FBeltItemCache NewItem;
    NewItem.DistanceAlongBelt = FGameConst::HalfLength;
    NewItem.ItemType = ItemType;
    NewItem.bIsBlocked = false;
    BeltData.ItemCache.PushLast(NewItem);

    return true;
}

EItemType UMassDspManager::ConsumeItemFromBelt(FBeltHandle BeltHandle, const TFunction<bool(EItemType)>& ValidateItemFunc)
{
    FBeltData* BeltData = BeltEntityRegistry.Find(BeltHandle);
    if (!BeltData || BeltData->ItemCache.IsEmpty()) return EItemType::None;

    // 第一个物品是最老的（最靠近末端）
    FBeltItemCache& FirstItem = BeltData->ItemCache[0];
    if (FirstItem.bIsBlocked && ValidateItemFunc(FirstItem.ItemType))
    {
        const EItemType ConsumedType = FirstItem.ItemType;
        BeltData->ItemCache.PopFirst();
        return ConsumedType;
    }

    return EItemType::None;
}

// ===== ISM 物品渲染池 =====

UInstancedStaticMeshComponent* UMassDspManager::GetOrCreateIsmForItemType(EItemType ItemType)
{
    if (UInstancedStaticMeshComponent** Found = ItemISMPool.Find(ItemType))
        return *Found;

    TryGetGameMode();
    if (!GameMode.IsValid() || !BeltsContainerActor) return nullptr;
    if (!GameMode->GameConfig) return nullptr;

    const FItemConfigData* ConfigData = GameMode->GameConfig->GetItemConfig(ItemType);
    if (!ConfigData || !ConfigData->Mesh) return nullptr;

    UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(BeltsContainerActor);
    ISM->SetupAttachment(BeltsContainerActor->GetRootComponent());
    ISM->SetStaticMesh(ConfigData->Mesh);
    if (ConfigData->Material)
    {
        ISM->SetMaterial(0, ConfigData->Material);
    }
    ISM->SetCastShadow(false);
    ISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    // 超出阈值距离后 GPU 自动剔除渲染，与 CPU 侧距离裁剪形成双层保护
    ISM->SetCullDistances(NearDistanceThreshold * 0.8f, NearDistanceThreshold);
    ISM->RegisterComponent();

    ItemISMPool.Add(ItemType, ISM);
    return ISM;
}

void UMassDspManager::UpdateAllBeltItemTransforms(FVector CameraPos)
{
    if (BeltEntityRegistry.IsEmpty()) return;

    const float NearDistSq = NearDistanceThreshold * NearDistanceThreshold;

    // ── Step 0: 缓存 Belt 指针，同时做距离判断，跳过远处 Belt ─────────────────
    TArray<FBeltHandle> ActiveBelts;
    BeltEntityRegistry.GetKeys(ActiveBelts);
    const int32 NumBelts = ActiveBelts.Num();

    TArray<const FBeltData*> BeltDataPtrs;
    TArray<bool> BeltIsNear;
    BeltDataPtrs.Reserve(NumBelts);
    BeltIsNear.SetNumUninitialized(NumBelts);

    int32 TotalNearItems = 0;
    for (int32 i = 0; i < NumBelts; ++i)
    {
        const FBeltHandle& Handle = ActiveBelts[i];
        const FBeltData* Data = BeltEntityRegistry.Find(Handle);
        BeltDataPtrs.Add(Data);

        bool bNear = false;
        if (BeltTrajectories.IsValidIndex(Handle.Index))
        {
            const float DistSq = FVector::DistSquared(
                BeltTrajectories[Handle.Index].RepresentativePosition, CameraPos);
            bNear = (DistSq <= NearDistSq);
        }
        BeltIsNear[i] = bNear;
        if (bNear && Data) TotalNearItems += Data->ItemCache.Num();
    }

    if (TotalNearItems == 0)
    {
        // 近处无物品：清空 ISM 实例
        for (auto& [Type, ISM] : ItemISMPool)
            if (ISM && ISM->GetInstanceCount() > 0)
                ISM->ClearInstances();
        return;
    }

    // ── Step 1: 前缀和（仅近处 Belt）────────────────────────────────────────
    TArray<int32> BeltItemOffsets;
    BeltItemOffsets.SetNumUninitialized(NumBelts);
    int32 RunningOffset = 0;
    for (int32 i = 0; i < NumBelts; ++i)
    {
        BeltItemOffsets[i] = RunningOffset;
        if (BeltIsNear[i] && BeltDataPtrs[i])
            RunningOffset += BeltDataPtrs[i]->ItemCache.Num();
    }

    // ── Step 2: 预分配平坦输出数组（仅近处物品，无远处开销）──────────────────
    struct FItemEntry
    {
        EItemType Type;
        FTransform T;
    };
    TArray<FItemEntry> FlatEntries;
    FlatEntries.SetNumUninitialized(TotalNearItems);

    // ── Step 3: ParallelFor 并行查 LUT（仅近处 Belt，零远处开销）──────────────
    ParallelFor(NumBelts, [&](int32 BeltIdx)
    {
        if (!BeltIsNear[BeltIdx]) return; // 远处：完全跳过，零开销

        const FBeltData* BeltData = BeltDataPtrs[BeltIdx];
        if (!BeltData || BeltData->ItemCache.IsEmpty()) return;

        const FBeltHandle& Handle = ActiveBelts[BeltIdx];
        if (!BeltTrajectories.IsValidIndex(Handle.Index)) return;

        const FBeltTrajectory& Trajectory = BeltTrajectories[Handle.Index];
        if (!Trajectory.IsValid()) return;

        int32 WriteIdx = BeltItemOffsets[BeltIdx];
        for (const FBeltItemCache& Cache : BeltData->ItemCache)
        {
            FItemEntry& Entry = FlatEntries[WriteIdx++];
            Entry.Type = Cache.ItemType;
            if (Cache.ItemType != EItemType::None)
                Trajectory.GetTransformAtDistance(Cache.DistanceAlongBelt, Entry.T);
        }
    });

    // ── Step 4: Bucket Sort ──────────────────────────────────────────────────
    for (auto& [Type, Arr] : CachedTransformsByType)
        Arr.Reset();

    for (const FItemEntry& Entry : FlatEntries)
        if (Entry.Type != EItemType::None)
            CachedTransformsByType.FindOrAdd(Entry.Type).Add(Entry.T);

    // ── Step 5: 每种物品类型 1 次 BatchUpdate，数据量 = 近处物品数 ────────────
    for (auto& [Type, Transforms] : CachedTransformsByType)
    {
        UInstancedStaticMeshComponent* ISM = GetOrCreateIsmForItemType(Type);
        if (!ISM) continue;

        const int32 NewCount = Transforms.Num();
        const int32 OldCount = ISM->GetInstanceCount();

        if (NewCount > OldCount)
        {
            // 扩容：追加新 Instance（先隐藏）
            const FTransform HiddenTransform(FVector(0.f, 0.f, -99999.f));
            for (int32 i = OldCount; i < NewCount; ++i)
                ISM->AddInstance(HiddenTransform, false);
        }
        else if (NewCount < OldCount)
        {
            TArray<int32> ToRemove;
            ToRemove.Reserve(OldCount - NewCount);
            for (int32 i = NewCount; i < OldCount; ++i)
                ToRemove.Add(i);
            ISM->RemoveInstances(ToRemove);
        }

        if (NewCount > 0)
            ISM->BatchUpdateInstancesTransforms(0, Transforms, false, true, true);
    }

    // 清理本帧不再需要实例的 ISM 类型（物品耗尽时）
    for (auto& [Type, ISM] : ItemISMPool)
        if (ISM && !CachedTransformsByType.Contains(Type) && ISM->GetInstanceCount() > 0)
            ISM->ClearInstances();
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

void UMassDspManager::FlushBeltMesh(UMaterialInterface* Material) const
{
    if (!BeltProceduralMesh) return;

    // 所有传送带合并 = 永远只有Section 0 = 1个DrawCall
    BeltProceduralMesh->CreateMeshSection_LinearColor(
        0,
        PendingBeltMesh.Vertices,
        PendingBeltMesh.Triangles,
        PendingBeltMesh.Normals,
        PendingBeltMesh.UVs,
        PendingBeltMesh.Colors,
        PendingBeltMesh.Tangents,
        false
    );

    if (Material)
    {
        BeltProceduralMesh->SetMaterial(0, Material);
    }
}
