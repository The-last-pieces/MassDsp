#include "Subsystems/MassDspManager.h"

#include "GameConst.h"
#include "MassCommonFragments.h"
#include "MassDspGameMode.h"

#include "Actors/MassDspBuilding.h"
#include "Actors/MassDspAssembler.h"

#include "Fragments/BeltItemFragment.h"
#include "Fragments/MassDspBuildingSlotsFragment.h"
#include "Fragments/MassDspAssemblerFragment.h"
#include "Fragments/MassDspMinerFragment.h"
#include "Fragments/MassDspLogisticsTowerFragment.h"
#include "Fragments/MassDspStorageFragment.h"

#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "MassEntityConfigAsset.h"
#include "MassRepresentationFragments.h"
#include "MassLODFragments.h"

#include "ProceduralMeshComponent.h"
#include "Components/SplineComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Inventory/MassDspPlayerInventoryComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
    UMassDspPlayerInventoryComponent* GetPlayerInventoryComponent(UWorld* World)
    {
        if (!World) return nullptr;
        APlayerController* PC = World->GetFirstPlayerController();
        if (APawn* Pawn = PC ? PC->GetPawn() : nullptr)
        {
            UMassDspPlayerInventoryComponent* InvComp = Pawn->FindComponentByClass<UMassDspPlayerInventoryComponent>();
            if (!InvComp)
            {
                InvComp = NewObject<UMassDspPlayerInventoryComponent>(Pawn);
                InvComp->RegisterComponent();
            }
            return InvComp;
        }
        return nullptr;
    }

    int32 StoreIntoAssembler(FMassDspAssemblerFragment& Assembler, const FRecipeDataForFragment& Recipe, EItemType ItemType, int32 Quantity)
    {
        int32 Stored = 0;
        while (Stored < Quantity && Assembler.TryConsumeItemFromSlot(ItemType, Recipe))
        {
            ++Stored;
        }
        return Stored;
    }

    int32 TakeFromAssembler(FMassDspAssemblerFragment& Assembler, const FRecipeDataForFragment& Recipe, EItemType ItemType, int32 Quantity)
    {
        if (Quantity <= 0) return 0;

        int32 Taken = 0;
        for (int32 SlotIndex = 0; SlotIndex < Recipe.OutputsCount && Taken < Quantity; ++SlotIndex)
        {
            FBufferEntry& Entry = Assembler.OutputBuffers[SlotIndex];
            if (Entry.ItemType != ItemType || Entry.Amount <= 0) continue;

            const int32 ToTake = FMath::Min(Quantity - Taken, Entry.Amount);
            Entry.Amount -= ToTake;
            Taken += ToTake;
            if (Entry.Amount == 0)
            {
                Entry.ItemType = EItemType::None;
            }
        }

        Assembler.UpdateSatisfied(Recipe);
        return Taken;
    }
}

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

            // 全局共享样条单例：LUT 烘焙和 Chunk 几何生成时复用，仅此一个 USplineComponent 对象，零 GC 压力
            SharedSplineHelper = NewObject<USplineComponent>(BeltsContainerActor, TEXT("SharedSplineHelper"));
            SharedSplineHelper->SetupAttachment(Root);
            SharedSplineHelper->SetVisibility(false);
            SharedSplineHelper->SetHiddenInGame(true);
            SharedSplineHelper->SetClosedLoop(false);
            SharedSplineHelper->RegisterComponent();
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

bool UMassDspManager::SetMinerItemType(FMassEntityHandle Entity, EItemType NewItemType)
{
    if (!Entity.IsValid() || NewItemType == EItemType::None) return false;

    UMassEntitySubsystem* ESub = GetWorld() ? GetWorld()->GetSubsystem<UMassEntitySubsystem>() : nullptr;
    if (!ESub) return false;

    FMassEntityManager& EM = ESub->GetMutableEntityManager();
    if (!EM.IsEntityValid(Entity)) return false;

    FMassDspMinerFragment* Miner = EM.GetFragmentDataPtr<FMassDspMinerFragment>(Entity);
    if (!Miner) return false;

    Miner->StoredItemType = NewItemType;
    Miner->InventoryCount = 0;
    Miner->NextProductionWorldTime = 0.f;
    return true;
}

bool UMassDspManager::SetAssemblerRecipe(FMassEntityHandle Entity, ERecipeType NewRecipeType)
{
    if (!Entity.IsValid() || NewRecipeType == ERecipeType::None) return false;

    TryGetGameMode();
    if (!GameMode.IsValid() || !GameMode->GameConfig || !GameMode->GameConfig->GetRecipeConfig(NewRecipeType))
        return false;

    UMassEntitySubsystem* ESub = GetWorld() ? GetWorld()->GetSubsystem<UMassEntitySubsystem>() : nullptr;
    if (!ESub) return false;

    FMassEntityManager& EM = ESub->GetMutableEntityManager();
    if (!EM.IsEntityValid(Entity)) return false;

    FMassDspAssemblerFragment* Assembler = EM.GetFragmentDataPtr<FMassDspAssemblerFragment>(Entity);
    if (!Assembler) return false;

    Assembler->ActiveRecipeType = NewRecipeType;
    Assembler->ResetForRecipeChange();
    return true;
}

bool UMassDspManager::SetLogisticsTowerMode(FMassEntityHandle Entity, ELogisticsTowerMode NewMode)
{
    if (!Entity.IsValid()) return false;

    UMassEntitySubsystem* ESub = GetWorld() ? GetWorld()->GetSubsystem<UMassEntitySubsystem>() : nullptr;
    if (!ESub) return false;

    FMassEntityManager& EM = ESub->GetMutableEntityManager();
    if (!EM.IsEntityValid(Entity)) return false;

    FMassDspLogisticsTowerFragment* Tower = EM.GetFragmentDataPtr<FMassDspLogisticsTowerFragment>(Entity);
    if (!Tower) return false;

    Tower->TowerMode = NewMode;
    Tower->bDirty = true;
    return true;
}

int32 UMassDspManager::TryStoreItemsFromPlayer(FMassEntityHandle Entity, EItemType ItemType, int32 Quantity)
{
    if (!Entity.IsValid() || ItemType == EItemType::None || Quantity <= 0) return 0;

    UMassDspPlayerInventoryComponent* Inventory = GetPlayerInventoryComponent(GetWorld());
    if (!Inventory) return 0;

    UMassEntitySubsystem* ESub = GetWorld() ? GetWorld()->GetSubsystem<UMassEntitySubsystem>() : nullptr;
    if (!ESub) return 0;

    FMassEntityManager& EM = ESub->GetMutableEntityManager();
    if (!EM.IsEntityValid(Entity)) return 0;

    const int32 Available = Inventory->GetItemCount(ItemType);
    const int32 Requested = FMath::Min(Quantity, Available);
    if (Requested <= 0) return 0;

    int32 Stored = 0;
    if (FMassDspStorageFragment* Storage = EM.GetFragmentDataPtr<FMassDspStorageFragment>(Entity))
    {
        Stored = Storage->TryConsumeItems(ItemType, Requested);
    }
    else if (FMassDspAssemblerFragment* Assembler = EM.GetFragmentDataPtr<FMassDspAssemblerFragment>(Entity))
    {
        TryGetGameMode();
        const UGameConfigData* GameConfig = GameMode.IsValid() ? GameMode->GameConfig.Get() : nullptr;
        const FRecipeConfigData* RecipeConfig = GameConfig ? GameConfig->GetRecipeConfig(Assembler->ActiveRecipeType) : nullptr;
        if (RecipeConfig)
        {
            const FRecipeDataForFragment Recipe = RecipeConfig->ToFragment(Assembler->ActiveRecipeType);
            Stored = StoreIntoAssembler(*Assembler, Recipe, ItemType, Requested);
        }
    }

    if (Stored > 0)
    {
        Inventory->RemoveItem(ItemType, Stored);
    }

    return Stored;
}

