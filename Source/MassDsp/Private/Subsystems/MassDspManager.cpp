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
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

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
    CancelAnyPreview();

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

FBeltHandle UMassDspManager::CreateRuntimeBelt(const TFunction<void(USplineComponent*)>& InitSpline, EBeltType BeltType)
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

    // --- 从 BeltType 配置读取速度 ---
    float BeltSpeed = FGameConst::ItemSpace * 2; // 默认速度（fallback）
    TryGetGameMode();
    if (GameMode.IsValid() && GameMode->GameConfig)
    {
        if (const FBeltTypeConfig* Config = GameMode->GameConfig->GetBeltTypeConfig(BeltType))
        {
            BeltSpeed = Config->Speed;
        }
    }

    // --- 创建 Handle 并存储 ---
    FBeltTrajectory NewTrajectory;
    NewTrajectory.SplineComponent = NewSpline;
    NewTrajectory.TotalLength = NewSpline->GetSplineLength();
    NewTrajectory.Speed = BeltSpeed;

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

    // --- 生成网格几何，追加到对应 BeltType 的 PendingBeltMeshMap 条目 ---
    if (BeltType != EBeltType::None)
    {
        if (!BeltProceduralMesh)
        {
            BeltProceduralMesh = NewObject<UProceduralMeshComponent>(BeltsContainerActor, TEXT("BeltProceduralMesh"));
            BeltProceduralMesh->SetupAttachment(BeltsContainerActor->GetRootComponent());
            BeltProceduralMesh->SetVisibility(true);
            BeltProceduralMesh->SetCastShadow(false);
            BeltProceduralMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            BeltProceduralMesh->SetCullDistance(MaxRenderDistance * 2);
            BeltProceduralMesh->RegisterComponent();
        }

        FMergedBeltMeshData& MeshData = PendingBeltMeshMap.FindOrAdd(BeltType);
        GenerateConveyorMesh(MeshData, NewSpline, C_Width, C_BeltThickness, C_UVScale, 5.0f);
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
    FMergedBeltMeshData& OutMesh,
    const USplineComponent* Spline,
    float Width,
    float Thickness,
    float UVScale,
    float AngleThreshold)
{
    if (!Spline || Spline->GetNumberOfSplinePoints() < 2) return;

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

    // --- 2. 构建几何体，追加到 OutMesh ---

    // 关键：IndexOffset = 已有顶点数
    int32 IndexOffset = OutMesh.Vertices.Num();

    const float HalfWidth = Width * 0.5f;
    const float HalfThick = Thickness * 0.5f;

    auto AddQuad = [&](int32 V0, int32 V1, int32 V2, int32 V3)
    {
        OutMesh.Triangles.Add(IndexOffset + V0);
        OutMesh.Triangles.Add(IndexOffset + V1);
        OutMesh.Triangles.Add(IndexOffset + V2);
        OutMesh.Triangles.Add(IndexOffset + V2);
        OutMesh.Triangles.Add(IndexOffset + V1);
        OutMesh.Triangles.Add(IndexOffset + V3);
    };

    // 顶点局部IndexOffset（相对本次追加的起点）
    int32 LocalOffset = 0;
    int32 NumSlices = Slices.Num();

    // --- A. 顶面 ---
    for (int32 i = 0; i < NumSlices; i++)
    {
        const auto& [Location, Right, Up, Tangent, Distance] = Slices[i];
        FVector Center = Location + (Up * HalfThick);
        OutMesh.Vertices.Add(Center - (Right * HalfWidth));
        OutMesh.Vertices.Add(Center + (Right * HalfWidth));
        OutMesh.Normals.Add(Up);
        OutMesh.Normals.Add(Up);
        OutMesh.UVs.Add(FVector2D(0.0f, Distance / UVScale));
        OutMesh.UVs.Add(FVector2D(1.0f, Distance / UVScale));
        OutMesh.Tangents.Add(FProcMeshTangent(Tangent, false));
        OutMesh.Tangents.Add(FProcMeshTangent(Tangent, false));
        OutMesh.Colors.Add(FLinearColor::White);
        OutMesh.Colors.Add(FLinearColor::White);
    }
    for (int32 i = 0; i < NumSlices - 1; i++)
    {
        int32 Base = LocalOffset + (i * 2);
        AddQuad(Base, Base + 1, Base + 2, Base + 3);
    }
    LocalOffset += NumSlices * 2;

    // --- B. 底面 ---
    for (int32 i = 0; i < NumSlices; i++)
    {
        const auto& Slice = Slices[i];
        FVector Center = Slice.Location - (Slice.Up * HalfThick);
        OutMesh.Vertices.Add(Center - (Slice.Right * HalfWidth));
        OutMesh.Vertices.Add(Center + (Slice.Right * HalfWidth));
        OutMesh.Normals.Add(-Slice.Up);
        OutMesh.Normals.Add(-Slice.Up);
        OutMesh.UVs.Add(FVector2D(0.0f, Slice.Distance / UVScale));
        OutMesh.UVs.Add(FVector2D(1.0f, Slice.Distance / UVScale));
        OutMesh.Tangents.Add(FProcMeshTangent(Slice.Tangent, false));
        OutMesh.Tangents.Add(FProcMeshTangent(Slice.Tangent, false));
        OutMesh.Colors.Add(FLinearColor::White);
        OutMesh.Colors.Add(FLinearColor::White);
    }
    for (int32 i = 0; i < NumSlices - 1; i++)
    {
        int32 Base = LocalOffset + (i * 2);
        AddQuad(Base, Base + 2, Base + 1, Base + 3);
    }
    LocalOffset += NumSlices * 2;

    // --- C. 左侧面 (Left Face) ---
    // UV: U=0.5 固定在中心区域（无边框效果），V=Distance/UVScale 沿传送带方向
    for (int32 i = 0; i < NumSlices; i++)
    {
        const auto& [Location, Right, Up, Tangent, Distance] = Slices[i];
        FVector TopL = Location + (Up * HalfThick) - (Right * HalfWidth);
        FVector BotL = Location - (Up * HalfThick) - (Right * HalfWidth);

        OutMesh.Vertices.Add(TopL);
        OutMesh.Vertices.Add(BotL);

        OutMesh.Normals.Add(-Right);
        OutMesh.Normals.Add(-Right);

        OutMesh.UVs.Add(FVector2D(0.5f, Distance / UVScale));
        OutMesh.UVs.Add(FVector2D(0.5f, Distance / UVScale));

        OutMesh.Tangents.Add(FProcMeshTangent(Tangent, false));
        OutMesh.Tangents.Add(FProcMeshTangent(Tangent, false));

        OutMesh.Colors.Add(FLinearColor(0.0f, 0.5f, 0.5f, 1.0f));
        OutMesh.Colors.Add(FLinearColor(0.0f, 0.5f, 0.5f, 1.0f));
    }
    for (int32 i = 0; i < NumSlices - 1; i++)
    {
        int32 Base = LocalOffset + (i * 2);
        // 与右侧面绕序相反，使法线朝 -Right
        AddQuad(Base + 2, Base + 3, Base, Base + 1);
    }
    LocalOffset += NumSlices * 2;

    // --- D. 右侧面 (Right Face) ---
    // UV: U=0.5 固定在中心区域（避免 Distance=0 时落入边框区产生箭头动画），V=Distance/UVScale 沿传送带方向
    for (int32 i = 0; i < NumSlices; i++)
    {
        const auto& [Location, Right, Up, Tangent, Distance] = Slices[i];
        FVector TopR = Location + (Up * HalfThick) + (Right * HalfWidth);
        FVector BotR = Location - (Up * HalfThick) + (Right * HalfWidth);

        OutMesh.Vertices.Add(TopR);
        OutMesh.Vertices.Add(BotR);

        OutMesh.Normals.Add(Right);
        OutMesh.Normals.Add(Right);

        OutMesh.UVs.Add(FVector2D(0.5f, Distance / UVScale));
        OutMesh.UVs.Add(FVector2D(0.5f, Distance / UVScale));

        OutMesh.Tangents.Add(FProcMeshTangent(Tangent, false));
        OutMesh.Tangents.Add(FProcMeshTangent(Tangent, false));

        OutMesh.Colors.Add(FLinearColor(0.0f, 0.5f, 0.5f, 1.0f));
        OutMesh.Colors.Add(FLinearColor(0.0f, 0.5f, 0.5f, 1.0f));
    }
    for (int32 i = 0; i < NumSlices - 1; i++)
    {
        int32 Base = LocalOffset + (i * 2);
        AddQuad(Base + 2, Base, Base + 3, Base + 1);
    }
    LocalOffset += NumSlices * 2;

    // --- E. 起点封口 (Front Cap) ---
    // 面法线朝 -Tangent（传送带入口方向）
    {
        const auto& S = Slices[0];
        FVector TopL = S.Location + (S.Up * HalfThick) - (S.Right * HalfWidth);
        FVector TopR = S.Location + (S.Up * HalfThick) + (S.Right * HalfWidth);
        FVector BotL = S.Location - (S.Up * HalfThick) - (S.Right * HalfWidth);
        FVector BotR = S.Location - (S.Up * HalfThick) + (S.Right * HalfWidth);
        const FVector CapNormal = -S.Tangent;

        // 顶点顺序: TopL(0), TopR(1), BotL(2), BotR(3)
        OutMesh.Vertices.Add(TopL);
        OutMesh.Vertices.Add(TopR);
        OutMesh.Vertices.Add(BotL);
        OutMesh.Vertices.Add(BotR);
        for (int32 j = 0; j < 4; j++) OutMesh.Normals.Add(CapNormal);
        OutMesh.UVs.Add(FVector2D(0.0f, 0.0f));
        OutMesh.UVs.Add(FVector2D(1.0f, 0.0f));
        OutMesh.UVs.Add(FVector2D(0.0f, 1.0f));
        OutMesh.UVs.Add(FVector2D(1.0f, 1.0f));
        for (int32 j = 0; j < 4; j++) OutMesh.Tangents.Add(FProcMeshTangent(S.Right, false));
        for (int32 j = 0; j < 4; j++) OutMesh.Colors.Add(FLinearColor(0.0f, 0.5f, 0.5f, 1.0f));

        // 绕序使法线朝 -Tangent
        AddQuad(LocalOffset + 0, LocalOffset + 2, LocalOffset + 1, LocalOffset + 3);
        LocalOffset += 4;
    }

    // --- F. 终点封口 (Back Cap) ---
    // 面法线朝 +Tangent（传送带出口方向）
    {
        const auto& S = Slices[NumSlices - 1];
        FVector TopL = S.Location + (S.Up * HalfThick) - (S.Right * HalfWidth);
        FVector TopR = S.Location + (S.Up * HalfThick) + (S.Right * HalfWidth);
        FVector BotL = S.Location - (S.Up * HalfThick) - (S.Right * HalfWidth);
        FVector BotR = S.Location - (S.Up * HalfThick) + (S.Right * HalfWidth);
        const FVector CapNormal = S.Tangent;

        OutMesh.Vertices.Add(TopL);
        OutMesh.Vertices.Add(TopR);
        OutMesh.Vertices.Add(BotL);
        OutMesh.Vertices.Add(BotR);
        for (int32 j = 0; j < 4; j++) OutMesh.Normals.Add(CapNormal);
        OutMesh.UVs.Add(FVector2D(0.0f, 0.0f));
        OutMesh.UVs.Add(FVector2D(1.0f, 0.0f));
        OutMesh.UVs.Add(FVector2D(0.0f, 1.0f));
        OutMesh.UVs.Add(FVector2D(1.0f, 1.0f));
        for (int32 j = 0; j < 4; j++) OutMesh.Tangents.Add(FProcMeshTangent(S.Right, false));
        for (int32 j = 0; j < 4; j++) OutMesh.Colors.Add(FLinearColor(0.0f, 0.5f, 0.5f, 1.0f));

        // 绕序使法线朝 +Tangent（与起点封口相反）
        AddQuad(LocalOffset + 0, LocalOffset + 1, LocalOffset + 2, LocalOffset + 3);
        LocalOffset += 4;
    }
}

