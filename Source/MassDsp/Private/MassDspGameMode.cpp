#include "MassDspGameMode.h"

#include "Subsystems/MassDspManager.h"
#include "Subsystems/MassDspLogisticsSubsystem.h"

#include "Logistics/MassDspDroneStrategy.h"
#include "Fragments/MassDspLogisticsTowerFragment.h"
#include "Fragments/MassDspMinerFragment.h"

#include "MassEntitySubsystem.h"
#include "Engine/Engine.h"

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

    // TestCase1();
    // TestCase2();
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

    // --- Step 1: 若传送带数量变化则 O(N) 重建 SoA（正常情况不触发）---
    if (Manager->BeltEntityRegistry.Num() != Manager->Belt_CachedCount)
        Manager->RebuildBeltSoA();

    const int32 NumBelts = Manager->Belt_Ptrs.Num();
    if (NumBelts == 0) return;

    float* RESTRICT TM = Manager->Belt_TotalMove.GetData();
    const float* RESTRICT BS = Manager->Belt_Speed.GetData();
    FBeltData** RESTRICT Ptrs = Manager->Belt_Ptrs.GetData();

    // --- Step 2 Pass 2: 同步回 BeltData + 阻塞组合并检查 + Rebase ---
    static constexpr float RebaseThreshold = 1e6f;
    for (int32 i = 0; i < NumBelts; ++i)
    {
        TM[i] += BS[i] * DeltaTime;
        FBeltData* BeltData = Ptrs[i];
        BeltData->TotalMove = TM[i];

        const int32 N = BeltData->ItemCache.Num();
        if (N == 0) continue;

        if (BeltData->BlockedCount < N)
        {
            const float BackOfGroup = (BeltData->BlockedCount > 0)
                                          ? BeltData->GetGroupFront()
                                          - static_cast<float>(BeltData->BlockedCount) * FGameConst::ItemSpace
                                          : BeltData->BeltLength - FGameConst::HalfLength;

            const float FrontFreePos =
                BeltData->ItemCache[BeltData->BlockedCount].Offset + TM[i];

            if (FrontFreePos >= BackOfGroup)
            {
                if (BeltData->BlockedCount == 0)
                    BeltData->GroupFrontOffset =
                        (BeltData->BeltLength - FGameConst::HalfLength) - TM[i];
                ++BeltData->BlockedCount;
            }
        }

        if (TM[i] > RebaseThreshold)
        {
            for (int32 j = BeltData->BlockedCount; j < N; ++j)
                BeltData->ItemCache[j].Offset += TM[i];
            BeltData->GroupFrontOffset += TM[i];
            TM[i] = BeltData->TotalMove = 0.f;
        }
    }

    // --- Step 3: ~30fps 同步视锥体内物品 Transform 到 ISM ---
    // 视野外传送带完全跳过（CPU 侧视锥剔除），GPU 上传量 = O(可见物品数)
    Manager->SyncAccum += DeltaTime;
    if (true || Manager->SyncAccum >= 1.0f / 60.0f)
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