int32 UMassDspManager::TryTakeItemsForPlayer(FMassEntityHandle Entity, EItemType ItemType, int32 Quantity)
{
    if (!Entity.IsValid() || ItemType == EItemType::None || Quantity <= 0) return 0;

    UMassDspPlayerInventoryComponent* Inventory = GetPlayerInventoryComponent(GetWorld());
    if (!Inventory) return 0;

    const int32 Requested = FMath::Min(Quantity, Inventory->GetFreeCapacity());
    if (Requested <= 0) return 0;

    UMassEntitySubsystem* ESub = GetWorld() ? GetWorld()->GetSubsystem<UMassEntitySubsystem>() : nullptr;
    if (!ESub) return 0;

    FMassEntityManager& EM = ESub->GetMutableEntityManager();
    if (!EM.IsEntityValid(Entity)) return 0;

    int32 Taken = 0;
    if (FMassDspStorageFragment* Storage = EM.GetFragmentDataPtr<FMassDspStorageFragment>(Entity))
    {
        if (Storage->StoredItemType == ItemType)
        {
            Taken = Storage->TryProvideItems(Requested);
        }
    }
    else if (FMassDspMinerFragment* Miner = EM.GetFragmentDataPtr<FMassDspMinerFragment>(Entity))
    {
        if (Miner->StoredItemType == ItemType)
        {
            Taken = FMath::Min(Requested, Miner->InventoryCount);
            Miner->InventoryCount -= Taken;
        }
    }
    else if (FMassDspAssemblerFragment* Assembler = EM.GetFragmentDataPtr<FMassDspAssemblerFragment>(Entity))
    {
        TryGetGameMode();
        const UGameConfigData* GameConfig = GameMode.IsValid() ? GameMode->GameConfig.Get() : nullptr;
        if (const FRecipeConfigData* RecipeConfig = GameConfig ? GameConfig->GetRecipeConfig(Assembler->ActiveRecipeType) : nullptr)
        {
            const FRecipeDataForFragment Recipe = RecipeConfig->ToFragment(Assembler->ActiveRecipeType);
            Taken = TakeFromAssembler(*Assembler, Recipe, ItemType, Requested);
        }
    }

    if (Taken > 0)
    {
        Inventory->AddItem(ItemType, Taken);
    }

    return Taken;
}

TWeakObjectPtr<AMassDspGameMode> UMassDspManager::TryGetGameMode()
{
    if (!GameMode.IsValid())
    {
        GameMode = Cast<AMassDspGameMode>(GetWorld()->GetAuthGameMode());
    }

    return GameMode;
}

FBeltHandle UMassDspManager::CreateRuntimeBelt(const FBeltRebuildData& RebuildData, EBeltType BeltType, const FVector& CameraPos)
{
    if (!BeltsContainerActor) return FBeltHandle();

    // --- 精确长度计算（Dubins 解析，无需 Spline；Hermite 仍需 Spline 积分）---
    check(SharedSplineHelper != nullptr);
    float ExactLength = 0.f;
    if (RebuildData.RebuildType == EBeltRebuildType::Dubins)
    {
        // Dubins: 长度纯解析计算，零 USplineComponent API 调用
        const FDubinsPathData& D = RebuildData.DubinsData;
        ExactLength = D.TotalLength;
        if (D.bHasStartExtend)
            ExactLength += FVector::Dist(D.StartExtendPos, FVector(D.StartPos.X, D.StartPos.Y, D.StartZ));
        if (D.bHasEndExtend)
            ExactLength += FVector::Dist(FVector(D.EndPos.X, D.EndPos.Y, D.EndZ), D.EndExtendPos);
    }
    else
    {
        // Hermite: 需要样条曲线数值积分
        SharedSplineHelper->ClearSplinePoints(false);
        BuildBeltSplineFromPoints(SharedSplineHelper, RebuildData.HermiteData.A, RebuildData.HermiteData.B,
                                  RebuildData.HermiteData.C, RebuildData.HermiteData.D);
        ExactLength = SharedSplineHelper->GetSplineLength();
    }

    // --- 从 BeltType 配置读取速度 ---
    float BeltSpeed = FGameConst::ItemSpace * 2;
    TryGetGameMode();
    if (GameMode.IsValid() && GameMode->GameConfig)
    {
        if (const FBeltTypeConfig* Config = GameMode->GameConfig->GetBeltTypeConfig(BeltType))
            BeltSpeed = Config->Speed;
    }

    // --- 创建轨迹 + ComputeBoundsOnly ---
    FBeltTrajectory NewTrajectory;
    NewTrajectory.TotalLength = ExactLength;
    NewTrajectory.Speed = BeltSpeed;

    int32 Index = BeltTrajectories.Add(NewTrajectory);
    FBeltTrajectory& Traj = BeltTrajectories[Index];

    if (RebuildData.RebuildType == EBeltRebuildType::Dubins)
        Traj.ComputeBoundsOnly(RebuildData.DubinsData, 500.f);
    else
        Traj.ComputeBoundsOnly(SharedSplineHelper, 500.f);

    // --- 同步存储重建数据（与 BeltTrajectories 下标一一对应）---
    FBeltRebuildData RD = RebuildData;
    RD.BeltType = BeltType;
    if (!BeltRebuildData.IsValidIndex(Index))
        BeltRebuildData.Insert(Index, MoveTemp(RD));
    else
        BeltRebuildData[Index] = MoveTemp(RD);

    // --- Handle 和 BeltData (TMap 插入) ---
    FBeltHandle NewHandle;
    NewHandle.Index = Index;
    NewHandle.Generation = 0;

    FBeltData& BeltData = BeltEntityRegistry.Add(NewHandle);
    BeltData.BeltLength = Traj.TotalLength;
    BeltData.BeltSpeed = Traj.Speed;

    // --- 注册到空间 Chunk（网格待 FlushBeltMesh 时生成）---
    if (BeltType != EBeltType::None)
    {
        const FIntPoint ChunkKey = GetChunkKey(Traj.RepresentativePosition);
        FBeltChunk& Chunk = BeltChunks.FindOrAdd(ChunkKey);
        Chunk.BeltTrajectoryIndices.Add(Index);
        Chunk.bMeshDirty = true;
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
    float AngleThreshold,
    float MaxSegmentLength)
{
    if (!Spline || Spline->GetNumberOfSplinePoints() < 2) return;

    // --- 1. 计算自适应切片 (Adaptive Slicing) ---
    TArray<FConveyorSlice> Slices;
    const float SplineLength = Spline->GetSplineLength();
    constexpr float CheckStep = 10.0f; // 采样精度 10cm

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
    FMassEntityHandle SBuilding, int32 StartSlotIndex,
    FMassEntityHandle EBuilding, int32 EndSlotIndex,
    EBeltType BeltType, EBeltSplineType SplineType)
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

    FBeltHandle BeltHandle;
    if (SplineType == EBeltSplineType::DubinsPath)
    {
        const FVector SF = StartSlot->WorldRotation.RotateVector(FVector(1.f, 0.f, 0.f));
        const FVector EF = EndSlot->WorldRotation.RotateVector(FVector(1.f, 0.f, 0.f));
        FDubinsPathData DP = ComputeDubinsPath(
            FVector2D(B.X, B.Y), FMath::Atan2(SF.Y, SF.X),
            FVector2D(C.X, C.Y), FMath::Atan2(-EF.Y, -EF.X),
            DubinsMinTurningRadius);
        DP.StartZ = B.Z;
        DP.EndZ = C.Z;
        DP.bHasStartExtend = true;
        DP.StartExtendPos = A;
        DP.bHasEndExtend = true;
        DP.EndExtendPos = D;
        if (DP.IsValid())
        {
            FBeltRebuildData RD;
            RD.RebuildType = EBeltRebuildType::Dubins;
            RD.DubinsData = DP;
            BeltHandle = CreateRuntimeBelt(RD, BeltType);
        }
    }
    if (!BeltHandle.IsValid()) // Hermite 样条回退（默认实现或 Dubins 失效）
    {
        FBeltRebuildData RD;
        RD.RebuildType = EBeltRebuildType::Hermite;
        RD.HermiteData.A = A;
        RD.HermiteData.B = B;
        RD.HermiteData.C = C;
        RD.HermiteData.D = D;
        BeltHandle = CreateRuntimeBelt(RD, BeltType);
    }

    if (!BeltHandle.IsValid()) return FBeltHandle();

    const FBeltTrajectory& Trajectory = BeltTrajectories[BeltHandle.Index];

    StartSlot->ConnectedLaneHandle = EndSlot->ConnectedLaneHandle = BeltHandle;
    StartSlot->BeltSpeed = EndSlot->BeltSpeed = Trajectory.Speed;

    // 更新 ConnectedCount 以便 BuildingProcessor 可以在 ProcessSlots 顶部早退
    if (FMassDspBuildingSlotsFragment* SSlots = EntityManager.GetFragmentDataPtr<FMassDspBuildingSlotsFragment>(SBuilding))
        SSlots->MarkOutputConnected();
    if (FMassDspBuildingSlotsFragment* ESlots = EntityManager.GetFragmentDataPtr<FMassDspBuildingSlotsFragment>(EBuilding))
        ESlots->MarkInputConnected();

    return BeltHandle;
}

