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

    constexpr int N = 2;
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

    // =====================================================================
    // 物流演示场景
    //   矿机 A  传送带  物流塔 B（供应方）
    //   物流塔 B ↔ 无人机 ↔ 物流塔 C（需求方）
    //   B、C 各挂匍 10 架无人机
    // =====================================================================
    UMassDspLogisticsSubsystem* LogisticsSub =
        World->GetSubsystem<UMassDspLogisticsSubsystem>();

    if (!LogisticsSub || !LogisticsTowerClass)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("物流演示跨过：LogisticsSubsystem 或 LogisticsTowerClass 为空"));
        return;
    }

    // ── 布局：A  B（供应）  C（需求）──
    // B、C 相距 1500 cm，均在 B 的默认覆盖半径（ 2000 cm）内
    const FVector MinerAPos = FVector(-5000.f, 5000.f, 0.f);
    const FVector TowerBPos = FVector(-3000.f, 5000.f, 0.f);
    const FVector TowerCPos = FVector(-3000.f, 6500.f, 0.f); // 离 B 1500 cm

    TArray<FBuildingSpawnData> DemoSpawnData;
    DemoSpawnData.Add(FBuildingSpawnData(
        MinerClass,
        FTransform(FRotator(0.f, 90.f, 0.f), MinerAPos),
        EBuildingType::Miner));
    DemoSpawnData.Add(FBuildingSpawnData(
        LogisticsTowerClass,
        FTransform(FRotator::ZeroRotator, TowerBPos),
        EBuildingType::LogisticsTower));
    DemoSpawnData.Add(FBuildingSpawnData(
        LogisticsTowerClass,
        FTransform(FRotator::ZeroRotator, TowerCPos),
        EBuildingType::LogisticsTower));

    TArray<FMassEntityHandle> DemoEntities = DspManager->BatchSpawnBuildings(DemoSpawnData);
    if (DemoEntities.Num() < 3 || !DemoEntities[0].IsValid() ||
        !DemoEntities[1].IsValid() || !DemoEntities[2].IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("物流演示建筑创建失败"));
        return;
    }

    const FMassEntityHandle MinerA  = DemoEntities[0];
    const FMassEntityHandle TowerB  = DemoEntities[1];
    const FMassEntityHandle TowerC  = DemoEntities[2];

    // ── 直接修改 Tower C 的 Fragment：设为消费方（持续请求 IronOre）──
    // Tower B 默认 DesiredItemType=None，当库存 > 80% 时才发 Supply（由 Processor 扫描触发）
    if (UMassEntitySubsystem* ESub = World->GetSubsystem<UMassEntitySubsystem>())
    {
        FMassEntityManager& EM = ESub->GetMutableEntityManager();

        // Tower B：纯供应方，ScanInterval 缩短以便快速响应
        if (FMassDspLogisticsTowerFragment* BFrag =
                EM.GetFragmentDataPtr<FMassDspLogisticsTowerFragment>(TowerB))
        {
            BFrag->SupplyTriggerRatio = 0.1f;  // 库存超 10% 即发 Supply（演示用低阈值）
            BFrag->ScanInterval       = 1.f;   // 每秒扫描一次（默认 3s，演示加快）
        }

        // Tower C：消费方，持续请求 IronOre
        if (FMassDspLogisticsTowerFragment* CFrag =
                EM.GetFragmentDataPtr<FMassDspLogisticsTowerFragment>(TowerC))
        {
            CFrag->DesiredItemType  = EItemType::IronOre;
            CFrag->DemandTriggerRatio = 0.9f; // 库存低于 90% 就请求补货（演示用高阈值保持频繁）
            CFrag->ScanInterval     = 1.f;
        }
    }

    // ── 传送带：矿机 A 输出（slot 0）→ 物流塔 B 输入（slot 0）──
    DspManager->CreateAndLinkBeltForSlot(MinerA, 0, TowerB, 0, EBeltType::Express);
    DspManager->FlushBeltMesh();

    // ── ISM：如果配置了 DroneMesh，在 GameMode Actor 上动态创建 ISM 组件──
    if (DroneMesh)
    {
        UInstancedStaticMeshComponent* DroneISM =
            NewObject<UInstancedStaticMeshComponent>(this, TEXT("DroneISMComponent"));
        DroneISM->SetStaticMesh(DroneMesh);
        DroneISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        DroneISM->SetCastShadow(false);
        DroneISM->RegisterComponent();
        LogisticsSub->SetupISMComponents(DroneISM, nullptr, nullptr);
    }

    // ── 注册无人机分派策略（全局注册一次）──
    LogisticsSub->RegisterDispatchStrategy(
        ELogisticsDeviceType::Drone,
        MakeUnique<FDroneDispatchStrategy>(LogisticsSub));

    // ── 创建无人机：B、C 各 10 架，初始位置设为堵位塔 P3──
    constexpr int32 DronesPerTower = 10;

    for (int32 i = 0; i < DronesPerTower; ++i)
    {
        // 加入 B 的无人机池
        FDroneHandle HB = LogisticsSub->CreateDrone(TowerB);
        if (HB.IsValid() && LogisticsSub->DronePool.IsValidIndex(HB.Index))
        {
            // 初始悉停位置 = 塔 B（第一个任务分配前 P3 就是起飞点）
            LogisticsSub->DronePool[HB.Index].P3 = TowerBPos;
        }

        // 加入 C 的无人机池
        FDroneHandle HC = LogisticsSub->CreateDrone(TowerC);
        if (HC.IsValid() && LogisticsSub->DronePool.IsValidIndex(HC.Index))
        {
            LogisticsSub->DronePool[HC.Index].P3 = TowerCPos;
        }
    }

    // ── 在 GameMode Actor 上加一个 Timer，每 2秒重新提交一对请求（去重保证始终有 Supply+Demand 匹配）──
    struct FDemoReqHelper
    {
        static void SubmitPair(
            UMassDspLogisticsSubsystem* Sub,
            FMassEntityHandle SrcSupply,
            FMassEntityHandle SrcDemand,
            FMassEntityHandle CoordTower)
        {
            Sub->SubmitSupplyRequest(SrcSupply, EItemType::IronOre, 5,
                ELogisticsRequestPriority::Normal, CoordTower);
            Sub->SubmitDemandRequest(SrcDemand, EItemType::IronOre, 5,
                ELogisticsRequestPriority::Normal, CoordTower);
        }
    };

    // 立即提交一次
    FDemoReqHelper::SubmitPair(LogisticsSub, TowerB, TowerC, TowerB);

    // 每 2 秒持续提交（容容子，去重逻辑在子系统内处理）
    // 使用 lambda 封装回调拷贝副本
    FMassEntityHandle CapB = TowerB, CapC = TowerC;
    TWeakObjectPtr<UMassDspLogisticsSubsystem> WeakSub = LogisticsSub;
    GetWorld()->GetTimerManager().SetTimer(
        DemoRequestTimer,
        [WeakSub, CapB, CapC]()
        {
            if (UMassDspLogisticsSubsystem* S = WeakSub.Get())
                FDemoReqHelper::SubmitPair(S, CapB, CapC, CapB);
        },
        2.f, /*bLoop=*/true);

    UE_LOG(LogTemp, Log,
        TEXT("物流演示初始化完成：矿机 A  塔 B[%d,%d]  塔 C[%d,%d] | 各 %d 架无人机 | Timer 2s/帧"),
        TowerB.Index, TowerB.SerialNumber,
        TowerC.Index, TowerC.SerialNumber, DronesPerTower);
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