void AMassDspGameMode::TestCase1() const
{
    UWorld* World = GetWorld();
    UMassDspManager* DspManager = World->GetSubsystem<UMassDspManager>();
    if (!DspManager) return;

    constexpr int N = 100;
    constexpr int BuildingsPerGroup = 5; // 每组：3矿机 + 1合成台 + 1仓库

    // 第一步：收集所有Building生成数据
    TArray<FBuildingSpawnData> AllBuildingDataList;
    AllBuildingDataList.Reserve(N * N * BuildingsPerGroup);

    for (int i = 0; i < N; ++i)
    {
        for (int j = 0; j < N; ++j)
        {
            constexpr int GridSize = 2800;
            FVector SpawnLocation = FVector((i - N / 2) * GridSize, (j - N / 2) * GridSize, 0);

            // 3个矿机
            AllBuildingDataList.Add(FBuildingSpawnData(
                FTransform(FRotator(0, 90, 0), SpawnLocation + FVector(0, 0, 0)),
                EBuildingType::Miner
            ));
            AllBuildingDataList.Add(FBuildingSpawnData(
                FTransform(FRotator(0, 90, 0), SpawnLocation + FVector(1000, 0, 0)),
                EBuildingType::Miner
            ));
            AllBuildingDataList.Add(FBuildingSpawnData(
                FTransform(FRotator(0, 90, 0), SpawnLocation + FVector(2000, 0, 0)),
                EBuildingType::Miner
            ));

            // 1个合成台
            AllBuildingDataList.Add(FBuildingSpawnData(
                FTransform(FRotator(0, 0, 0), SpawnLocation + FVector(1000, 1000, 0)),
                EBuildingType::Assembler
            ));

            // 1个仓库
            AllBuildingDataList.Add(FBuildingSpawnData(
                FTransform(FRotator(0, 180, 0), SpawnLocation + FVector(1000, 2000, 0)),
                EBuildingType::Storage
            ));
        }
    }

    auto Time1 = FPlatformTime::Seconds();

    // 第二步：单次批量创建所有Building Entity
    TArray<FMassEntityHandle> AllCreatedBuildings = DspManager->BatchSpawnBuildings(AllBuildingDataList);

    auto Time2 = FPlatformTime::Seconds();

    const double SpawnElapsed = Time2 - Time1;
    const double AvgSpawnTimeMs = (AllCreatedBuildings.Num() > 0) ? (SpawnElapsed * 1000.0 / AllCreatedBuildings.Num()) : 0.0;
    UE_LOG(LogTemp, Log, TEXT("[Spawn Profile] Total: %d entities | Total time: %.3f ms | Avg per entity: %.4f ms"),
           AllCreatedBuildings.Num(), SpawnElapsed * 1000.0, AvgSpawnTimeMs);

    // 第三步：遍历每组，连接传送带
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
        DspManager->CreateAndLinkBeltForSlot(MinerEntity2, 0, AssemblerEntity, 1, EBeltType::Express);
        DspManager->CreateAndLinkBeltForSlot(MinerEntity3, 0, AssemblerEntity, 0, EBeltType::Normal);
        DspManager->CreateAndLinkBeltForSlot(AssemblerEntity, 0, StorageEntity, 0, EBeltType::Fast);
    }
}