void UMassDspManager::RebuildBeltSoA()
{
    const int32 N = BeltEntityRegistry.Num();
    Belt_TotalMove.SetNumUninitialized(N);
    Belt_Speed.SetNumUninitialized(N);
    Belt_Ptrs.SetNumUninitialized(N);
    Belt_RepPos.SetNumUninitialized(N);
    Belt_BoundRadius.SetNumUninitialized(N);
    Belt_TrajIndex.SetNumUninitialized(N);

    int32 i = 0;
    for (auto& [Handle, BeltData] : BeltEntityRegistry)
    {
        BeltData.TickIdx = i;
        Belt_TotalMove[i] = BeltData.TotalMove;
        Belt_Speed[i] = BeltData.BeltSpeed;
        Belt_Ptrs[i] = &BeltData;
        Belt_TrajIndex[i] = Handle.Index;

        if (BeltTrajectories.IsValidIndex(Handle.Index))
        {
            const FBeltTrajectory& Traj = BeltTrajectories[Handle.Index];
            Belt_RepPos[i] = Traj.RepresentativePosition;
            Belt_BoundRadius[i] = Traj.BoundRadius;
        }
        else
        {
            Belt_RepPos[i] = FVector::ZeroVector;
            Belt_BoundRadius[i] = 0.f;
        }
        ++i;
    }

    // 重建空间哈希网格（格子边长 SpatialGridCellSize）
    SpatialGrid.Reset();
    const float InvCell = 1.f / SpatialGridCellSize;
    for (int32 j = 0; j < N; ++j)
    {
        const FVector& Pos = Belt_RepPos[j];
        const FIntPoint Cell(
            FMath::FloorToInt(Pos.X * InvCell),
            FMath::FloorToInt(Pos.Y * InvCell));
        SpatialGrid.FindOrAdd(Cell).Add(j);
    }

    Belt_CachedCount = N;
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
    // 确保 SoA 与 BeltEntityRegistry 同步（ProcessConveyor 通常已提前同步）
    if (BeltEntityRegistry.Num() != Belt_CachedCount)
        RebuildBeltSoA();

    if (Belt_CachedCount <= 0) return;

    const float MaxDistSq = MaxRenderDistance * MaxRenderDistance;
    const float InvCell = 1.f / SpatialGridCellSize;

    // ── Step 0A: 空间粗筛——枚举相机 AABB 内的格子，收集候选 SoA 下标 ─────────
    // 从 ~N_all 次 TMap::Find 降至 ~(2R/cell)² 次格子查询（典型约 20×20=400 次）
    TArray<int32> Candidates;
    Candidates.Reserve(512);

    const int32 CellMinX = FMath::FloorToInt((CameraPos.X - MaxRenderDistance) * InvCell);
    const int32 CellMaxX = FMath::FloorToInt((CameraPos.X + MaxRenderDistance) * InvCell);
    const int32 CellMinY = FMath::FloorToInt((CameraPos.Y - MaxRenderDistance) * InvCell);
    const int32 CellMaxY = FMath::FloorToInt((CameraPos.Y + MaxRenderDistance) * InvCell);

    for (int32 CX = CellMinX; CX <= CellMaxX; ++CX)
        for (int32 CY = CellMinY; CY <= CellMaxY; ++CY)
            if (const TArray<int32>* List = SpatialGrid.Find(FIntPoint(CX, CY)))
                Candidates.Append(*List);

    if (Candidates.IsEmpty())
    {
        for (auto& [Type, ISM] : ItemISMPool)
            if (ISM && ISM->GetInstanceCount() > 0)
                ISM->ClearInstances();
        return;
    }

    // ── Step 0B: 精确双重门控（距离 + 视锥），纯平坦数组，无 TMap 访问 ─────────
    TArray<int32> VisibleIndices;
    VisibleIndices.Reserve(Candidates.Num());

    const FVector* RESTRICT RepPosData = Belt_RepPos.GetData();
    const float* RESTRICT BoundRadData = Belt_BoundRadius.GetData();

    for (const int32 Idx : Candidates)
    {
        if (FVector::DistSquared(CameraPos, RepPosData[Idx]) > MaxDistSq) continue;
        if (!ViewFrustum.IntersectSphere(RepPosData[Idx], BoundRadData[Idx])) continue;
        VisibleIndices.Add(Idx);
    }

    const int32 NumVisible = VisibleIndices.Num();
    if (NumVisible == 0)
    {
        for (auto& [Type, ISM] : ItemISMPool)
            if (ISM && ISM->GetInstanceCount() > 0)
                ISM->ClearInstances();
        return;
    }

    // ── Step 1: 前缀和（仅可见 Belt）─────────────────────────────────────────
    TArray<int32> ItemOffsets;
    ItemOffsets.SetNumUninitialized(NumVisible);
    int32 TotalVisibleItems = 0;
    for (int32 vi = 0; vi < NumVisible; ++vi)
    {
        const FBeltData* BD = Belt_Ptrs[VisibleIndices[vi]];
        ItemOffsets[vi] = TotalVisibleItems;
        if (BD) TotalVisibleItems += BD->ItemCache.Num();
    }

    if (TotalVisibleItems == 0)
    {
        for (auto& [Type, ISM] : ItemISMPool)
            if (ISM && ISM->GetInstanceCount() > 0)
                ISM->ClearInstances();
        return;
    }

    // ── Step 2: 预分配平坦输出数组（仅可见物品）──────────────────────────────
    // 注意：使用默认初始化而非 SetNumUninitialized。
    // LUT 未加载的 Belt（视距外）在 ParallelFor 中会 return 跳过，留下未写入槽位。
    // Type 默认 None → Step 4 跳过；T 默认 FTransform::Identity → 旋转归一化，
    // 避免向 BatchUpdateInstancesTransforms 传入未初始化内存导致断言崩溃。
    struct FItemEntry
    {
        EItemType Type = EItemType::None;
        FTransform T = FTransform::Identity;
    };
    TArray<FItemEntry> FlatEntries;
    FlatEntries.SetNum(TotalVisibleItems);

    // ── Step 3: ParallelFor 并行查 LUT（仅可见 Belt，无 TMap 访问）────────────
    const int32* RESTRICT TrajIdxData = Belt_TrajIndex.GetData();
    ParallelFor(NumVisible, [&](int32 vi)
    {
        const int32 SoaIdx = VisibleIndices[vi];
        const FBeltData* BD = Belt_Ptrs[SoaIdx];
        if (!BD || BD->ItemCache.IsEmpty()) return;

        const int32 TrajIdx = TrajIdxData[SoaIdx];
        if (!BeltTrajectories.IsValidIndex(TrajIdx)) return;

        const FBeltTrajectory& Traj = BeltTrajectories[TrajIdx];
        if (!Traj.IsValid()) return;

        int32 WriteIdx = ItemOffsets[vi];
        const int32 ItemCount = BD->ItemCache.Num();
        for (int32 ItemIdx = 0; ItemIdx < ItemCount; ++ItemIdx)
        {
            const FBeltItemCache& Cache = BD->ItemCache[ItemIdx];
            FItemEntry& Entry = FlatEntries[WriteIdx++];
            Entry.Type = Cache.ItemType;
            if (Cache.ItemType != EItemType::None)
            {
                const float EffDist = BD->GetEffectivePosition(ItemIdx);
                Traj.GetTransformAtDistance(EffDist, Entry.T);
            }
        }
    });

    // ── Step 4: Bucket Sort ───────────────────────────────────────────────────
    for (auto& [Type, Arr] : CachedTransformsByType) Arr.Reset();
    for (const FItemEntry& Entry : FlatEntries)
        if (Entry.Type != EItemType::None)
            CachedTransformsByType.FindOrAdd(Entry.Type).Add(Entry.T);

    // ── Step 5: 每种物品类型 1 次 BatchUpdate ─────────────────────────────────
    for (auto& [Type, Transforms] : CachedTransformsByType)
    {
        UInstancedStaticMeshComponent* ISM = GetOrCreateIsmForItemType(Type);
        if (!ISM) continue;

        const int32 NewCount = Transforms.Num();
        const int32 OldCount = ISM->GetInstanceCount();
        if (NewCount > OldCount)
        {
            const FTransform HiddenTransform(FVector(0.f, 0.f, -99999.f));
            for (int32 i = OldCount; i < NewCount; ++i)
                ISM->AddInstance(HiddenTransform, false);
        }
        else if (NewCount < OldCount)
        {
            TArray<int32> ToRemove;
            ToRemove.Reserve(OldCount - NewCount);
            for (int32 i = NewCount; i < OldCount; ++i) ToRemove.Add(i);
            ISM->RemoveInstances(ToRemove);
        }
        if (NewCount > 0)
            ISM->BatchUpdateInstancesTransforms(0, Transforms, false, true, true);
    }

    for (auto& [Type, ISM] : ItemISMPool)
        if (ISM && !CachedTransformsByType.Contains(Type) && ISM->GetInstanceCount() > 0)
            ISM->ClearInstances();

    // 每帧累计到阈値后驱动一次 LOD / Chunk 可视性更新 + 分帧网格刷新
    // 注：LodAccum 不部分被外部 DeltaTime 驱动，简化为每次调用加一个帧间隔
    //   GameMode::SyncAccum 已保证这里 ~60fps 调用，所以 LodAccum 每帧 += 1/60
    constexpr float LodInterval = 1.0f; // 每秒更新一次 LOD
    LodAccum += 1.f / 60.f;
    if (LodAccum >= LodInterval)
    {
        LodAccum = 0.f;
        UpdateBeltLODs(CameraPos);
        UpdateBeltChunkVisibility(CameraPos);
    }
    TickBeltMeshFlush(CameraPos);
}