// TODO 优化成异步的(如果要开启碰撞)
FBeltHandle UMassDspManager::CreateAndLinkBeltForSlot(
    FMassEntityHandle SBuilding, int32 StartSlotIndex, FMassEntityHandle EBuilding, int32 EndSlotIndex, EBeltType BeltType
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

    if (StartSlot->ConnectedLaneHandle.IsValid() || EndSlot->ConnectedLaneHandle.IsValid())
    {
        // 已经有连接了，拒绝创建
        return FBeltHandle();
    }

    FVector A = StartSlot->WorldLocation - StartSlot->WorldRotation * FVector(StartSlot->SlotExtend, 0, 0);
    FVector B = StartSlot->WorldLocation;
    FVector C = EndSlot->WorldLocation;
    FVector D = EndSlot->WorldLocation - EndSlot->WorldRotation * FVector(EndSlot->SlotExtend, 0, 0);

    // TODO 考虑引入中间拐点来减少曲线段的占比
    // 使用通用样条构建接口（与传送带预览共用，保证行为一致）
    FBeltHandle BeltHandle = CreateRuntimeBelt([A, B, C, D](USplineComponent* NewSpline)
    {
        BuildBeltSplineFromPoints(NewSpline, A, B, C, D);
    }, BeltType);

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
    ISM->RegisterComponent();

    ItemISMPool.Add(ItemType, ISM);
    return ISM;
}

