#include "MassDspGameMode.h"

#include "Subsystems/MassDspManager.h"

#include "Actors/MassDspMiner.h"
#include "Actors/MassDspStorage.h"
#include "Actors/MassDspAssembler.h"

#include "Misc/CoreDelegates.h"
#include "Engine/LocalPlayer.h"
#include "SceneManagement.h"

AMassDspGameMode::AMassDspGameMode()
{
    PrimaryActorTick.bCanEverTick = true;
}

void AMassDspGameMode::BeginPlay()
{
    Super::BeginPlay();

    if (GEngine)
    {
        GEngine->bEnableOnScreenDebugMessages = true;
    }

    UWorld* World = GetWorld();
    UMassDspManager* DspManager = World->GetSubsystem<UMassDspManager>();
    if (!DspManager) return;

    if (!MinerClass || !StorageClass || !AssemblerClass)
    {
        UE_LOG(LogTemp, Warning, TEXT("Building Classes not set in GameMode!"));
        return;
    }

    // ===== 新方案：批量创建Building Entity（无Actor实例化）=====

    // 大规模创建

    // TODO 5w建筑的时候帧率跌得有点夸张,得优化下
    constexpr int N = 40;
    constexpr int BuildingsPerGroup = 5; // 每组：3矿机 + 1合成台 + 1仓库

    // 第一步：收集所有Building生成数据
    TArray<FBuildingSpawnData> AllBuildingDataList;
    AllBuildingDataList.Reserve(N * N * BuildingsPerGroup);

    for (int i = -N / 2; i < N / 2; ++i)
    {
        for (int j = -N / 2; j < N / 2; ++j)
        {
            constexpr int GridSize = 4000;
            FVector SpawnLocation = FVector(i * GridSize, j * GridSize, 0);

            // 3个矿机
            AllBuildingDataList.Add(FBuildingSpawnData(
                MinerClass,
                FTransform(FRotator(0, 90, 0), SpawnLocation + FVector(0, 0, 0)),
                EBuildingType::Miner
            ));
            AllBuildingDataList.Add(FBuildingSpawnData(
                MinerClass,
                FTransform(FRotator(0, 90, 0), SpawnLocation + FVector(1000, 0, 0)),
                EBuildingType::Miner
            ));
            AllBuildingDataList.Add(FBuildingSpawnData(
                MinerClass,
                FTransform(FRotator(0, 90, 0), SpawnLocation + FVector(2000, 0, 0)),
                EBuildingType::Miner
            ));

            // 1个合成台
            AllBuildingDataList.Add(FBuildingSpawnData(
                AssemblerClass,
                FTransform(FRotator(0, 0, 0), SpawnLocation + FVector(1000, 1000, 0)),
                EBuildingType::Assembler
            ));

            // 1个仓库
            AllBuildingDataList.Add(FBuildingSpawnData(
                StorageClass,
                FTransform(FRotator(0, 180, 0), SpawnLocation + FVector(1000, 2000, 0)),
                EBuildingType::Storage
            ));
        }
    }

    // 第二步：单次批量创建所有Building Entity
    TArray<FMassEntityHandle> AllCreatedBuildings = DspManager->BatchSpawnBuildings(AllBuildingDataList);

    // 第三步：遍历每组，连接传送带
    constexpr int BeltsPerGroup = 4; // 每组传送带数量
    constexpr int TotalBelts = N * N * BeltsPerGroup;

    const double BeltLinkStartTime = FPlatformTime::Seconds();

    for (int GroupIndex = 0; GroupIndex < N * N; ++GroupIndex)
    {
        const int BaseIndex = GroupIndex * BuildingsPerGroup;

        FMassEntityHandle MinerEntity1 = AllCreatedBuildings[BaseIndex + 0];
        FMassEntityHandle MinerEntity2 = AllCreatedBuildings[BaseIndex + 1];
        FMassEntityHandle MinerEntity3 = AllCreatedBuildings[BaseIndex + 2];
        FMassEntityHandle AssemblerEntity = AllCreatedBuildings[BaseIndex + 3];
        FMassEntityHandle StorageEntity = AllCreatedBuildings[BaseIndex + 4];

        // 创建传送带连接
        DspManager->CreateAndLinkBeltForSlot(MinerEntity1, 0, AssemblerEntity, 2, EBeltType::Normal);
        DspManager->CreateAndLinkBeltForSlot(MinerEntity2, 0, AssemblerEntity, 1, EBeltType::Normal);
        DspManager->CreateAndLinkBeltForSlot(MinerEntity3, 0, AssemblerEntity, 0, EBeltType::Normal);
        DspManager->CreateAndLinkBeltForSlot(AssemblerEntity, 0, StorageEntity, 0, EBeltType::Fast);
    }

    const double BeltLinkElapsed = FPlatformTime::Seconds() - BeltLinkStartTime;
    const double AvgBeltTimeMs = (TotalBelts > 0) ? (BeltLinkElapsed * 1000.0 / TotalBelts) : 0.0;
    UE_LOG(LogTemp, Log, TEXT("[Belt Profile] Total: %d belts | Total time: %.3f ms | Avg per belt: %.4f ms"),
           TotalBelts, BeltLinkElapsed * 1000.0, AvgBeltTimeMs);

    DspManager->FlushBeltMesh();
}