// ===== 新增：Building Entity批量创建系统 =====

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
    if (!GameMode.IsValid() || !GameMode->GameConfig || !GameMode->GameConfig->BeltItemConfigAsset)
    {
        UE_LOG(LogTemp, Error, TEXT("BatchSpawnBuildings: GameMode, GameConfig or BeltItemConfigAsset is null"));
        return CreatedEntities;
    }

    FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
    const FMassEntityTemplate& EntityTemplate = GameMode->GameConfig->BeltItemConfigAsset->GetConfig().GetOrCreateEntityTemplate(*GetWorld());

    // 按 BuildingType 分组，相同类型共用同一 Archetype，一次 BatchCreateEntities
    TMap<EBuildingType, TArray<int32>> TypeToIndices;
    for (int32 i = 0; i < SpawnDataList.Num(); ++i)
    {
        TypeToIndices.FindOrAdd(SpawnDataList[i].BuildingType).Add(i);
    }

    for (auto& [BuildingType, Indices] : TypeToIndices)
    {
        // 从 GameConfig 动态查询该建筑类型对应的 Class
        const FBuildingTypeConfig* BuildingTypeCfg = GameMode->GameConfig->GetBuildingConfig(BuildingType);
        if (!BuildingTypeCfg || !BuildingTypeCfg->BuildingClass) continue;
        TSubclassOf<AMassDspBuilding> ResolvedClass = BuildingTypeCfg->BuildingClass;

        const AMassDspBuilding* CDO = GetDefault<AMassDspBuilding>(ResolvedClass);
        if (!CDO) continue;

        auto Shared = EntityTemplate.GetSharedFragmentValues();
        if (auto AssemblerCDO = Cast<AMassDspAssembler>(CDO))
        {
            FMassDspRecipeSharedFragment Fragment;
            Fragment.Recipe = GameMode->GameConfig->GetRecipeConfig(AssemblerCDO->RecipeType)->ToFragment(AssemblerCDO->RecipeType);
            Shared.Add(FSharedStruct::Make<FMassDspRecipeSharedFragment>(Fragment));
            Shared.Sort();
        }

        // 获取或创建 Archetype（有缓存则直接取，避免重复 CreateArchetype）
        FMassArchetypeHandle Archetype;
        if (FMassArchetypeHandle* Cached = CachedBuildingArchetypes.Find(BuildingType))
        {
            Archetype = *Cached;
        }
        else
        {
            FMassArchetypeCompositionDescriptor Composition = EntityTemplate.GetCompositionDescriptor();
            for (const UScriptStruct* FragmentType : CDO->GetStaticStructs())
            {
                Composition.GetContainer<FMassFragment>().Add(*FragmentType);
            }
            if (BuildingType == EBuildingType::Assembler)
            {
                Composition.GetContainer<FMassSharedFragment>().Add(*FMassDspRecipeSharedFragment::StaticStruct());
            }
            Archetype = EntityManager.CreateArchetype(Composition);
            CachedBuildingArchetypes.Add(BuildingType, Archetype);
        }

        if (!Archetype.IsValid()) continue;

        // 一次性批量分配本类型全部实体（核心优化：避免逐个 CreateEntity 的内存碎片和锁开销）
        TArray<FMassEntityHandle> BatchHandles;
        BatchHandles.Reserve(Indices.Num());

        EntityManager.BatchCreateEntities(Archetype, Shared, Indices.Num(), BatchHandles);

        // 逐实体初始化 Fragment 数据（创建后必须初始化，无法批量跳过）
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
            BuildingEntityTypeRegistry.Add(EntityHandle, BuildingType);
            RegisterBuildingInGrid(EntityHandle, SpawnData.WorldTransform.GetLocation());
        }
    }

    UE_LOG(LogTemp, Log, TEXT("BatchSpawnBuildings: Created %d / %d building entities"), SuccessCount, SpawnDataList.Num());

    return CreatedEntities;
}