void AMassDspGameMode::TestCase2() const
{
    // ─────────────────────────────────────────────────────────────────────────
    // 物流演示（供应塔与需求塔在大范围内随机散布，供需比可调）
    //
    //   供应塔和需求塔各自独立随机分布，不再配对紧挨，无人机跨越整个场地调度
    //   所有塔使用同一物品类型，确保任意供需塔之间均可调度
    //
    //  ┌─────────────────────────── 可调常数 ──────────────────────────────┐
    //  │  NumSupplyTowers  —— 供应塔数量（与 NumDemandTowers 之比即供需比）│
    //  │  NumDemandTowers  —— 需求塔数量                                   │
    //  │  DronesPerTower   —— 每个供应塔无人机数量                         │
    //  │  SpawnRange       —— 塔随机散布半径（cm）                         │
    //  │  MinTowerDist     —— 两塔之间最小间距（cm，防止重叠）             │
    //  └───────────────────────────────────────────────────────────────────┘
    // ─────────────────────────────────────────────────────────────────────────

    // TODO 优化物流系统

    // ═══════════════════════════ 可调常数 ════════════════════════════════════
    constexpr int32 NumSupplyTowers = 100; // 供应塔数量
    constexpr int32 NumDemandTowers = 100; // 需求塔数量（供需比 = 15:5 = 3:1）
    constexpr int32 DronesPerTower = 150; // 每个供应塔无人机数量
    constexpr float SpawnRange = 10000.f; // 随机散布半径（cm，±500m）
    constexpr float MinTowerDist = 1000.f; // 两塔最小间距（cm）
    constexpr float IntraSpacing = 800.f; // 矿机/仓库 与塔的距离（cm）
    constexpr int32 RandSeed = 42; // 固定种子，保证每次运行位置相同
    // ═════════════════════════════════════════════════════════════════════════

    constexpr EItemType ItemType = EItemType::IronOre; // 所有塔使用同一物品类型
    constexpr int32 NumTotalBuildings = NumSupplyTowers * 2 + NumDemandTowers * 2;
    //   供应组：矿机 + 供应塔（×NumSupplyTowers）
    //   需求组：需求塔 + 仓库（×NumDemandTowers）

    auto World = GetWorld();
    UMassDspLogisticsSubsystem* LogisticsSub = World->GetSubsystem<UMassDspLogisticsSubsystem>();
    auto DspManager = World->GetSubsystem<UMassDspManager>();
    if (!LogisticsSub || !GameConfig)
        return;

    FRandomStream Rand(RandSeed);

    // ─── 随机生成塔的 2D 位置，保证相互间距 ≥ MinTowerDist ───────────────
    TArray<FVector2D> AllTowerPos; // 已占用位置（用于碰撞检测）
    AllTowerPos.Reserve(NumSupplyTowers + NumDemandTowers);

    auto TryGetRandomPos = [&](FVector2D& OutPos) -> bool
    {
        for (int32 Tries = 0; Tries < 200; ++Tries)
        {
            const FVector2D Candidate(
                Rand.FRandRange(-SpawnRange, SpawnRange),
                Rand.FRandRange(-SpawnRange, SpawnRange));
            bool bFarEnough = true;
            for (const FVector2D& Occupied : AllTowerPos)
            {
                if (FVector2D::Distance(Candidate, Occupied) < MinTowerDist)
                {
                    bFarEnough = false;
                    break;
                }
            }
            if (bFarEnough)
            {
                OutPos = Candidate;
                AllTowerPos.Add(Candidate);
                return true;
            }
        }
        // 超出重试次数：强制放置（极低概率，仅当场地极度拥挤时）
        OutPos = FVector2D(
            Rand.FRandRange(-SpawnRange, SpawnRange),
            Rand.FRandRange(-SpawnRange, SpawnRange));
        AllTowerPos.Add(OutPos);
        return false;
    };

    TArray<FVector2D> SupplyPos, DemandPos;
    SupplyPos.Reserve(NumSupplyTowers);
    DemandPos.Reserve(NumDemandTowers);

    for (int32 i = 0; i < NumSupplyTowers; ++i)
    {
        FVector2D P;
        TryGetRandomPos(P);
        SupplyPos.Add(P);
    }
    for (int32 i = 0; i < NumDemandTowers; ++i)
    {
        FVector2D P;
        TryGetRandomPos(P);
        DemandPos.Add(P);
    }

    // ─── 批量生成建筑数据 ─────────────────────────────────────────────────
    // 布局：
    //   供应组（每项 2 个建筑，索引 i*2, i*2+1）：
    //     [i*2+0]  矿机     位于供应塔 −X 侧 IntraSpacing 处
    //     [i*2+1]  供应塔   位于 SupplyPos[i]
    //   需求组（每项 2 个建筑，基础索引 NumSupplyTowers*2）：
    //     [base+j*2+0]  需求塔   位于 DemandPos[j]
    //     [base+j*2+1]  仓库     位于需求塔 +X 侧 IntraSpacing 处
    TArray<FBuildingSpawnData> SpawnData;
    SpawnData.Reserve(NumTotalBuildings);

    for (int32 i = 0; i < NumSupplyTowers; ++i)
    {
        const FVector TowerPos(SupplyPos[i].X, SupplyPos[i].Y, 0.f);
        SpawnData.Add({
            FTransform(FRotator(0, 90, 0), TowerPos + FVector(-IntraSpacing, 0.f, 1000.f)),
            EBuildingType::Miner
        });
        SpawnData.Add({
            FTransform(FRotator::ZeroRotator, TowerPos + FVector(0, 0.f, 2000.f)),
            EBuildingType::LogisticsTower
        });
    }

    for (int32 j = 0; j < NumDemandTowers; ++j)
    {
        const FVector TowerPos(DemandPos[j].X, DemandPos[j].Y, 0.f);
        SpawnData.Add({
            FTransform(FRotator::ZeroRotator, TowerPos + FVector(0, 0.f, 2000.f)),
            EBuildingType::LogisticsTower
        });
        SpawnData.Add({
            FTransform(FRotator(0, 180, 0), TowerPos + FVector(IntraSpacing, 0.f, 1000.f)),
            EBuildingType::Storage
        });
    }

    TArray<FMassEntityHandle> Entities = DspManager->BatchSpawnBuildings(SpawnData);
    if (Entities.Num() < NumTotalBuildings)
    {
        UE_LOG(LogTemp, Error, TEXT("[Logistics] 建筑批量创建失败，期望 %d 实际 %d"),
               NumTotalBuildings, Entities.Num());
        return;
    }

    // ─── Fragment 写入 + 传送带连接 ──────────────────────────────────────
    if (UMassEntitySubsystem* ESub = World->GetSubsystem<UMassEntitySubsystem>())
    {
        FMassEntityManager& EM = ESub->GetMutableEntityManager();

        // 供应组
        for (int32 i = 0; i < NumSupplyTowers; ++i)
        {
            const int32 Base = i * 2;
            const FMassEntityHandle MinerEnt = Entities[Base + 0];
            const FMassEntityHandle SupplyEnt = Entities[Base + 1];

            DspManager->CreateAndLinkBeltForSlot(MinerEnt, 0, SupplyEnt, 0, EBeltType::Express);

            if (FMassDspMinerFragment* MF = EM.GetFragmentDataPtr<FMassDspMinerFragment>(MinerEnt))
                MF->StoredItemType = ItemType;

            if (FMassDspLogisticsTowerFragment* SF =
                EM.GetFragmentDataPtr<FMassDspLogisticsTowerFragment>(SupplyEnt))
            {
                SF->TowerMode = ELogisticsTowerMode::Supply;
                SF->ItemType = ItemType;
                SF->RequestThreshold = 0;
                SF->DroneCargoCount = FGameConst::DroneCarryCapacity; // 8
                SF->ScanInterval = 0.5f;
            }
        }

        // 需求组
        const int32 DemandBase = NumSupplyTowers * 2;
        for (int32 j = 0; j < NumDemandTowers; ++j)
        {
            const int32 Base = DemandBase + j * 2;
            const FMassEntityHandle DemandEnt = Entities[Base + 0];
            const FMassEntityHandle StorageEnt = Entities[Base + 1];

            DspManager->CreateAndLinkBeltForSlot(DemandEnt, 0, StorageEnt, 0, EBeltType::Express);

            if (FMassDspLogisticsTowerFragment* DF =
                EM.GetFragmentDataPtr<FMassDspLogisticsTowerFragment>(DemandEnt))
            {
                DF->TowerMode = ELogisticsTowerMode::Demand;
                DF->ItemType = ItemType;
                DF->RequestThreshold = 500;
                DF->DroneCargoCount = FGameConst::DroneCarryCapacity; // 8
                DF->ScanInterval = 0.5f;
            }
        }
    }

    // ── 每个供应塔创建 DronesPerTower 架无人机 ─────────────────────────────
    int32 TotalDrones = 0;
    for (int32 i = 0; i < NumSupplyTowers; ++i)
    {
        const FMassEntityHandle SupplyEnt = Entities[i * 2 + 1];
        const FVector TowerPos = SpawnData[i * 2 + 1].WorldTransform.GetLocation();
        for (int32 d = 0; d < DronesPerTower; ++d)
            LogisticsSub->CreateDrone(SupplyEnt, TowerPos,
                                      FGameConst::DefaultDroneFlightSpeed,
                                      FGameConst::DroneCarryCapacity);
        TotalDrones += DronesPerTower;
    }

    // ── 每个需求塔创建 DronesPerTower 架无人机 ─────────────────────────────
    const int32 DemandBase = NumSupplyTowers * 2;
    for (int32 j = 0; j < NumDemandTowers; ++j)
    {
        const FMassEntityHandle DemandEnt = Entities[DemandBase + j * 2 + 0];
        const FVector TowerPos = SpawnData[DemandBase + j * 2 + 0].WorldTransform.GetLocation();
        for (int32 d = 0; d < DronesPerTower; ++d)
            LogisticsSub->CreateDrone(DemandEnt, TowerPos,
                                      FGameConst::DefaultDroneFlightSpeed,
                                      FGameConst::DroneCarryCapacity);
        TotalDrones += DronesPerTower;
    }

    UE_LOG(LogTemp, Log,
           TEXT("[Logistics] 初始化完成 | 供应塔 %d | 需求塔 %d | 供需比 %d:%d | 无人机 %d | 范围 ±%.0fcm (种子=%d)"),
           NumSupplyTowers, NumDemandTowers, NumSupplyTowers, NumDemandTowers,
           TotalDrones, SpawnRange, RandSeed);
}
