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

    constexpr int N = 40;
    constexpr int BuildingsPerGroup = 5; // 每组：3矿机 + 1合成台 + 1仓库

    // 第一步：收集所有Building生成数据
    TArray<FBuildingSpawnData> AllBuildingDataList;
    AllBuildingDataList.Reserve(N * N * BuildingsPerGroup);

    for (int i = 0; i < N; ++i)
    {
        for (int j = 0; j < N; ++j)
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
        DspManager->CreateAndLinkBeltForSlot(MinerEntity1, 0, AssemblerEntity, 2, ConveyorMaterial);
        DspManager->CreateAndLinkBeltForSlot(MinerEntity2, 0, AssemblerEntity, 1, ConveyorMaterial);
        DspManager->CreateAndLinkBeltForSlot(MinerEntity3, 0, AssemblerEntity, 0, ConveyorMaterial);
        DspManager->CreateAndLinkBeltForSlot(AssemblerEntity, 0, StorageEntity, 0, ConveyorMaterial);
    }

    const double BeltLinkElapsed = FPlatformTime::Seconds() - BeltLinkStartTime;
    const double AvgBeltTimeMs = (TotalBelts > 0) ? (BeltLinkElapsed * 1000.0 / TotalBelts) : 0.0;
    UE_LOG(LogTemp, Log, TEXT("[Belt Profile] Total: %d belts | Total time: %.3f ms | Avg per belt: %.4f ms"),
           TotalBelts, BeltLinkElapsed * 1000.0, AvgBeltTimeMs);

    // GameMode的BeginPlay里，所有传送带创建完毕后：
    DspManager->FlushBeltMesh(ConveyorMaterial);
}

void AMassDspGameMode::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    ProcessConveyor(DeltaTime);

    // 记录帧时间
    FrameTimeHistory.Add(DeltaTime);
    if (FrameTimeHistory.Num() > MaxHistorySize)
    {
        FrameTimeHistory.RemoveAt(0);
    }

    // 定期更新统计
    TimeSinceLastUpdate += DeltaTime;
    if (TimeSinceLastUpdate >= StatUpdateInterval)
    {
        UpdateFrameStats();
        TimeSinceLastUpdate = 0.0f;
    }

    // 显示统计信息
    if (GEngine)
    {
        float CurrentFPS = 1.0f / DeltaTime;
        GEngine->AddOnScreenDebugMessage(
            INDEX_NONE,
            0.0f,
            FColor::Yellow,
            FString::Printf(TEXT("Current: %.1f FPS | Avg: %.1f FPS | 1%% Low: %.1f FPS"),
                            CurrentFPS, AverageFPS, OnePercentLowFPS),
            true,
            FVector2D(1.5f, 1.5f)
        );
    }
}

void AMassDspGameMode::ProcessConveyor(float DeltaTime) const
{
    UMassDspManager* Manager = GetWorld()->GetSubsystem<UMassDspManager>();
    if (!Manager || Manager->BeltEntityRegistry.IsEmpty()) return;

    // --- Step 1: 缓存所有 Belt 指针，避免 ParallelFor 里访问 TMap ---
    TArray<FBeltHandle> ActiveBelts;
    Manager->BeltEntityRegistry.GetKeys(ActiveBelts);

    TArray<FBeltData*> BeltDataPtrs;
    BeltDataPtrs.Reserve(ActiveBelts.Num());
    for (const FBeltHandle& Handle : ActiveBelts)
    {
        BeltDataPtrs.Add(Manager->BeltEntityRegistry.Find(Handle));
    }

    // --- Step 2: 并行更新各传送带物品位置（纯连续 TArray，无随机内存访问）---
    //for (int BeltIdx = 0; BeltIdx < BeltDataPtrs.Num(); BeltIdx++)
    ParallelFor(BeltDataPtrs.Num(), [&](int BeltIdx)
    {
        FBeltData* BeltData = BeltDataPtrs[BeltIdx];
        if (!BeltData || BeltData->ItemCache.IsEmpty()) return;

        const FBeltHandle& Handle = ActiveBelts[BeltIdx];
        if (!Manager->BeltTrajectories.IsValidIndex(Handle.Index)) return;

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
    if (Manager->SyncAccum >= 1.0f / 30.0f)
    {
        Manager->SyncAccum = 0.f;

        // 构建当前帧视锥体（ViewProjectionMatrix → FConvexVolume）
        FConvexVolume ViewFrustum;
        APlayerController* PC = GetWorld()->GetFirstPlayerController();
        ULocalPlayer* LP = PC ? PC->GetLocalPlayer() : nullptr;
        if (LP && LP->ViewportClient && LP->ViewportClient->Viewport)
        {
            FSceneViewProjectionData ProjData;
            if (LP->GetProjectionData(LP->ViewportClient->Viewport, ProjData))
            {
                GetViewFrustumBounds(ViewFrustum, ProjData.ComputeViewProjectionMatrix(),
                                     /*bUseNearPlane=*/true);
            }
        }
        Manager->UpdateAllBeltItemTransforms(ViewFrustum);
    }
}

void AMassDspGameMode::UpdateFrameStats()
{
    if (FrameTimeHistory.Num() < 10) return;

    // 计算平均FPS
    float TotalFrameTime = 0.0f;
    for (float FrameTime : FrameTimeHistory)
    {
        TotalFrameTime += FrameTime;
    }
    AverageFPS = FrameTimeHistory.Num() / TotalFrameTime;

    // 计算1% Low FPS
    TArray<float> SortedFrameTimes = FrameTimeHistory;
    SortedFrameTimes.Sort([](float A, float B) { return A > B; }); // 降序排序

    int32 OnePercentCount = FMath::Max(1, FMath::CeilToInt(SortedFrameTimes.Num() * 0.01f));
    float OnePercentSum = 0.0f;
    for (int32 i = 0; i < OnePercentCount; ++i)
    {
        OnePercentSum += SortedFrameTimes[i];
    }
    OnePercentLowFPS = OnePercentCount / OnePercentSum;
}