void UMassDspManager::FlushChunk(FIntPoint ChunkKey, FBeltChunk& Chunk, const FVector& CameraPos)
{
    if (Chunk.BeltTrajectoryIndices.IsEmpty()) return;

    // 计算 Chunk 中心（世界 XY，Z 使用相机 Z 以计算 3D 距离）
    const FVector ChunkCenter(
        (static_cast<float>(ChunkKey.X) + 0.5f) * SpatialGridCellSize,
        (static_cast<float>(ChunkKey.Y) + 0.5f) * SpatialGridCellSize,
        CameraPos.Z);

    const bool bUseCameraLOD = !CameraPos.IsNearlyZero();
    const float ChunkDist = bUseCameraLOD ? FVector::Dist(CameraPos, ChunkCenter) : 0.f;

    // 视距之外：清空网格，归还 PMC 到对象池
    if (bUseCameraLOD && ChunkDist > LUTUnloadDistance + LUTLoadHysteresis)
    {
        if (Chunk.PMC)
        {
            Chunk.PMC->ClearAllMeshSections();
            if (FreePMCPool.Num() < MaxFreePMCPoolSize)
                FreePMCPool.Add(Chunk.PMC);
            else
                Chunk.PMC->DestroyComponent();
            Chunk.PMC = nullptr;
            Chunk.CurrentMeshLOD = -1;
        }
        Chunk.bMeshDirty = false;
        return;
    }

    const int32 TargetLOD = bUseCameraLOD ? ComputeBeltLODLevel(ChunkDist) : 1;

    // LOD 无变化且网格未标脏 → 不重建
    if (!Chunk.bMeshDirty && Chunk.CurrentMeshLOD == TargetLOD) return;

    // --- 从对象池取用 PMC（池空时才新建，避免频繁 RegisterComponent 开销）---
    if (!Chunk.PMC)
    {
        if (FreePMCPool.Num() > 0)
        {
            Chunk.PMC = FreePMCPool.Pop(EAllowShrinking::No);
        }
        else
        {
            const FString PMCName = FString::Printf(TEXT("BeltChunkPMC_%d_%d"), ChunkKey.X, ChunkKey.Y);
            Chunk.PMC = NewObject<UProceduralMeshComponent>(BeltsContainerActor, *PMCName);
            Chunk.PMC->SetupAttachment(BeltsContainerActor->GetRootComponent());
            Chunk.PMC->SetVisibility(true);
            Chunk.PMC->SetCastShadow(false);
            Chunk.PMC->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Chunk.PMC->SetCullDistance(MaxRenderDistance * 2.f);
            Chunk.PMC->RegisterComponent();
        }
    }

    // --- 按 LOD 对应参数重建本 Chunk 内所有传送带的网格 ---
    const float AngleThresh = BeltLODAngleThresholds[TargetLOD];
    const float MaxSegLen = BeltLODMaxSegLengths[TargetLOD];

    TMap<EBeltType, FMergedBeltMeshData> ChunkMeshData;

    for (const int32 TrajIdx : Chunk.BeltTrajectoryIndices)
    {
        if (!BeltTrajectories.IsValidIndex(TrajIdx)) continue;
        if (!BeltRebuildData.IsValidIndex(TrajIdx)) continue;

        const FBeltRebuildData& RD = BeltRebuildData[TrajIdx];
        if (RD.BeltType == EBeltType::None) continue;

        // 复用 SharedSplineHelper（全局单例，零 GC 压力；Build 函数末尾已调 UpdateSpline）
        SharedSplineHelper->ClearSplinePoints(false);

        if (RD.RebuildType == EBeltRebuildType::Dubins)
            BuildBeltSplineFromDubins(SharedSplineHelper, RD.DubinsData);
        else
            BuildBeltSplineFromPoints(SharedSplineHelper, RD.HermiteData.A, RD.HermiteData.B,
                                      RD.HermiteData.C, RD.HermiteData.D);

        GenerateConveyorMesh(ChunkMeshData.FindOrAdd(RD.BeltType), SharedSplineHelper,
                             C_Width, C_BeltThickness, C_UVScale, AngleThresh, MaxSegLen);
    }

    // --- 提交到 PMC（每 BeltType 一个 Section）---
    TryGetGameMode();
    Chunk.PMC->ClearAllMeshSections();
    for (auto& [BeltType, MeshData] : ChunkMeshData)
    {
        if (MeshData.Vertices.IsEmpty()) continue;

        const int32 SectionIdx = static_cast<int32>(BeltType);
        Chunk.PMC->CreateMeshSection_LinearColor(
            SectionIdx,
            MeshData.Vertices,
            MeshData.Triangles,
            MeshData.Normals,
            MeshData.UVs,
            MeshData.Colors,
            MeshData.Tangents,
            false);

        if (GameMode.IsValid() && GameMode->GameConfig)
        {
            if (const FBeltTypeConfig* Config = GameMode->GameConfig->GetBeltTypeConfig(BeltType))
            {
                if (Config->Material)
                {
                    // 按 BeltType 缓存 MID：同种传送带所有 Chunk 共享同一实例，避免每次 FlushChunk 泄漏新 MID
                    UMaterialInstanceDynamic* DynMat = nullptr;
                    if (UMaterialInstanceDynamic** Cached = BeltMIDCache.Find(BeltType))
                    {
                        DynMat = *Cached;
                    }
                    else
                    {
                        DynMat = UMaterialInstanceDynamic::Create(Config->Material, this);
                        DynMat->SetVectorParameterValue(TEXT("ArrowColor"), Config->Color);
                        DynMat->SetScalarParameterValue(TEXT("Speed"), Config->Speed / C_UVScale);
                        BeltMIDCache.Add(BeltType, DynMat);
                    }
                    Chunk.PMC->SetMaterial(SectionIdx, DynMat);
                }
            }
        }
    }

    Chunk.CurrentMeshLOD = TargetLOD;
    Chunk.bMeshDirty = false;
}

void UMassDspManager::FlushBeltMesh(const FVector& CameraPos)
{
    for (auto& [ChunkKey, Chunk] : BeltChunks)
    {
        if (Chunk.bMeshDirty)
            FlushChunk(ChunkKey, Chunk, CameraPos);
    }
}

void UMassDspManager::TickBeltMeshFlush(const FVector& CameraPos)
{
    // 初始加载时队列可能很大：允许首帧多处理一些近处 Chunk，
    // 用当前队首距离决定本帧上限：距离 <5000cm → 最多 8 个，<10000cm → 4 个，否则 ChunksPerFrame
    int32 BudgetThisFrame = ChunksPerFrame;
    if (PendingFlushQueue.Num() > 0)
    {
        if (const FBeltChunk* First = BeltChunks.Find(PendingFlushQueue[0]))
        {
            // 队首 Chunk 中点距离（近似）
            const FIntPoint& K = PendingFlushQueue[0];
            const float Dx = (static_cast<float>(K.X) + 0.5f) * SpatialGridCellSize - CameraPos.X;
            const float Dy = (static_cast<float>(K.Y) + 0.5f) * SpatialGridCellSize - CameraPos.Y;
            const float QFrontDist = FMath::Sqrt(Dx * Dx + Dy * Dy);
            if (QFrontDist < 5000.f) BudgetThisFrame = 8;
            else if (QFrontDist < 10000.f) BudgetThisFrame = 4;
        }
    }

    int32 Processed = 0;
    while (PendingFlushQueue.Num() > 0 && Processed < BudgetThisFrame)
    {
        const FIntPoint Key = PendingFlushQueue[0];
        PendingFlushQueue.RemoveAt(0, 1, EAllowShrinking::No);
        PendingFlushSet.Remove(Key);

        if (FBeltChunk* Chunk = BeltChunks.Find(Key))
        {
            if (Chunk->bMeshDirty)
                FlushChunk(Key, *Chunk, CameraPos);
        }
        ++Processed;
    }
}

// ============================================================
//  传送带 LOD 懒加载 / Chunk 可见性管理
// ============================================================

void UMassDspManager::RebuildLUTForTrajectory(int32 TrajIndex, int32 LODLevel)
{
    if (!BeltTrajectories.IsValidIndex(TrajIndex)) return;
    if (!BeltRebuildData.IsValidIndex(TrajIndex)) return;
    if (!BeltsContainerActor) return;

    FBeltTrajectory& Traj = BeltTrajectories[TrajIndex];
    const FBeltRebuildData& RD = BeltRebuildData[TrajIndex];

    check(SharedSplineHelper != nullptr);

    if (RD.RebuildType == EBeltRebuildType::Dubins)
    {
        // Dubins: 纯解析采样，零 USplineComponent API 调用
        Traj.BakeLUTForLOD(RD.DubinsData, LODLevel);
    }
    else
    {
        // Hermite: 仍通过 Spline 曲线积分
        SharedSplineHelper->ClearSplinePoints(false);
        BuildBeltSplineFromPoints(SharedSplineHelper, RD.HermiteData.A, RD.HermiteData.B,
                                  RD.HermiteData.C, RD.HermiteData.D);
        Traj.BakeLUTForLOD(SharedSplineHelper, LODLevel);
    }
}

void UMassDspManager::UpdateBeltLODs(const FVector& CameraPos)
{
    // 注意：此函数应在游戏线程调用（创建/销毁 USplineComponent 需要游戏线程）
    // 建议调用频率：~0.5-1Hz（每秒一次），避免频繁重建开销
    for (auto It = BeltTrajectories.CreateIterator(); It; ++It)
    {
        FBeltTrajectory& Traj = *It;
        const float Dist = FVector::Dist(CameraPos, Traj.RepresentativePosition);

        if (Dist > LUTUnloadDistance)
        {
            // 超出卸载距离（含滞后余量）：释放 LUT 内存
            if (Traj.CurrentLOD >= 0)
                Traj.UnloadLUT();
        }
        else
        {
            const int32 TargetLOD = ComputeBeltLODLevel(Dist);
            if (Traj.CurrentLOD != TargetLOD)
            {
                // LOD 等级变化（含从未加载 → 首次加载）：重建 LUT
                RebuildLUTForTrajectory(It.GetIndex(), TargetLOD);
            }
        }
    }
}

