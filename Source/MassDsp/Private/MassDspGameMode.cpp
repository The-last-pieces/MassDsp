#include "MassDspGameMode.h"

#include "Subsystems/MassDspManager.h"

#include "Actors/MassDspMiner.h"
#include "Actors/MassDspStorage.h"
#include "Actors/MassDspAssembler.h"

#include "Misc/CoreDelegates.h"

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

    for (int i = 0; i < 22; ++i)
    {
        for (int j = 0; j < 22; ++j)
        {
            constexpr int GridSize = 4000;
            FVector SpawnLocation = FVector(i * GridSize, j * GridSize, 0);

            // 构建Building生成数据列表
            TArray<FBuildingSpawnData> BuildingDataList;

            // 3个矿机
            BuildingDataList.Add(FBuildingSpawnData(
                MinerClass,
                FTransform(FRotator(0, 90, 0), SpawnLocation + FVector(0, 0, 0)),
                EBuildingType::Miner
            ));
            BuildingDataList.Add(FBuildingSpawnData(
                MinerClass,
                FTransform(FRotator(0, 90, 0), SpawnLocation + FVector(1000, 0, 0)),
                EBuildingType::Miner
            ));
            BuildingDataList.Add(FBuildingSpawnData(
                MinerClass,
                FTransform(FRotator(0, 90, 0), SpawnLocation + FVector(2000, 0, 0)),
                EBuildingType::Miner
            ));

            // 1个合成台
            BuildingDataList.Add(FBuildingSpawnData(
                AssemblerClass,
                FTransform(FRotator(0, 0, 0), SpawnLocation + FVector(1000, 1000, 0)),
                EBuildingType::Assembler
            ));

            // 1个仓库
            BuildingDataList.Add(FBuildingSpawnData(
                StorageClass,
                FTransform(FRotator(0, 180, 0), SpawnLocation + FVector(1000, 2000, 0)),
                EBuildingType::Storage
            ));

            // 批量创建所有Building Entity
            TArray<FMassEntityHandle> CreatedBuildings = DspManager->BatchSpawnBuildings(BuildingDataList);

            // 提取EntityHandle用于连接传送带
            FMassEntityHandle MinerEntity1 = CreatedBuildings[0];
            FMassEntityHandle MinerEntity2 = CreatedBuildings[1];
            FMassEntityHandle MinerEntity3 = CreatedBuildings[2];
            FMassEntityHandle AssemblerEntity = CreatedBuildings[3];
            FMassEntityHandle StorageEntity = CreatedBuildings[4];

            // 创建传送带连接
            DspManager->CreateAndLinkBeltForSlot(MinerEntity1, 0, AssemblerEntity, 2, ConveyorMaterial);
            DspManager->CreateAndLinkBeltForSlot(MinerEntity2, 0, AssemblerEntity, 1, ConveyorMaterial);
            DspManager->CreateAndLinkBeltForSlot(MinerEntity3, 0, AssemblerEntity, 0, ConveyorMaterial);
            DspManager->CreateAndLinkBeltForSlot(AssemblerEntity, 0, StorageEntity, 0, ConveyorMaterial);
        }
    }
}

void AMassDspGameMode::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

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
