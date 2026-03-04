#include "MassDspGameMode.h"

#include "Subsystems/MassDspManager.h"
#include "Subsystems/MassDspLogisticsSubsystem.h"

#include "Actors/MassDspMiner.h"
#include "Actors/MassDspStorage.h"
#include "Actors/MassDspAssembler.h"
#include "Actors/MassDspLogisticsTower.h"

#include "Logistics/MassDspDroneStrategy.h"
#include "Fragments/MassDspLogisticsTowerFragment.h"

#include "MassEntitySubsystem.h"
#include "Engine/LocalPlayer.h"

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
    constexpr int BeltsPerGroup = 4; // 每组传送带数量
    constexpr int TotalBelts = N * N * BeltsPerGroup;

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

    auto Time3 = FPlatformTime::Seconds();

    const double BeltLinkElapsed = Time3 - Time2;
    const double AvgBeltTimeMs = (TotalBelts > 0) ? (BeltLinkElapsed * 1000.0 / TotalBelts) : 0.0;
    UE_LOG(LogTemp, Log, TEXT("[Belt Profile] Total: %d belts | Total time: %.3f ms | Avg per belt: %.4f ms"),
           TotalBelts, BeltLinkElapsed * 1000.0, AvgBeltTimeMs);

    if (this) return;

    // =====================================================================
    // 物流演示场景（DSP 风格：塔角色配置 → 系统全自动调度）
    //
    //   矿机 A1/A2/A3  ──快速传送带──▶  物流塔 B（供应协调塔）
    //                                         │  30 架无人机
    //                                         ▼
    //                                  物流塔 C（需求方，CoordinatorTower = B）
    //                                         │
    //                                  仓库 D（消端）
    //
    //   Processor 每 0.5s 扫描一次：
    //     · 塔 B 自身库存 > 5%  → 提交 Supply(TowerB) 到 TowerB 队列
    //     · 塔 C DesiredItemType=IronOre, 库存 < 95%
    //           → 提交 Demand(TowerC) 也路由到 TowerB 队列
    //     · MatchPendingRequests 在 TowerB 队列内找到 Supply+Demand 配对
    //       → 立即派遣就近空闲无人机
    // =====================================================================
    UMassDspLogisticsSubsystem* LogisticsSub =
        World->GetSubsystem<UMassDspLogisticsSubsystem>();

    if (!LogisticsSub || !LogisticsTowerClass)
    {
        UE_LOG(LogTemp, Warning,
               TEXT("物流演示跳过：LogisticsSubsystem 或 LogisticsTowerClass 为空"));
        return;
    }

    // ── 布局（XY 平面，Z=0）──
    // A1/A2/A3：三矿机在 TowerB 西侧排列，保证 TowerB 存货充沛
    // TowerB/C 的覆盖半径默认 2000 cm；B→C 间距 3500 cm，故需扩大半径
    const FVector TowerBPos = FVector(0.f, 0.f, 0.f);
    const FVector TowerCPos = FVector(0.f, 4000.f, 0.f); // 离 B 4000 cm，有飞行视觉感
    const FVector MinerA1Pos = FVector(-2000.f, -800.f, 0.f);
    const FVector MinerA2Pos = FVector(-2000.f, 0.f, 0.f);
    const FVector MinerA3Pos = FVector(-2000.f, 800.f, 0.f);
    const FVector StorageDPos = FVector(0.f, 7000.f, 0.f); // 消端：TowerC 下游

    TArray<FBuildingSpawnData> DemoSpawn;
    DemoSpawn.Add({MinerClass, FTransform(FRotator(0, 90, 0), MinerA1Pos), EBuildingType::Miner});
    DemoSpawn.Add({MinerClass, FTransform(FRotator(0, 90, 0), MinerA2Pos), EBuildingType::Miner});
    DemoSpawn.Add({MinerClass, FTransform(FRotator(0, 90, 0), MinerA3Pos), EBuildingType::Miner});
    DemoSpawn.Add({LogisticsTowerClass, FTransform(FRotator::ZeroRotator, TowerBPos), EBuildingType::LogisticsTower});
    DemoSpawn.Add({LogisticsTowerClass, FTransform(FRotator::ZeroRotator, TowerCPos), EBuildingType::LogisticsTower});
    DemoSpawn.Add({StorageClass, FTransform(FRotator::ZeroRotator, StorageDPos), EBuildingType::Storage});

    TArray<FMassEntityHandle> DemoEntities = DspManager->BatchSpawnBuildings(DemoSpawn);
    if (DemoEntities.Num() < 6)
    {
        UE_LOG(LogTemp, Error, TEXT("物流演示建筑创建失败"));
        return;
    }

    const FMassEntityHandle MinerA1 = DemoEntities[0];
    const FMassEntityHandle MinerA2 = DemoEntities[1];
    const FMassEntityHandle MinerA3 = DemoEntities[2];
    const FMassEntityHandle TowerB = DemoEntities[3];
    const FMassEntityHandle TowerC = DemoEntities[4];
    const FMassEntityHandle StorageD = DemoEntities[5];

    // ── Fragment 配置（在 EntityManager 上直接写，无需 Actor）──
    if (UMassEntitySubsystem* ESub = World->GetSubsystem<UMassEntitySubsystem>())
    {
        FMassEntityManager& EM = ESub->GetMutableEntityManager();

        // 塔 B：纯供应协调方
        //   · SupplyTriggerRatio=0.05 → 库存超过 5% 立即发 Supply（近乎总是发）
        //   · ScanInterval=0.5s → 高频扫描
        //   · CoverageRadius=6000 → 覆盖全演示场景
        if (FMassDspLogisticsTowerFragment* BFrag =
            EM.GetFragmentDataPtr<FMassDspLogisticsTowerFragment>(TowerB))
        {
            BFrag->SupplyTriggerRatio = 0.05f;
            BFrag->ScanInterval = 0.5f;
            BFrag->CoverageRadius = 6000.f;
        }

        // 塔 C：需求消费方
        //   · DesiredItemType=IronOre → Processor 扫描时自动提交 Demand
        //   · DemandTriggerRatio=0.95 → 库存低于 95% 就补货（几乎总是请求）
        //   · CoordinatorTowerEntity=TowerB → Demand 路由到 TowerB 队列与 Supply 配对
        if (FMassDspLogisticsTowerFragment* CFrag =
            EM.GetFragmentDataPtr<FMassDspLogisticsTowerFragment>(TowerC))
        {
            CFrag->DesiredItemType = EItemType::IronOre;
            CFrag->DemandTriggerRatio = 0.95f;
            CFrag->ScanInterval = 0.5f;
            CFrag->CoordinatorTowerEntity = TowerB; // 关键：路由到 B 的队列
        }
    }

    // ── 传送带：3 矿机 → TowerB；TowerC → StorageD ──
    DspManager->CreateAndLinkBeltForSlot(MinerA1, 0, TowerB, 0, EBeltType::Express);
    DspManager->CreateAndLinkBeltForSlot(MinerA2, 0, TowerB, 1, EBeltType::Express);
    DspManager->CreateAndLinkBeltForSlot(MinerA3, 0, TowerB, 2, EBeltType::Express);
    DspManager->CreateAndLinkBeltForSlot(TowerC, 0, StorageD, 0, EBeltType::Express);
    DspManager->FlushBeltMesh();

    // ── ISM 宿主 Actor（AGameModeBase 无 RootComponent，必须单独 spawn）──
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

        UE_LOG(LogTemp, Log, TEXT("[Logistics] DroneISM created, Mesh=%s"), *DroneMesh->GetName());
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[Logistics] DroneMesh 未配置，无人机不会显示"));
    }

    // ── 注册无人机分派策略 ──
    LogisticsSub->RegisterDispatchStrategy(
        ELogisticsDeviceType::Drone,
        MakeUnique<FDroneDispatchStrategy>(LogisticsSub));

    // ── 创建 30 架无人机，全部归属 TowerB（供应侧持有无人机池）──
    // 初始停靠位置均匀散布在 TowerB 附近，避免首帧全部挤在同一点
    constexpr int32 TotalDrones = 30;
    for (int32 i = 0; i < TotalDrones; ++i)
    {
        // 在 TowerB 周围半径 200 cm 内环形分布初始悬停点
        const float Angle = (static_cast<float>(i) / TotalDrones) * 2.f * PI;
        const float Spread = 200.f;
        const FVector InitPos = TowerBPos + FVector(FMath::Cos(Angle) * Spread,
                                                    FMath::Sin(Angle) * Spread,
                                                    100.f + i * 5.f);
        // 直接将 InitPos 传入 CreateDrone，P0~P3 与 ISM 一步到位，避免首次起飞位置跳变
        LogisticsSub->CreateDrone(TowerB, InitPos);
    }

    UE_LOG(LogTemp, Log,
           TEXT("[Logistics Demo] 初始化完成 | TowerB[%d,%d] TowerC[%d,%d] | %d 架无人机"
               " | 3 矿机持续补货 | Processor 0.5s/次全自动调度"),
           TowerB.Index, TowerB.SerialNumber,
           TowerC.Index, TowerC.SerialNumber,
           TotalDrones);
}