void UMassDspManager::UpdateBeltChunkVisibility(const FVector& CameraPos)
{
    // 收集视距内所有需刷新的 Chunk，按距离升序排列（最近优先），
    // 超出卸载距离的 Chunk 绝不入队，直接归还 PMC。
    struct FDirtyEntry
    {
        FIntPoint Key;
        float Dist;
    };
    TArray<FDirtyEntry> NewEntries;

    for (auto& [ChunkKey, Chunk] : BeltChunks)
    {
        const FVector ChunkCenter(
            (static_cast<float>(ChunkKey.X) + 0.5f) * SpatialGridCellSize,
            (static_cast<float>(ChunkKey.Y) + 0.5f) * SpatialGridCellSize,
            CameraPos.Z);

        const float Dist = FVector::Dist(CameraPos, ChunkCenter);
        const int32 TargetLOD = ComputeBeltLODLevel(Dist);

        if (Dist > LUTUnloadDistance + LUTLoadHysteresis)
        {
            // 超出卸载距离：归还 PMC 到对象池，从待刷新队列中移除
            if (Chunk.PMC)
            {
                Chunk.PMC->ClearAllMeshSections();
                if (FreePMCPool.Num() < MaxFreePMCPoolSize)
                    FreePMCPool.Add(Chunk.PMC);
                else
                    Chunk.PMC->DestroyComponent();
                Chunk.PMC = nullptr;
                Chunk.CurrentMeshLOD = -1;
            }
            // 移出队列（如果已入队），远距离 Chunk 不应占用刷新时间片
            if (PendingFlushSet.Remove(ChunkKey))
                PendingFlushQueue.Remove(ChunkKey);
        }
        else
        {
            // LOD 等级需要变化 → 标脏
            if (!Chunk.bMeshDirty && Chunk.CurrentMeshLOD != TargetLOD)
                Chunk.bMeshDirty = true;

            // 脏且未在队列中 → 收集待入队
            if (Chunk.bMeshDirty && !PendingFlushSet.Contains(ChunkKey))
                NewEntries.Add({ChunkKey, Dist});
        }
    }

    // 按距离升序排序后追加到队列末尾（最近的先处理）
    NewEntries.Sort([](const FDirtyEntry& A, const FDirtyEntry& B) { return A.Dist < B.Dist; });
    for (const FDirtyEntry& E : NewEntries)
    {
        PendingFlushSet.Add(E.Key);
        PendingFlushQueue.Add(E.Key);
    }
    // 不在此处立即 Flush：由调用方每帧调 TickBeltMeshFlush 分帧处理
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
//  Dubins 路径 —— file-scope 辅助实现
// ============================================================

namespace
{
    /** 规范角度到 [0, 2π) */
    static float DubNorm(float a)
    {
        const float t = FMath::Fmod(a, 2.f * PI);
        return t < 0.f ? t + 2.f * PI : t;
    }

    /** 词型 → 三段类型映射 */
    static void DubSegs(EDubinsWordType W, EDubinsSegType Out[3])
    {
        switch (W)
        {
        case EDubinsWordType::LSL: Out[0] = EDubinsSegType::Left;
            Out[1] = EDubinsSegType::Straight;
            Out[2] = EDubinsSegType::Left;
            break;
        case EDubinsWordType::RSR: Out[0] = EDubinsSegType::Right;
            Out[1] = EDubinsSegType::Straight;
            Out[2] = EDubinsSegType::Right;
            break;
        case EDubinsWordType::LSR: Out[0] = EDubinsSegType::Left;
            Out[1] = EDubinsSegType::Straight;
            Out[2] = EDubinsSegType::Right;
            break;
        case EDubinsWordType::RSL: Out[0] = EDubinsSegType::Right;
            Out[1] = EDubinsSegType::Straight;
            Out[2] = EDubinsSegType::Left;
            break;
        case EDubinsWordType::LRL: Out[0] = EDubinsSegType::Left;
            Out[1] = EDubinsSegType::Right;
            Out[2] = EDubinsSegType::Left;
            break;
        case EDubinsWordType::RLR: Out[0] = EDubinsSegType::Right;
            Out[1] = EDubinsSegType::Left;
            Out[2] = EDubinsSegType::Right;
            break;
        default: Out[0] = Out[1] = Out[2] = EDubinsSegType::Straight;
            break;
        }
    }

    /**
     * 沿 Dubins 段步进 ArcLen 距离并更新位置与朝向。
     * Left  = CCW 圆弧（转心在朝向左侧）
     * Right = CW  圆弧（转心在朝向右侧）
     */
    static void DubStep(FVector2D& Pos, float& H, EDubinsSegType Seg, float r, float ArcLen)
    {
        if (Seg == EDubinsSegType::Straight)
        {
            Pos.X += ArcLen * FMath::Cos(H);
            Pos.Y += ArcLen * FMath::Sin(H);
        }
        else if (Seg == EDubinsSegType::Left)
        {
            const float dT = ArcLen / r;
            const FVector2D C(Pos.X - r * FMath::Sin(H), Pos.Y + r * FMath::Cos(H));
            const float newH = H + dT;
            Pos = FVector2D(C.X + r * FMath::Sin(newH), C.Y - r * FMath::Cos(newH));
            H = newH;
        }
        else // Right
        {
            const float dT = ArcLen / r;
            const FVector2D C(Pos.X + r * FMath::Sin(H), Pos.Y - r * FMath::Cos(H));
            const float newH = H - dT;
            Pos = FVector2D(C.X - r * FMath::Sin(newH), C.Y + r * FMath::Cos(newH));
            H = newH;
        }
    }
} // namespace

// ============================================================
//  Dubins 路径 —— 公开接口实现
// ============================================================

FDubinsPathData UMassDspManager::ComputeDubinsPath(
    const FVector2D& StartPos, float StartHeading,
    const FVector2D& EndPos, float EndHeading,
    float r)
{
    FDubinsPathData Result;
    Result.StartPos = StartPos;
    Result.StartHeading = StartHeading;
    Result.EndPos = EndPos;
    Result.EndHeading = EndHeading;
    Result.TurningRadius = r;

    if (r <= 0.f) return Result;

    const float dx = EndPos.X - StartPos.X;
    const float dy = EndPos.Y - StartPos.Y;
    const float D = FMath::Sqrt(dx * dx + dy * dy) / r; // 归一化距离
    const float theta = FMath::Atan2(dy, dx); // 起点→终点方向角

    // 关键：两个朝向角必须相对于 theta 计算，否则所有公式均错误
    const float alpha = DubNorm(StartHeading - theta);
    const float beta = DubNorm(EndHeading - theta);

    const float sa = FMath::Sin(alpha), ca = FMath::Cos(alpha);
    const float sb = FMath::Sin(beta), cb = FMath::Cos(beta);
    const float cab = FMath::Cos(alpha - beta);

    float bestLen = FLT_MAX;

    // t, p, q 均为归一化圆弧角/直线长度；
    // 先检查原始符号再 DubNorm，防止负值被错误包裹为大正数
    auto TryWord = [&](EDubinsWordType W, float t_raw, float p_raw, float q_raw)
    {
        if (t_raw < -1e-6f || p_raw < -1e-6f || q_raw < -1e-6f) return;
        const float t = FMath::Max(0.f, t_raw);
        const float p = FMath::Max(0.f, p_raw);
        const float q = FMath::Max(0.f, q_raw);
        const float len = (t + p + q) * r;
        if (len < bestLen)
        {
            bestLen = len;
            Result.WordType = W;
            Result.SegLen[0] = t * r;
            Result.SegLen[1] = p * r;
            Result.SegLen[2] = q * r;
            Result.TotalLength = len;
        }
    };

    // ――― LSL ―――
    {
        const float pSq = 2.f + D * D - 2.f * cab + 2.f * D * (sa - sb);
        if (pSq >= 0.f)
        {
            const float p = FMath::Sqrt(pSq);
            const float tmp = FMath::Atan2(cb - ca, D + sa - sb);
            TryWord(EDubinsWordType::LSL, DubNorm(-alpha + tmp), p, DubNorm(beta - tmp));
        }
    }
    // ――― RSR ―――
    {
        const float pSq = 2.f + D * D - 2.f * cab + 2.f * D * (sb - sa);
        if (pSq >= 0.f)
        {
            const float p = FMath::Sqrt(pSq);
            const float tmp = FMath::Atan2(ca - cb, D - sa + sb);
            TryWord(EDubinsWordType::RSR, DubNorm(alpha - tmp), p, DubNorm(-beta + tmp));
        }
    }
    // ――― LSR ―――
    {
        const float pSq = -2.f + D * D + 2.f * cab + 2.f * D * (sa + sb);
        if (pSq >= 0.f)
        {
            const float p = FMath::Sqrt(pSq);
            const float tmp = FMath::Atan2(-ca - cb, D + sa + sb) - FMath::Atan2(-2.f, p);
            TryWord(EDubinsWordType::LSR, DubNorm(-alpha + tmp), p, DubNorm(-beta + tmp));
        }
    }
    // ――― RSL ―――
    {
        const float pSq = -2.f + D * D + 2.f * cab - 2.f * D * (sa + sb);
        if (pSq >= 0.f)
        {
            const float p = FMath::Sqrt(pSq);
            const float tmp = FMath::Atan2(ca + cb, D - sa - sb) - FMath::Atan2(2.f, p);
            TryWord(EDubinsWordType::RSL, DubNorm(alpha - tmp), p, DubNorm(beta - tmp));
        }
    }
    // ――― RLR ―――
    {
        const float tmp = (6.f - D * D + 2.f * cab + 2.f * D * (sa - sb)) / 8.f;
        if (FMath::Abs(tmp) <= 1.f)
        {
            const float p = DubNorm(2.f * PI - FMath::Acos(tmp));
            const float t_raw = DubNorm(alpha - FMath::Atan2(ca - cb, D - sa + sb) + p * 0.5f);
            TryWord(EDubinsWordType::RLR, t_raw, p, DubNorm(alpha - beta - t_raw + p));
        }
    }
    // ――― LRL ―――
    {
        const float tmp = (6.f - D * D + 2.f * cab + 2.f * D * (-sa + sb)) / 8.f;
        if (FMath::Abs(tmp) <= 1.f)
        {
            const float p = DubNorm(2.f * PI - FMath::Acos(tmp));
            const float t_raw = DubNorm(-alpha + FMath::Atan2(-ca + cb, D + sa - sb) + p * 0.5f);
            TryWord(EDubinsWordType::LRL, t_raw, p, DubNorm(beta - alpha - t_raw + p));
        }
    }

    return Result;
}

// TODO 直线段可以直接跳过采样
void UMassDspManager::BuildBeltSplineFromDubins(USplineComponent* Spline, const FDubinsPathData& Path)
{
    if (!Spline || !Path.IsValid()) return;

    constexpr float SampleStep = 10.f; // cm，等距采样间距
    constexpr float ZLift = 20.f; // cm，与 BuildBeltSplineFromPoints 保持一致

    EDubinsSegType SegTypes[3];
    DubSegs(Path.WordType, SegTypes);

    const float TotalLen = Path.TotalLength;
    const float dZdCm = (TotalLen > 0.f) ? (Path.EndZ - Path.StartZ) / TotalLen : 0.f;

    // ── 第一步：收集所有采样点（位置 + 朝向角）─────────────────────────────
    struct FSample
    {
        FVector Pos;
        float H;
    };
    TArray<FSample> Samples;
    Samples.Reserve(FMath::CeilToInt(TotalLen / SampleStep) + 6);

    // 如果有起点延伸，先添加 A 点（SlotExtend 直线段起点）
    const bool bHasStart = Path.bHasStartExtend;
    const bool bHasEnd = Path.bHasEndExtend;
    if (bHasStart)
    {
        FVector APos = Path.StartExtendPos;
        APos.Z += ZLift;
        // A 点的朝向 = A→B（即 Dubins 起点）方向
        const FVector BPos3D(Path.StartPos.X, Path.StartPos.Y, Path.StartZ + ZLift);
        const FVector AB = (BPos3D - APos).GetSafeNormal();
        Samples.Add({APos, static_cast<float>(FMath::Atan2(AB.Y, AB.X))});
    }

    FVector2D Pos2D = Path.StartPos;
    float H = Path.StartHeading;
    float Dist = 0.f;

    auto AddSample = [&]()
    {
        const float z = Path.StartZ + dZdCm * Dist + ZLift;
        Samples.Add({FVector(Pos2D.X, Pos2D.Y, z), H});
    };

    AddSample(); // B 点（Dubins 起点）

    const float r = Path.TurningRadius;
    for (int32 Seg = 0; Seg < 3; ++Seg)
    {
        float Rem = Path.SegLen[Seg];
        if (Rem <= KINDA_SMALL_NUMBER) continue;

        while (Rem > SampleStep + KINDA_SMALL_NUMBER)
        {
            DubStep(Pos2D, H, SegTypes[Seg], r, SampleStep);
            Dist += SampleStep;
            Rem -= SampleStep;
            AddSample();
        }
        DubStep(Pos2D, H, SegTypes[Seg], r, Rem);
        Dist += Rem;
    }

    // 强制精确终点，消除浮点累积误差
    Pos2D = Path.EndPos;
    H = Path.EndHeading;
    Dist = TotalLen;
    AddSample(); // C 点（Dubins 终点）

    // 如果有终点延伸，追加 D 点（SlotExtend 直线段终点）
    if (bHasEnd)
    {
        FVector DPos = Path.EndExtendPos;
        DPos.Z += ZLift;
        // D 点的朝向 = C→D 方向
        const FVector CPos3D(Path.EndPos.X, Path.EndPos.Y, Path.EndZ + ZLift);
        const FVector CD = (DPos - CPos3D).GetSafeNormal();
        Samples.Add({DPos, static_cast<float>(FMath::Atan2(CD.Y, CD.X))});
    }

    // ── 第二步：去除过近重复点（防止退化段）─────────────────────────────────
    constexpr float MinSepSq = (SampleStep * 0.5f) * (SampleStep * 0.5f);
    for (int32 i = Samples.Num() - 1; i > 0; --i)
    {
        if (FVector::DistSquared(Samples[i].Pos, Samples[i - 1].Pos) < MinSepSq)
        {
            // 保留靠近两端的点：若 i 是最后一个点，删 i-1；否则删 i
            if (i == Samples.Num() - 1)
                Samples.RemoveAt(i - 1, 1, EAllowShrinking::No);
            else
                Samples.RemoveAt(i, 1, EAllowShrinking::No);
        }
    }

    if (Samples.Num() < 2) return;

    // ── 第三步：写入样条 ───────────────────────────────────────────────────
    // 对应关系（有完整延伸时）：
    //   index 0          = A（Linear，A→B 段为直线）
    //   index 1          = B（CurveCustomTangent，in: A→B，out: Dubins 起始朝向）
    //   index 1..N-2     = Dubins 中间曲线（Catmull-Rom 自动切线）
    //   index N-2        = C（CurveCustomTangent，in: Dubins 终点朝向，out: C→D）
    //   index N-1        = D（Linear，C→D 段为直线）
    // 无延伸时回退到原逻辑（首尾 CurveCustomTangent）
    Spline->ClearSplinePoints(false);

    const int32 N = Samples.Num();
    const int32 StartExtIdx = bHasStart ? 0 : -1; // A
    const int32 DubStartIdx = bHasStart ? 1 : 0; // B
    const int32 DubEndIdx = bHasEnd ? N - 2 : N - 1; // C
    const int32 EndExtIdx = bHasEnd ? N - 1 : -1; // D

    const float TM = SampleStep * 3.f; // 首尾切线模长

    for (int32 i = 0; i < N; ++i)
    {
        const FSample& S = Samples[i];
        Spline->AddSplinePoint(S.Pos, ESplineCoordinateSpace::World, false);

        if (bHasStart && i == StartExtIdx)
        {
            // A 点：Linear → A→B 段自动成直线
            Spline->SetSplinePointType(i, ESplinePointType::Linear, false);
        }
        else if (i == DubStartIdx)
        {
            // B 点：CurveCustomTangent
            //   in-tangent: 若有 A 则对齐 A→B（保证直线段衔接）；否则沿 Dubins 起始方向
            //   out-tangent: Dubins 起始朝向
            Spline->SetSplinePointType(i, ESplinePointType::CurveCustomTangent, false);
            const FVector OutT(FMath::Cos(S.H) * TM, FMath::Sin(S.H) * TM, dZdCm * TM);
            const FVector InT = bHasStart
                                    ? (S.Pos - Samples[i - 1].Pos).GetSafeNormal() * TM
                                    : OutT;
            Spline->SetTangentsAtSplinePoint(i, InT, OutT, ESplineCoordinateSpace::World, false);
        }
        else if (i == DubEndIdx)
        {
            // C 点：CurveCustomTangent
            //   in-tangent: Dubins 终点朝向
            //   out-tangent: 若有 D 则对齐 C→D；否则沿 Dubins 终点朝向
            Spline->SetSplinePointType(i, ESplinePointType::CurveCustomTangent, false);
            const FVector InT(FMath::Cos(S.H) * TM, FMath::Sin(S.H) * TM, dZdCm * TM);
            const FVector OutT = bHasEnd
                                     ? (Samples[i + 1].Pos - S.Pos).GetSafeNormal() * TM
                                     : InT;
            Spline->SetTangentsAtSplinePoint(i, InT, OutT, ESplineCoordinateSpace::World, false);
        }
        else if (bHasEnd && i == EndExtIdx)
        {
            // D 点：Linear → C→D 段自动成直线
            Spline->SetSplinePointType(i, ESplinePointType::Linear, false);
        }
        else
        {
            // 中间 Dubins 曲线点：Catmull-Rom 自动切线，完全由位置决定
            Spline->SetSplinePointType(i, ESplinePointType::Curve, false);
        }
    }

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

// ============================================================
//  空间哈希网格 —— 辅助实现
// ============================================================

void UMassDspManager::RegisterBuildingInGrid(FMassEntityHandle Entity, const FVector& Location)
{
    const int32 CX = FMath::FloorToInt(Location.X / BuildingGridCellSize);
    const int32 CY = FMath::FloorToInt(Location.Y / BuildingGridCellSize);
    BuildingHashGrid.FindOrAdd(MakeBuildingCellKey(CX, CY)).Add(Entity);
}

void UMassDspManager::QueryBuildingGridRadius(const FVector& Center, float Radius, TArray<FMassEntityHandle>& OutEntities) const
{
    if (BuildingHashGrid.IsEmpty()) return;

    // 计算与查询 AABB 重叠的格子范围
    const int32 X0 = FMath::FloorToInt((Center.X - Radius) / BuildingGridCellSize);
    const int32 X1 = FMath::FloorToInt((Center.X + Radius) / BuildingGridCellSize);
    const int32 Y0 = FMath::FloorToInt((Center.Y - Radius) / BuildingGridCellSize);
    const int32 Y1 = FMath::FloorToInt((Center.Y + Radius) / BuildingGridCellSize);

    // 安全上限：单次查询最多覆盖 32×32 = 1024 个格；超出说明半径异常
    if ((X1 - X0) > 32 || (Y1 - Y0) > 32)
    {
        UE_LOG(LogTemp, Warning, TEXT("QueryBuildingGridRadius: radius %.0f is too large, clamped to 32 cells per axis"), Radius);
        return;
    }

    const int32 TotalCells = (X1 - X0 + 1) * (Y1 - Y0 + 1);
    OutEntities.Reserve(OutEntities.Num() + TotalCells * 4); // 粗估每格 4 个建筑

    for (int32 CX = X0; CX <= X1; ++CX)
    {
        for (int32 CY = Y0; CY <= Y1; ++CY)
        {
            if (const TArray<FMassEntityHandle>* Cell = BuildingHashGrid.Find(MakeBuildingCellKey(CX, CY)))
            {
                OutEntities.Append(*Cell);
            }
        }
    }
}

void UMassDspManager::FindBuildingsInRadius(const FVector& Center, float Radius, TArray<FMassEntityHandle>& OutEntities) const
{
    // 直接转发到内部哈希查询，对外提供公开接口
    QueryBuildingGridRadius(Center, Radius, OutEntities);
}

bool UMassDspManager::FindNearestBuilding(
    const FVector& PlayerLocation,
    float SearchRadius,
    FMassEntityHandle& OutEntity,
    EBuildingType& OutBuildingType,
    FVector& OutLocation,
    const TFunction<bool(const FVector&)>& LocationFilter)
{
    UMassEntitySubsystem* ESub = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
    if (!ESub) return false;

    FMassEntityManager& EM = ESub->GetMutableEntityManager();
    float BestDistSq = SearchRadius * SearchRadius;
    bool bFound = false;

    TArray<FMassEntityHandle> Candidates;
    QueryBuildingGridRadius(PlayerLocation, SearchRadius, Candidates);

    for (const FMassEntityHandle& Entity : Candidates)
    {
        if (!EM.IsEntityValid(Entity)) continue;

        const FTransformFragment* TF = EM.GetFragmentDataPtr<FTransformFragment>(Entity);
        if (!TF) continue;

        const FVector BuildingLoc = TF->GetTransform().GetLocation();
        if (LocationFilter && !LocationFilter(BuildingLoc)) continue;
        const float DistSq = FVector::DistSquared(PlayerLocation, BuildingLoc);
        if (DistSq < BestDistSq)
        {
            BestDistSq = DistSq;
            OutEntity = Entity;
            OutLocation = BuildingLoc;
            if (const EBuildingType* TypePtr = BuildingEntityTypeRegistry.Find(Entity))
                OutBuildingType = *TypePtr;
            else
                OutBuildingType = EBuildingType::None;
            bFound = true;
        }
    }
    return bFound;
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

    // 槽口可能偏离建筑中心最多 MaxBuildingSlotOffset，搜索半径外扩以免漏查
    TArray<FMassEntityHandle> Candidates;
    QueryBuildingGridRadius(WorldPos, SearchRadius + MaxBuildingSlotOffset, Candidates);

    for (const FMassEntityHandle& Entity : Candidates)
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
        Ghost->SetActorTransform(InitialTransform);
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

    TArray<FBuildingSpawnData> SpawnList;
    SpawnList.Add(FBuildingSpawnData(FinalTransform, BuildingType));
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

void UMassDspManager::BeginPreviewBelt(EBeltType BeltType, EBeltSplineType SplineType)
{
    CancelAnyPreview();

    PreviewBeltType = BeltType;
    PreviewBeltSplineType = SplineType;
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
    const EBeltSplineType SplineType = PreviewBeltSplineType; // 在 Cancel 前捕捉
    const FMassEntityHandle StartEnt = BeltStartEntity;
    const int32 StartSlotIdx = BeltStartSlotIndex;
    const FMassEntityHandle EndEnt = BeltEndEntity;
    const int32 EndSlotIdx = BeltEndSlotIndex;

    CancelBeltPreview();

    FBeltHandle Handle = CreateAndLinkBeltForSlot(StartEnt, StartSlotIdx, EndEnt, EndSlotIdx, BeltType, SplineType);
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

    // 根据样条类型选择构建方式
    if (PreviewBeltSplineType == EBeltSplineType::DubinsPath)
    {
        const FVector SF = BeltStartSlotRotation.RotateVector(FVector(1.f, 0.f, 0.f));
        const float SH = FMath::Atan2(SF.Y, SF.X);
        float EH;
        if (EndSlotExtend > 0.f)
        {
            const FVector EF = EndSlotRotation.RotateVector(FVector(1.f, 0.f, 0.f));
            EH = FMath::Atan2(-EF.Y, -EF.X);
        }
        else
        {
            const FVector2D Dir(EndWorldPos.X - BeltStartSlotLocation.X,
                                EndWorldPos.Y - BeltStartSlotLocation.Y);
            EH = FMath::Atan2(Dir.Y, Dir.X);
        }
        FDubinsPathData DP = ComputeDubinsPath(
            FVector2D(BeltStartSlotLocation.X, BeltStartSlotLocation.Y), SH,
            FVector2D(EndWorldPos.X, EndWorldPos.Y), EH,
            DubinsMinTurningRadius);
        DP.StartZ = BeltStartSlotLocation.Z;
        DP.EndZ = EndWorldPos.Z;
        // 起点延伸（始终存在）
        DP.bHasStartExtend = true;
        DP.StartExtendPos = A;
        // 终点延伸（仅在已吸附到终点槽口时存在）
        if (EndSlotExtend > 0.f)
        {
            DP.bHasEndExtend = true;
            DP.EndExtendPos = D;
        }
        if (DP.IsValid())
        {
            BuildBeltSplineFromDubins(PreviewSpline, DP);
        }
        else
        {
            BuildBeltSplineFromPoints(PreviewSpline, A, B, C, D); // 失效时回退到 Hermite
        }
    }
    else
    {
        BuildBeltSplineFromPoints(PreviewSpline, A, B, C, D);
    }

    // 重用 GenerateConveyorMesh 生成预览几何（独立 MeshData，不写入 Chunk）
    // 预览始终使用 LOD1 参数（50cm LUT，5° 角度阈值），保证预览与最终结果视觉一致
    FMergedBeltMeshData PreviewMeshData;
    GenerateConveyorMesh(PreviewMeshData, PreviewSpline, C_Width, C_BeltThickness, C_UVScale,
                         BeltLODAngleThresholds[1], BeltLODMaxSegLengths[1]);

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

    // 通过哈希网格缩小候选集：仅遍历 (HighlightRadius + MaxBuildingSlotOffset) 范围内的建筑
    TArray<FMassEntityHandle> Candidates;
    QueryBuildingGridRadius(WorldPos, HighlightRadius + MaxBuildingSlotOffset, Candidates);

    for (const FMassEntityHandle& Entity : Candidates)
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