void UMassDspManager::UpdateAllBeltItemTransforms(const FConvexVolume& ViewFrustum, const FVector& CameraPos)
{
    if (BeltEntityRegistry.IsEmpty()) return;

    const float MaxDistSq = MaxRenderDistance * MaxRenderDistance;

    // ── Step 0: 双重门控：视锥剔除（平视侧面/背面）+ 距离上限（飞高时防覆盖写） ───
    TArray<FBeltHandle> ActiveBelts;
    BeltEntityRegistry.GetKeys(ActiveBelts);
    const int32 NumBelts = ActiveBelts.Num();

    TArray<const FBeltData*> BeltDataArr;
    TArray<bool> BeltIsVisible;
    BeltDataArr.Reserve(NumBelts);
    BeltIsVisible.SetNumUninitialized(NumBelts);

    int32 TotalVisibleItems = 0;
    for (int32 i = 0; i < NumBelts; ++i)
    {
        const FBeltHandle& Handle = ActiveBelts[i];
        const FBeltData* Data = BeltEntityRegistry.Find(Handle);
        BeltDataArr.Add(Data);

        bool bVisible = false;
        if (BeltTrajectories.IsValidIndex(Handle.Index))
        {
            const FBeltTrajectory& BeltDef = BeltTrajectories[Handle.Index];
            // 门控 1：距离上限（MaxRenderDistance）——飞高时截断覆盖面积
            if (const float DistSq = FVector::DistSquared(CameraPos, BeltDef.RepresentativePosition); DistSq <= MaxDistSq)
            {
                // 门控 2：视锥剔除（IntersectSphere）——平视时切掉侧面/背面
                bVisible = ViewFrustum.IntersectSphere(BeltDef.RepresentativePosition, BeltDef.BoundRadius);
            }
        }
        BeltIsVisible[i] = bVisible;
        if (bVisible && Data) TotalVisibleItems += Data->ItemCache.Num();
    }

    if (TotalVisibleItems == 0)
    {
        // 视野内无物品：清空 ISM 实例
        for (auto& [Type, ISM] : ItemISMPool)
            if (ISM && ISM->GetInstanceCount() > 0)
                ISM->ClearInstances();
        return;
    }

    // ── Step 1: 前缀和（仅视锥内 Belt）─────────────────────────────────────
    TArray<int32> BeltItemOffsets;
    BeltItemOffsets.SetNumUninitialized(NumBelts);
    int32 RunningOffset = 0;
    for (int32 i = 0; i < NumBelts; ++i)
    {
        BeltItemOffsets[i] = RunningOffset;
        if (BeltIsVisible[i] && BeltDataArr[i])
            RunningOffset += BeltDataArr[i]->ItemCache.Num();
    }

    // ── Step 2: 预分配平坦输出数组（仅视锥内物品）──────────────────────────
    struct FItemEntry
    {
        EItemType Type;
        FTransform T;
    };
    TArray<FItemEntry> FlatEntries;
    FlatEntries.SetNumUninitialized(TotalVisibleItems);

    // ── Step 3: ParallelFor 并行查 LUT（仅视锥内 Belt，视野外零开销）─────────
    ParallelFor(NumBelts, [&](int32 BeltIdx)
    {
        if (!BeltIsVisible[BeltIdx]) return; // 视野外：完全跳过，零开销

        const FBeltData* BeltData = BeltDataArr[BeltIdx];
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
    // 预分配与输入等长的数组，保证输出顺序与输入一致；未成功创建的槽位保持默认无效 Handle
    TArray<FMassEntityHandle> CreatedEntities;
    if (SpawnDataList.IsEmpty()) return CreatedEntities;
    CreatedEntities.SetNum(SpawnDataList.Num());
    int32 SuccessCount = 0;

    UMassEntitySubsystem* EntitySubsystem = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
    if (!EntitySubsystem)
    {
        UE_LOG(LogTemp, Error, TEXT("BatchSpawnBuildings: MassEntitySubsystem not found"));
        return CreatedEntities;
    }

    TryGetGameMode();
    if (!GameMode.IsValid() || !GameMode->BeltItemConfigAsset)
    {
        UE_LOG(LogTemp, Error, TEXT("BatchSpawnBuildings: GameMode or BeltItemConfigAsset is null"));
        return CreatedEntities;
    }

    FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
    const FMassEntityTemplate& EntityTemplate = GameMode->BeltItemConfigAsset->GetConfig().GetOrCreateEntityTemplate(*GetWorld());

    // 按 BuildingType 分组，相同类型共用同一 Archetype，一次 BatchCreateEntities
    TMap<EBuildingType, TArray<int32>> TypeToIndices;
    for (int32 i = 0; i < SpawnDataList.Num(); ++i)
    {
        TypeToIndices.FindOrAdd(SpawnDataList[i].BuildingType).Add(i);
    }

    for (auto& [BuildingType, Indices] : TypeToIndices)
    {
        const FBuildingSpawnData& FirstData = SpawnDataList[Indices[0]];
        if (!FirstData.BuildingClass) continue;

        // 获取或创建 Archetype（有缓存则直接取，避免重复 CreateArchetype）
        FMassArchetypeHandle Archetype;
        if (FMassArchetypeHandle* Cached = CachedBuildingArchetypes.Find(BuildingType))
        {
            Archetype = *Cached;
        }
        else
        {
            const AMassDspBuilding* CDO = GetDefault<AMassDspBuilding>(FirstData.BuildingClass);
            if (!CDO) continue;

            FMassArchetypeCompositionDescriptor Composition = EntityTemplate.GetCompositionDescriptor();
            for (const UScriptStruct* FragmentType : CDO->GetStaticStructs())
            {
                Composition.GetContainer<FMassFragment>().Add(*FragmentType);
            }
            Archetype = EntityManager.CreateArchetype(Composition);
            CachedBuildingArchetypes.Add(BuildingType, Archetype);
        }

        if (!Archetype.IsValid()) continue;

        // 一次性批量分配本类型全部实体（核心优化：避免逐个 CreateEntity 的内存碎片和锁开销）
        TArray<FMassEntityHandle> BatchHandles;
        BatchHandles.Reserve(Indices.Num());
        EntityManager.BatchCreateEntities(Archetype, EntityTemplate.GetSharedFragmentValues(), Indices.Num(), BatchHandles);

        // 逐实体初始化 Fragment 数据（创建后必须初始化，无法批量跳过）
        const AMassDspBuilding* CDO = GetDefault<AMassDspBuilding>(FirstData.BuildingClass);
        for (int32 j = 0; j < BatchHandles.Num(); ++j)
        {
            const FMassEntityHandle EntityHandle = BatchHandles[j];
            const FBuildingSpawnData& SpawnData = SpawnDataList[Indices[j]];

            EntityManager.SetEntityFragmentValues(EntityHandle, EntityTemplate.GetInitialFragmentValues());
            CDO->InitFragmentForEntity(EntityManager, EntityHandle, SpawnData.WorldTransform);

            if (FTransformFragment* TransformFrag = EntityManager.GetFragmentDataPtr<FTransformFragment>(EntityHandle))
            {
                TransformFrag->SetTransform(SpawnData.WorldTransform);
            }

            if (FMassRepresentationFragment* RepFrag = EntityManager.GetFragmentDataPtr<FMassRepresentationFragment>(EntityHandle))
            {
                if (auto CachedDesc = CachedBuildingMeshDesc.Find(BuildingType))
                {
                    RepFrag->StaticMeshDescHandle = *CachedDesc;
                }
                else if (const FBuildingTypeConfig* BuildingConfig = GameMode->GameConfig->GetBuildingConfig(BuildingType))
                {
                    RepFrag->StaticMeshDescHandle = BuildingConfig->GetOrCreateMeshHandle(GetWorld());
                    CachedBuildingMeshDesc.Add(BuildingType, RepFrag->StaticMeshDescHandle);
                }
            }

            ++BuildingEntityCount;
            ++SuccessCount;
            CreatedEntities[Indices[j]] = EntityHandle; // 按原始输入下标回写，保证顺序
            SpawnedBuildingEntities.Add(EntityHandle);
        }
    }

    UE_LOG(LogTemp, Log, TEXT("BatchSpawnBuildings: Created %d / %d building entities"), SuccessCount, SpawnDataList.Num());

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

    // 使用缓存的 Archetype（避免每次重建 Composition + CreateArchetype）
    FMassArchetypeHandle CustomArchetype;
    if (FMassArchetypeHandle* Cached = CachedBuildingArchetypes.Find(SpawnData.BuildingType))
    {
        CustomArchetype = *Cached;
    }
    else
    {
        FMassArchetypeCompositionDescriptor Composition = EntityTemplate.GetCompositionDescriptor();
        for (const UScriptStruct* FragmentType : BuildingCDO->GetStaticStructs())
        {
            Composition.GetContainer<FMassFragment>().Add(*FragmentType);
        }
        CustomArchetype = EntityManager.CreateArchetype(Composition);
        CachedBuildingArchetypes.Add(SpawnData.BuildingType, CustomArchetype);
    }

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
        if (auto Cached = CachedBuildingMeshDesc.Find(SpawnData.BuildingType))
        {
            RepFrag->StaticMeshDescHandle = *Cached;
        }
        else if (const FBuildingTypeConfig* BuildingConfig = GameMode->GameConfig->GetBuildingConfig(SpawnData.BuildingType))
        {
            RepFrag->StaticMeshDescHandle = BuildingConfig->GetOrCreateMeshHandle(GetWorld());
            CachedBuildingMeshDesc.Add(SpawnData.BuildingType, RepFrag->StaticMeshDescHandle);
        }
    }

    ++BuildingEntityCount;
    SpawnedBuildingEntities.Add(EntityHandle);

    return EntityHandle;
}

void UMassDspManager::FlushBeltMesh()
{
    if (!BeltProceduralMesh) return;

    TryGetGameMode();

    // 每个 EBeltType 使用独立的 MeshSection，section 索引 = 枚举值
    for (auto& [BeltType, MeshData] : PendingBeltMeshMap)
    {
        const int32 SectionIdx = static_cast<int32>(BeltType);

        BeltProceduralMesh->CreateMeshSection_LinearColor(
            SectionIdx,
            MeshData.Vertices,
            MeshData.Triangles,
            MeshData.Normals,
            MeshData.UVs,
            MeshData.Colors,
            MeshData.Tangents,
            false
        );

        // 按 BeltType 配置创建动态材质实例并应用到对应 Section
        if (BeltMaterializedSet.Add(BeltType).IsValidId() && GameMode.IsValid() && GameMode->GameConfig)
        {
            if (const FBeltTypeConfig* Config = GameMode->GameConfig->GetBeltTypeConfig(BeltType))
            {
                if (Config->Material)
                {
                    // Speed 参数单位：UV/s = BeltSpeed(cm/s) / UVScale(100cm)
                    // ArrowColor 对应 BeltType 配置中的 Color
                    UMaterialInstanceDynamic* DynMat = UMaterialInstanceDynamic::Create(Config->Material, this);
                    DynMat->SetVectorParameterValue(TEXT("ArrowColor"), Config->Color);
                    DynMat->SetScalarParameterValue(TEXT("Speed"), Config->Speed / C_UVScale);
                    BeltProceduralMesh->SetMaterial(SectionIdx, DynMat);
                }
            }
        }
    }
}

// ============================================================
//  建造系统 —— 通用样条构建接口
// ============================================================

void UMassDspManager::BuildBeltSplineFromPoints(USplineComponent* Spline, FVector A, FVector B, FVector C, FVector D)
{
    if (!Spline) return;

    // Z 抬高 20cm，防止穿入地板
    A.Z += 20.f;
    B.Z += 20.f;
    C.Z += 20.f;
    D.Z += 20.f;

    Spline->ClearSplinePoints(false);
    Spline->AddSplinePoint(A, ESplineCoordinateSpace::World, false);
    Spline->AddSplinePoint(B, ESplineCoordinateSpace::World, false);
    Spline->AddSplinePoint(C, ESplineCoordinateSpace::World, false);
    Spline->AddSplinePoint(D, ESplineCoordinateSpace::World, false);

    // 线段方向
    const FVector AB_Dir = (B - A).GetSafeNormal();
    const FVector CD_Dir = (D - C).GetSafeNormal();

    const float AB_Len = FVector::Dist(A, B);
    const float CD_Len = FVector::Dist(C, D);
    const float BC_Len = FVector::Dist(B, C);
    constexpr float TangentScale = 5.f;

    const FVector StartTangent = AB_Dir * FMath::Max(AB_Len * 1.f, BC_Len * 0.4f) * TangentScale;
    const FVector EndTangent = CD_Dir * FMath::Max(CD_Len * 1.f, BC_Len * 0.4f) * TangentScale;

    // 端点 Linear，中间两点 CurveCustomTangent
    Spline->SetSplinePointType(0, ESplinePointType::Linear, false);

    Spline->SetSplinePointType(1, ESplinePointType::CurveCustomTangent, false);
    // in-tangent: A→B 方向（避免 B 点 Hermite 切线为零）
    Spline->SetTangentsAtSplinePoint(1, B - A, StartTangent, ESplineCoordinateSpace::World, false);

    Spline->SetSplinePointType(2, ESplinePointType::CurveCustomTangent, false);
    // out-tangent: C→D 方向（避免 C 点末端扭曲）
    Spline->SetTangentsAtSplinePoint(2, EndTangent, D - C, ESplineCoordinateSpace::World, false);

    Spline->SetSplinePointType(3, ESplinePointType::Linear, false);

    Spline->UpdateSpline();
}

// ============================================================
//  建造系统 —— 工具接口
// ============================================================

TSubclassOf<AMassDspBuilding> UMassDspManager::GetBuildingClassForType(EBuildingType BuildingType)
{
    TryGetGameMode();
    if (!GameMode.IsValid() || !GameMode->GameConfig) return nullptr;
    if (const FBuildingTypeConfig* Cfg = GameMode->GameConfig->GetBuildingConfig(BuildingType))
        return Cfg->BuildingClass;
    return nullptr;
}

bool UMassDspManager::FindNearestBuildingSlot(
    const FVector& WorldPos,
    EBuildingSlotType SlotType,
    float SearchRadius,
    FMassEntityHandle& OutEntity,
    int32& OutSlotIndex,
    FVector& OutSlotLocation,
    FQuat& OutSlotRotation,
    float& OutSlotExtend)
{
    UMassEntitySubsystem* ESub = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
    if (!ESub) return false;

    FMassEntityManager& EM = ESub->GetMutableEntityManager();
    float BestDistSq = SearchRadius * SearchRadius;
    bool bFound = false;

    for (const FMassEntityHandle& Entity : SpawnedBuildingEntities)
    {
        if (!EM.IsEntityValid(Entity)) continue;

        FMassDspBuildingSlotsFragment* SF = EM.GetFragmentDataPtr<FMassDspBuildingSlotsFragment>(Entity);
        if (!SF) continue;

        TArrayView<FBuildingSlotState> Slots = (SlotType == EBuildingSlotType::Output)
                                                   ? SF->GetOutputSlots()
                                                   : SF->GetInputSlots();

        for (int32 i = 0; i < Slots.Num(); ++i)
        {
            // 已被传送带占用的槽口不允许再次连接
            if (Slots[i].ConnectedLaneHandle.IsValid()) continue;

            const float DSq = FVector::DistSquared(WorldPos, Slots[i].WorldLocation);
            if (DSq < BestDistSq)
            {
                BestDistSq = DSq;
                OutEntity = Entity;
                OutSlotIndex = i;
                OutSlotLocation = Slots[i].WorldLocation;
                OutSlotRotation = Slots[i].WorldRotation;
                OutSlotExtend = Slots[i].SlotExtend;
                bFound = true;
            }
        }
    }

    return bFound;
}

// ============================================================
//  建造系统 —— 建筑预览
// ============================================================

void UMassDspManager::BeginPreviewBuilding(EBuildingType BuildingType, const FTransform& InitialTransform)
{
    CancelAnyPreview();

    TryGetGameMode();
    if (!GameMode.IsValid() || !GameMode->GameConfig) return;

    const FBuildingTypeConfig* Cfg = GameMode->GameConfig->GetBuildingConfig(BuildingType);
    if (!Cfg) return;

    UWorld* World = GetWorld();
    if (!World) return;

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    SpawnParams.ObjectFlags |= RF_Transient;

    if (AActor* Ghost = World->SpawnActor<AActor>(AActor::StaticClass(), InitialTransform, SpawnParams))
    {
        UStaticMeshComponent* SMC = NewObject<UStaticMeshComponent>(Ghost, TEXT("GhostMesh"));
        Ghost->SetRootComponent(SMC);
        if (Cfg->Mesh) SMC->SetStaticMesh(Cfg->Mesh);
        SMC->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        SMC->SetCastShadow(false);
        // 自定义深度用于轮廓高亮（需要 PostProcess 启用 CustomDepth）
        SMC->SetRenderCustomDepth(true);
        SMC->SetCustomDepthStencilValue(2);
        // 尝试将每个材质通道设为半透明（材质中需有 Opacity 参数）
        for (int32 i = 0; i < SMC->GetNumMaterials(); ++i)
        {
            if (UMaterialInterface* Mat = SMC->GetMaterial(i))
            {
                UMaterialInstanceDynamic* DM = UMaterialInstanceDynamic::Create(Mat, SMC);
                DM->SetScalarParameterValue(TEXT("Opacity"), 0.5f);
                SMC->SetMaterial(i, DM);
            }
        }
        SMC->RegisterComponent();
#if WITH_EDITOR
        Ghost->SetActorLabel(TEXT("PreviewBuilding"));
#endif
        PreviewBuildingActor = Ghost;
    }

    PreviewBuildingType = BuildingType;
    CurrentPlaceMode = EBuildPlaceMode::Building;
}

void UMassDspManager::UpdateBuildingPreviewTransform(const FTransform& WorldTransform) const
{
    if (IsValid(PreviewBuildingActor))
        PreviewBuildingActor->SetActorTransform(WorldTransform);
}

FMassEntityHandle UMassDspManager::ConfirmPreviewBuilding()
{
    if (CurrentPlaceMode != EBuildPlaceMode::Building || !IsValid(PreviewBuildingActor))
        return FMassEntityHandle();

    const FTransform FinalTransform = PreviewBuildingActor->GetActorTransform();
    const EBuildingType BuildingType = PreviewBuildingType;

    CancelBuildingPreview();

    TSubclassOf<AMassDspBuilding> BuildingClass = GetBuildingClassForType(BuildingType);
    if (!BuildingClass) return FMassEntityHandle();

    TArray<FBuildingSpawnData> SpawnList;
    SpawnList.Add(FBuildingSpawnData(BuildingClass, FinalTransform, BuildingType));
    TArray<FMassEntityHandle> Results = BatchSpawnBuildings(SpawnList);

    return Results.IsEmpty() ? FMassEntityHandle() : Results[0];
}

void UMassDspManager::CancelBuildingPreview()
{
    if (IsValid(PreviewBuildingActor))
    {
        PreviewBuildingActor->Destroy();
        PreviewBuildingActor = nullptr;
    }
    if (CurrentPlaceMode == EBuildPlaceMode::Building)
        CurrentPlaceMode = EBuildPlaceMode::None;
    PreviewBuildingType = EBuildingType::None;
}

// ============================================================
//  建造系统 —— 传送带预览
// ============================================================

void UMassDspManager::BeginPreviewBelt(EBeltType BeltType)
{
    CancelAnyPreview();

    PreviewBeltType = BeltType;
    bBeltHasStart = false;
    bPreviewBeltDistanceValid = true;
    BeltStartEntity = FMassEntityHandle();
    BeltStartSlotIndex = -1;
    BeltEndEntity = FMassEntityHandle();
    BeltEndSlotIndex = -1;
    CurrentPlaceMode = EBuildPlaceMode::Belt;
}

bool UMassDspManager::SelectBeltSlot(const FVector& WorldPos)
{
    constexpr float SnapRadius = 200.f;

    if (!bBeltHasStart)
    {
        // ── 第 1 次：选择 Output 起点槽 ──────────────────────────────────
        FMassEntityHandle FoundEntity;
        int32 FoundSlotIndex = -1;
        FVector FoundSlotLoc;
        FQuat FoundSlotRot = FQuat::Identity;
        float FoundSlotExt = 100.f;

        if (!FindNearestBuildingSlot(WorldPos, EBuildingSlotType::Output, SnapRadius,
                                     FoundEntity, FoundSlotIndex, FoundSlotLoc, FoundSlotRot, FoundSlotExt))
        {
            UE_LOG(LogTemp, Log, TEXT("SelectBeltSlot: 附近没有可用 Output 槽口（搜索半径 %.0fcm）"), SnapRadius);
            return false;
        }

        BeltStartSlotRotation = FoundSlotRot;
        BeltStartSlotExtend = FoundSlotExt;
        BeltStartEntity = FoundEntity;
        BeltStartSlotIndex = FoundSlotIndex;
        BeltStartSlotLocation = FoundSlotLoc;
        bBeltHasStart = true;

        UE_LOG(LogTemp, Log, TEXT("SelectBeltSlot: 起点已选 @ (%.0f, %.0f, %.0f)，请继续选择终点"),
               FoundSlotLoc.X, FoundSlotLoc.Y, FoundSlotLoc.Z);
        return false; // 仍需选择终点
    }
    else
    {
        // ── 第 2 次：选择 Input 终点槽 ───────────────────────────────────
        FMassEntityHandle FoundEntity;
        int32 FoundSlotIndex = -1;
        FVector FoundSlotLoc;
        FQuat FoundSlotRot = FQuat::Identity;
        float FoundSlotExt = 100.f;

        if (!FindNearestBuildingSlot(WorldPos, EBuildingSlotType::Input, SnapRadius,
                                     FoundEntity, FoundSlotIndex, FoundSlotLoc, FoundSlotRot, FoundSlotExt))
        {
            UE_LOG(LogTemp, Log, TEXT("SelectBeltSlot: 附近没有可用 Input 槽口（搜索半径 %.0fcm）"), SnapRadius);
            return false;
        }

        if (FoundEntity == BeltStartEntity)
        {
            UE_LOG(LogTemp, Warning, TEXT("SelectBeltSlot: 不能连接同一建筑的槽口"));
            return false;
        }

        BeltEndEntity = FoundEntity;
        BeltEndSlotIndex = FoundSlotIndex;

        RebuildPreviewBeltMesh(FoundSlotLoc, FoundSlotRot, FoundSlotExt);

        UE_LOG(LogTemp, Log, TEXT("SelectBeltSlot: 终点已选 @ (%.0f, %.0f, %.0f)，可调用 ConfirmPreviewBelt"),
               FoundSlotLoc.X, FoundSlotLoc.Y, FoundSlotLoc.Z);
        return true; // 两端均已锁定
    }
}

void UMassDspManager::UpdateBeltPreviewEndPoint(const FVector& EndWorldPos, const FQuat& EndSlotRotation, float EndSlotExtend)
{
    if (!bBeltHasStart) return;
    RebuildPreviewBeltMesh(EndWorldPos, EndSlotRotation, EndSlotExtend);
}

FBeltHandle UMassDspManager::ConfirmPreviewBelt()
{
    if (CurrentPlaceMode != EBuildPlaceMode::Belt
        || !bBeltHasStart
        || !BeltEndEntity.IsValid())
    {
        return FBeltHandle();
    }

    const EBeltType BeltType = PreviewBeltType;
    const FMassEntityHandle StartEnt = BeltStartEntity;
    const int32 StartSlotIdx = BeltStartSlotIndex;
    const FMassEntityHandle EndEnt = BeltEndEntity;
    const int32 EndSlotIdx = BeltEndSlotIndex;

    CancelBeltPreview();

    FBeltHandle Handle = CreateAndLinkBeltForSlot(StartEnt, StartSlotIdx, EndEnt, EndSlotIdx, BeltType);
    if (Handle.IsValid())
    {
        FlushBeltMesh();
        UE_LOG(LogTemp, Log, TEXT("ConfirmPreviewBelt: 传送带创建成功 [Handle=%d]"), Handle.Index);
    }
    return Handle;
}

void UMassDspManager::CancelBeltPreview()
{
    ClearPreviewBeltMesh();
    bBeltHasStart = false;
    bPreviewBeltDistanceValid = true;
    BeltStartEntity = FMassEntityHandle();
    BeltStartSlotIndex = -1;
    BeltEndEntity = FMassEntityHandle();
    BeltEndSlotIndex = -1;
    PreviewBeltType = EBeltType::None;
    if (CurrentPlaceMode == EBuildPlaceMode::Belt)
        CurrentPlaceMode = EBuildPlaceMode::None;
}

void UMassDspManager::CancelAnyPreview()
{
    CancelBuildingPreview();
    CancelBeltPreview();
}

// ──── 预览传送带网格重建（通用接口：复用 GenerateConveyorMesh）────

void UMassDspManager::RebuildPreviewBeltMesh(const FVector& EndWorldPos, const FQuat& EndSlotRotation, float EndSlotExtend)
{
    if (!BeltsContainerActor) return;

    // 延迟创建 PreviewSpline
    if (!IsValid(PreviewSpline))
    {
        PreviewSpline = NewObject<USplineComponent>(BeltsContainerActor, TEXT("PreviewSpline"));
        PreviewSpline->SetupAttachment(BeltsContainerActor->GetRootComponent());
        PreviewSpline->SetClosedLoop(false);
        PreviewSpline->RegisterComponent();
    }

    // 延迟创建 PreviewBeltMesh
    if (!IsValid(PreviewBeltMesh))
    {
        PreviewBeltMesh = NewObject<UProceduralMeshComponent>(BeltsContainerActor, TEXT("PreviewBeltMesh"));
        PreviewBeltMesh->SetupAttachment(BeltsContainerActor->GetRootComponent());
        PreviewBeltMesh->SetVisibility(true);
        PreviewBeltMesh->SetCastShadow(false);
        PreviewBeltMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        PreviewBeltMesh->RegisterComponent();
    }

    // 距离校验：超出上限时标记为无效（预览继续显示但变红，禁止确认）
    const float CurrentDist = FVector::Dist(BeltStartSlotLocation, EndWorldPos);
    bPreviewBeltDistanceValid = (CurrentDist >= MinBeltLength && CurrentDist <= MaxBeltLength);
    if (!bPreviewBeltDistanceValid) return;

    // 根据起点/终点槽口数据计算四个控制点（与 CreateAndLinkBeltForSlot 逻辑相同）
    // A = 起点槽口向后退一个 Extend 距离
    // B = 起点槽口位置
    // C = 终点位置（鼠标坐标或已吸附的终点槽口）
    // D = 终点槽口向后退一个 Extend 距离（无终点槽口时沿传送带延伸方向合成）
    const FVector A = BeltStartSlotLocation - BeltStartSlotRotation * FVector(BeltStartSlotExtend, 0.f, 0.f);
    const FVector B = BeltStartSlotLocation;
    const FVector C = EndWorldPos;
    const FVector D = (EndSlotExtend > 0.f)
                          ? EndWorldPos - EndSlotRotation * FVector(EndSlotExtend, 0.f, 0.f)
                          : EndWorldPos - (BeltStartSlotLocation - EndWorldPos).GetSafeNormal() * 100.f;

    // 重用通用接口构建样条
    BuildBeltSplineFromPoints(PreviewSpline, A, B, C, D);

    // 重用 GenerateConveyorMesh 生成预览几何（独立 MeshData，不写入 PendingBeltMeshMap）
    FMergedBeltMeshData PreviewMeshData;
    GenerateConveyorMesh(PreviewMeshData, PreviewSpline, C_Width, C_BeltThickness, C_UVScale, 5.f);

    if (PreviewMeshData.Vertices.IsEmpty()) return;

    PreviewBeltMesh->CreateMeshSection_LinearColor(
        0,
        PreviewMeshData.Vertices,
        PreviewMeshData.Triangles,
        PreviewMeshData.Normals,
        PreviewMeshData.UVs,
        PreviewMeshData.Colors,
        PreviewMeshData.Tangents,
        false
    );

    // 应用材质：距离超限时覆盖为红色警告；正常时使用对应传送带类型材质
    TryGetGameMode();
    if (GameMode.IsValid() && GameMode->GameConfig)
    {
        if (const FBeltTypeConfig* Cfg = GameMode->GameConfig->GetBeltTypeConfig(PreviewBeltType))
        {
            if (Cfg->Material)
            {
                UMaterialInstanceDynamic* DM = UMaterialInstanceDynamic::Create(Cfg->Material, PreviewBeltMesh);
                if (bPreviewBeltDistanceValid)
                {
                    DM->SetVectorParameterValue(TEXT("ArrowColor"), Cfg->Color);
                    DM->SetScalarParameterValue(TEXT("Speed"), Cfg->Speed / C_UVScale);
                }
                else
                {
                    // 超出最大距离 → 红色警告，速度为零（静止）
                    DM->SetVectorParameterValue(TEXT("ArrowColor"), FLinearColor(1.f, 0.1f, 0.1f));
                    DM->SetScalarParameterValue(TEXT("Speed"), 0.f);
                }
                PreviewBeltMesh->SetMaterial(0, DM);
            }
        }
    }
}

void UMassDspManager::GetNearbySlotsForHighlight(
    const FVector& WorldPos,
    float HighlightRadius,
    TArray<FVector>& OutOutputLocs,
    TArray<FVector>& OutInputLocs) const
{
    UMassEntitySubsystem* ESub = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
    if (!ESub) return;

    FMassEntityManager& EM = ESub->GetMutableEntityManager();
    const float RadiusSq = HighlightRadius * HighlightRadius;

    for (const FMassEntityHandle& Entity : SpawnedBuildingEntities)
    {
        if (!EM.IsEntityValid(Entity)) continue;

        FMassDspBuildingSlotsFragment* SF = EM.GetFragmentDataPtr<FMassDspBuildingSlotsFragment>(Entity);
        if (!SF) continue;

        for (const FBuildingSlotState& Slot : SF->GetOutputSlots())
        {
            // 已占用的槽口不参与高亮
            if (Slot.ConnectedLaneHandle.IsValid()) continue;
            if (FVector::DistSquared(WorldPos, Slot.WorldLocation) <= RadiusSq)
                OutOutputLocs.Add(Slot.WorldLocation);
        }
        for (const FBuildingSlotState& Slot : SF->GetInputSlots())
        {
            // 已占用的槽口不参与高亮
            if (Slot.ConnectedLaneHandle.IsValid()) continue;
            if (FVector::DistSquared(WorldPos, Slot.WorldLocation) <= RadiusSq)
                OutInputLocs.Add(Slot.WorldLocation);
        }
    }
}

void UMassDspManager::ClearPreviewBeltMesh() const
{
    if (IsValid(PreviewBeltMesh))
        PreviewBeltMesh->ClearAllMeshSections();
    if (IsValid(PreviewSpline))
        PreviewSpline->ClearSplinePoints(true);
}