void AMassDspGameMode::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    ProcessConveyor(DeltaTime);
}

void AMassDspGameMode::ProcessConveyor(float DeltaTime) const
{
    UMassDspManager* Manager = GetWorld()->GetSubsystem<UMassDspManager>();
    if (!Manager || Manager->BeltEntityRegistry.IsEmpty()) return;

    // --- Step 1: 缓存所有 Belt 指针，避免 ParallelFor 里访问 TMap ---
    TArray<FBeltHandle> ActiveBelts;
    Manager->BeltEntityRegistry.GetKeys(ActiveBelts);

    TArray<FBeltData*> BeltDataArray;
    BeltDataArray.Reserve(ActiveBelts.Num());
    for (const FBeltHandle& Handle : ActiveBelts)
    {
        BeltDataArray.Add(Manager->BeltEntityRegistry.Find(Handle));
    }

    // --- Step 2: 并行更新各传送带物品位置（纯连续 TArray，无随机内存访问）---
    ParallelFor(BeltDataArray.Num(), [&](int BeltIdx)
    {
        FBeltData* BeltData = BeltDataArray[BeltIdx];
        if (!BeltData || BeltData->ItemCache.IsEmpty()) return;

        if (const auto& [Index, Generation] = ActiveBelts[BeltIdx]; !Manager->BeltTrajectories.IsValidIndex(Index)) return;

        const float BeltLength = BeltData->BeltLength;

        const float Speed = BeltData->BeltSpeed;

        // 末端阻挡位：最后一个物品不能超过传送带末端
        float LastItemTail = BeltLength - FGameConst::HalfLength;

        // 同一 Belt 内必须顺序遍历（前驱物品决定后驱物品的上限）
        for (FBeltItemCache& Item : BeltData->ItemCache)
        {
            if (float Desired = Item.DistanceAlongBelt + Speed * DeltaTime; Desired > LastItemTail)
            {
                Item.DistanceAlongBelt = LastItemTail;
                Item.bIsBlocked = true;
            }
            else
            {
                Item.DistanceAlongBelt = Desired;
                Item.bIsBlocked = false;
            }

            LastItemTail = Item.DistanceAlongBelt
                - FGameConst::HalfLength * 2.f
                - FGameConst::MinSpacing;
        }
    });

    // --- Step 3: ~30fps 同步视锥体内物品 Transform 到 ISM ---
    // 视野外传送带完全跳过（CPU 侧视锥剔除），GPU 上传量 = O(可见物品数)
    Manager->SyncAccum += DeltaTime;
    if (Manager->SyncAccum >= 1.0f / 60.0f)
    {
        Manager->SyncAccum = 0.f;

        // 构建当前帧视锥体（ViewProjectionMatrix → FConvexVolume）
        FConvexVolume ViewFrustum;
        FVector CamLoc = FVector::ZeroVector;
        APlayerController* PC = GetWorld()->GetFirstPlayerController();
        ULocalPlayer* LP = PC ? PC->GetLocalPlayer() : nullptr;
        if (LP && LP->ViewportClient && LP->ViewportClient->Viewport)
        {
            FSceneViewProjectionData ProjData;
            if (LP->GetProjectionData(LP->ViewportClient->Viewport, ProjData))
            {
                GetViewFrustumBounds(ViewFrustum, ProjData.ComputeViewProjectionMatrix(),
                                     /*bUseNearPlane=*/true);
                CamLoc = ProjData.ViewOrigin;
            }
        }
        Manager->UpdateAllBeltItemTransforms(ViewFrustum, CamLoc);
    }
}
