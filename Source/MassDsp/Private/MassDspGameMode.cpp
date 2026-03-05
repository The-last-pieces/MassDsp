#include "MassDspGameMode.h"

#include "Subsystems/MassDspManager.h"
#include "Subsystems/MassDspLogisticsSubsystem.h"

#include "Actors/MassDspMiner.h"
#include "Actors/MassDspStorage.h"
#include "Actors/MassDspAssembler.h"
#include "Actors/MassDspLogisticsTower.h"

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
    TestCase2();
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


void AMassDspGameMode::TestCase1() const
{
    UWorld* World = GetWorld();
    UMassDspManager* DspManager = World->GetSubsystem<UMassDspManager>();
    if (!DspManager) return;

    constexpr int N = 10; // TODO 300的时候内存炸了
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

    DspManager->FlushBeltMesh();
}

void AMassDspGameMode::TestCase2()
{
    // ─────────────────────────────────────────────────────────────────────────
    // DSP 行星内物流演示（固定种子 = 42，100 组供需配对，全自动调度）
    //
    //   10 × 10 网格，每格间距 8000 cm
    //   每组结构（沿 X 轴排列）：
    //     矿机 ──Express──▶ 供应塔 ◀···无人机···▶ 需求塔 ──Express──▶ 仓库
    //
    //   随机物品类型：IronOre / CopperOre / Stone / Coal（种子 42 固定）
    //   供应塔：Supply 模式，RequestThreshold=50，DroneCargoCount=5
    //   需求塔：Demand 模式，RequestThreshold=20，DroneCargoCount=5
    //   无人机：每个供应塔 3 架（共 300 架）
    // ─────────────────────────────────────────────────────────────────────────

    auto World = GetWorld();
    UMassDspLogisticsSubsystem* LogisticsSub = World->GetSubsystem<UMassDspLogisticsSubsystem>();
    auto DspManager = World->GetSubsystem<UMassDspManager>();
    if (!LogisticsSub || !LogisticsTowerClass)
    {
        return;
    }

    constexpr int32 GroupRows = 10;
    constexpr int32 GroupCols = 10;
    constexpr int32 NumGroups = GroupRows * GroupCols; // 100
    constexpr int32 DronesPerTower = 100;
    constexpr int32 BuildingsPerGroup = 4; // Miner+Supply+Demand+Storage

    // 固定种子随机流（保证每次运行结果相同）
    FRandomStream Rand(42);

    static constexpr EItemType ItemTypes[] = {
        EItemType::IronOre, // EItemType::CopperOre, EItemType::Stone, EItemType::Coal
    };
    static constexpr int32 NumItemTypes = UE_ARRAY_COUNT(ItemTypes);

    TArray<FBuildingSpawnData> LogisticsSpawn;
    LogisticsSpawn.Reserve(NumGroups * BuildingsPerGroup);

    TArray<EItemType> GroupItemTypes;
    GroupItemTypes.Reserve(NumGroups);

    for (int32 Row = 0; Row < GroupRows; ++Row)
    {
        for (int32 Col = 0; Col < GroupCols; ++Col)
        {
            constexpr float IntraSpacing = 1500.f;
            constexpr float GroupSpacingX = 4000.f;
            constexpr float GroupSpacingY = 2000.f;
            const FVector GroupOrigin = FVector(
                (Col - GroupCols * 0.5f) * GroupSpacingX,
                (Row - GroupRows * 0.5f) * GroupSpacingY,
                0.f);

            const EItemType ItemT = ItemTypes[Rand.RandRange(0, NumItemTypes - 1)];
            GroupItemTypes.Add(ItemT);

            // 矿机
            LogisticsSpawn.Add({
                MinerClass,
                FTransform(FRotator(0, 90, 0), GroupOrigin),
                EBuildingType::Miner
            });
            // 供应塔
            LogisticsSpawn.Add({
                LogisticsTowerClass,
                FTransform(FRotator::ZeroRotator, GroupOrigin + FVector(IntraSpacing, 0.f, 0.f)),
                EBuildingType::LogisticsTower
            });
            // 需求塔
            LogisticsSpawn.Add({
                LogisticsTowerClass,
                FTransform(FRotator::ZeroRotator, GroupOrigin + FVector(IntraSpacing * 2.f, 0.f, 0.f)),
                EBuildingType::LogisticsTower
            });
            // 仓库
            LogisticsSpawn.Add({
                StorageClass,
                FTransform(FRotator(0, 180, 0), GroupOrigin + FVector(IntraSpacing * 3.f, 0.f, 0.f)),
                EBuildingType::Storage
            });
        }
    }

    TArray<FMassEntityHandle> LogisticsEntities = DspManager->BatchSpawnBuildings(LogisticsSpawn);
    if (LogisticsEntities.Num() < NumGroups * BuildingsPerGroup)
    {
        UE_LOG(LogTemp, Error, TEXT("[Logistics] 建筑批量创建失败，期望 %d，实际 %d"),
               NumGroups * BuildingsPerGroup, LogisticsEntities.Num());
        return;
    }

    // 传送带连接 + Fragment 写入
    if (UMassEntitySubsystem* ESub = World->GetSubsystem<UMassEntitySubsystem>())
    {
        FMassEntityManager& EM = ESub->GetMutableEntityManager();

        for (int32 GroupIdx = 0; GroupIdx < NumGroups; ++GroupIdx)
        {
            const int32 Base = GroupIdx * BuildingsPerGroup;
            const FMassEntityHandle MinerEnt = LogisticsEntities[Base + 0];
            const FMassEntityHandle SupplyTowerEnt = LogisticsEntities[Base + 1];
            const FMassEntityHandle DemandTowerEnt = LogisticsEntities[Base + 2];
            const FMassEntityHandle StorageEnt = LogisticsEntities[Base + 3];
            const EItemType ItemT = GroupItemTypes[GroupIdx];

            // 传送带：矿机 → 供应塔；需求塔 → 仓库
            DspManager->CreateAndLinkBeltForSlot(MinerEnt, 0, SupplyTowerEnt, 0, EBeltType::Express);
            DspManager->CreateAndLinkBeltForSlot(DemandTowerEnt, 0, StorageEnt, 0, EBeltType::Express);

            // 矿机：生产指定物品
            if (FMassDspMinerFragment* MF = EM.GetFragmentDataPtr<FMassDspMinerFragment>(MinerEnt))
            {
                MF->StoredItemType = ItemT;
            }

            // 供应塔：Supply 模式
            if (FMassDspLogisticsTowerFragment* SF =
                EM.GetFragmentDataPtr<FMassDspLogisticsTowerFragment>(SupplyTowerEnt))
            {
                SF->TowerMode = ELogisticsTowerMode::Supply;
                SF->ItemType = ItemT;
                SF->RequestThreshold = 50;
                SF->DroneCargoCount = 5;
                SF->ScanInterval = 0.5f;
            }

            // 需求塔：Demand 模式
            if (FMassDspLogisticsTowerFragment* DF =
                EM.GetFragmentDataPtr<FMassDspLogisticsTowerFragment>(DemandTowerEnt))
            {
                DF->TowerMode = ELogisticsTowerMode::Demand;
                DF->ItemType = ItemT;
                DF->RequestThreshold = 20;
                DF->DroneCargoCount = 5;
                DF->ScanInterval = 0.5f;
            }
        }
    }

    DspManager->FlushBeltMesh();

    // ── ISM 宿主 Actor ──────────────────────────────────────────────────────
    if (DroneMesh)
    {
        FActorSpawnParameters ISMHostParams;
        ISMHostParams.Name = TEXT("DroneISMHostActor");
        ISMHostParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        AActor* ISMHost = World->SpawnActor<AActor>(AActor::StaticClass(),
                                                    FVector::ZeroVector, FRotator::ZeroRotator, ISMHostParams);
        check(ISMHost);

        USceneComponent* ISMHostRoot = NewObject<USceneComponent>(ISMHost, TEXT("ISMHostRoot"));
        ISMHost->SetRootComponent(ISMHostRoot);
        ISMHostRoot->RegisterComponent();

        UInstancedStaticMeshComponent* DroneISM =
            NewObject<UInstancedStaticMeshComponent>(ISMHost, TEXT("DroneISMComponent"));
        DroneISM->SetStaticMesh(DroneMesh);
        DroneISM->SetMobility(EComponentMobility::Movable);
        DroneISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        DroneISM->SetCastShadow(false);
        DroneISM->AttachToComponent(ISMHostRoot, FAttachmentTransformRules::KeepRelativeTransform);
        ISMHost->AddInstanceComponent(DroneISM);
        DroneISM->RegisterComponent();
        LogisticsSub->SetupISMComponents(DroneISM, nullptr, nullptr);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[Logistics] DroneMesh 未配置，无人机不会显示"));
    }

    // ── 注册无人机分派策略 ──────────────────────────────────────────────────
    LogisticsSub->RegisterDispatchStrategy(
        ELogisticsDeviceType::Drone,
        MakeUnique<FDroneDispatchStrategy>(LogisticsSub));

    // ── 每个供应塔创建 DronesPerTower 架无人机（环形初始位置）─────────────
    for (int32 GroupIdx = 0; GroupIdx < NumGroups; ++GroupIdx)
    {
        const int32 Base = GroupIdx * BuildingsPerGroup;
        const FMassEntityHandle SupplyTowerEnt = LogisticsEntities[Base + 1];
        const FVector TowerPos = LogisticsSpawn[Base + 1].WorldTransform.GetLocation();

        for (int32 d = 0; d < DronesPerTower; ++d)
        {
            // TODO idle的时候也按螺旋盘旋
            // TODO 请求端也要放无人机
            // const float Angle = (static_cast<float>(d) / DronesPerTower) * 2.f * PI;
            // const FVector InitPos = TowerPos + FVector(FMath::Cos(Angle) * 200.f,
            //                                            FMath::Sin(Angle) * 200.f,
            //                                            100.f + d * 10.f);
            LogisticsSub->CreateDrone(SupplyTowerEnt, TowerPos);
        }
    }

    // ── 每个需求塔同样创建 DronesPerTower 架无人机（主动取货能力）─────────────
    for (int32 GroupIdx = 0; GroupIdx < NumGroups; ++GroupIdx)
    {
        const int32 Base = GroupIdx * BuildingsPerGroup;
        const FMassEntityHandle DemandTowerEnt = LogisticsEntities[Base + 2];
        const FVector DemandPos = LogisticsSpawn[Base + 2].WorldTransform.GetLocation();

        for (int32 d = 0; d < DronesPerTower; ++d)
            LogisticsSub->CreateDrone(DemandTowerEnt, DemandPos);
    }

    UE_LOG(LogTemp, Log,
           TEXT("[Logistics] 100 组物流初始化完成 | %d 供应塔 | %d 需求塔 | %d 架无人机（种子=42）"),
           NumGroups, NumGroups, NumGroups * DronesPerTower * 2);
}