void AMassDspGameMode::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    ProcessConveyor(DeltaTime);
}

void AMassDspGameMode::ProcessConveyor(float DeltaTime) const
{
    auto BeginTime = FPlatformTime::Seconds();

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

    // --- Step 2: 严格 O(1) 每条传送带更新：刚体阻塞组 + 自由区双区模型 ---
    ParallelFor(BeltDataArray.Num(), [&](int BeltIdx)
    {
        FBeltData* BeltData = BeltDataArray[BeltIdx];
        if (!BeltData || BeltData->ItemCache.IsEmpty()) return;

        // 严格 O(1)：全带唯一操作
        BeltData->TotalMove += BeltData->BeltSpeed * DeltaTime;

        // 摊还 O(1)：检查前沿自由物品是否追上阻塞组组尾（或首次到达出口），追上则合并入组
        const int32 N = BeltData->ItemCache.Num();
        if (BeltData->BlockedCount < N)
        {
            // 组尾位置：BlockedCount==0 时视为出口线
            const float BackOfGroup = (BeltData->BlockedCount > 0)
                ? BeltData->GetGroupFront()
                  - static_cast<float>(BeltData->BlockedCount) * FGameConst::ItemSpace
                : BeltData->BeltLength - FGameConst::HalfLength;

            const float FrontFreePos =
                BeltData->ItemCache[BeltData->BlockedCount].Offset + BeltData->TotalMove;

            if (FrontFreePos >= BackOfGroup)
            {
                if (BeltData->BlockedCount == 0)
                {
                    // 首个物品到达出口：初始化组头锁定在出口线
                    BeltData->GroupFrontOffset =
                        (BeltData->BeltLength - FGameConst::HalfLength) - BeltData->TotalMove;
                }
                ++BeltData->BlockedCount;
            }
        }

        // 防 float 精度退化（约 41 分钟触发一次）：
        // 自由物品 Offset 与 GroupFrontOffset 均需更新，以维持和 TotalMove 的相对精度。
        static constexpr float RebaseThreshold = 1e6f;
        if (BeltData->TotalMove > RebaseThreshold)
        {
            for (int32 i = BeltData->BlockedCount; i < N; ++i) // 只更新自由物品
                BeltData->ItemCache[i].Offset += BeltData->TotalMove;
            BeltData->GroupFrontOffset += BeltData->TotalMove; // 组头同步
            BeltData->TotalMove = 0.f;
        }
    });

    auto EndTime = FPlatformTime::Seconds();

    const double ElapsedMs = (EndTime - BeginTime) * 1000.0;
    UE_LOG(LogTemp, Log, TEXT("[Tick Profile] Conveyor Update Time: %.3f ms"), ElapsedMs);

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
