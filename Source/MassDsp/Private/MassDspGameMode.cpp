#include "MassDspGameMode.h"

#include "MassEntitySubsystem.h"

#include "Subsystems/MassDspManager.h"

#include "Actors/MassDspMiner.h"
#include "Actors/MassDspStorage.h"
#include "Actors/MassDspAssembler.h"

#include "Misc/CoreDelegates.h"

void AMassDspGameMode::BeginPlay()
{
    Super::BeginPlay();

    UWorld* World = GetWorld();
    UMassDspManager* DspManager = World->GetSubsystem<UMassDspManager>();
    if (!DspManager) return;

    if (!MinerClass || !StorageClass || !AssemblerClass)
    {
        UE_LOG(LogTemp, Warning, TEXT("Building Classes not set in GameMode!"));
        return;
    }

    // ===== 新方案：批量创建Building Entity（无Actor实例化）=====

    // 构建Building生成数据列表
    TArray<FBuildingSpawnData> BuildingDataList;

    // 3个矿机
    BuildingDataList.Add(FBuildingSpawnData(
        MinerClass,
        FTransform(FRotator(0, 90, 0), FVector(0, 0, 0)),
        EBuildingType::Miner
    ));
    BuildingDataList.Add(FBuildingSpawnData(
        MinerClass,
        FTransform(FRotator(0, 90, 0), FVector(1000, 0, 0)),
        EBuildingType::Miner
    ));
    BuildingDataList.Add(FBuildingSpawnData(
        MinerClass,
        FTransform(FRotator(0, 90, 0), FVector(2000, 0, 0)),
        EBuildingType::Miner
    ));

    // 1个合成台
    BuildingDataList.Add(FBuildingSpawnData(
        AssemblerClass,
        FTransform(FRotator(0, 0, 0), FVector(1000, 1000, 0)),
        EBuildingType::Assembler
    ));

    // 1个仓库
    BuildingDataList.Add(FBuildingSpawnData(
        StorageClass,
        FTransform(FRotator(0, 180, 0), FVector(1000, 2000, 0)),
        EBuildingType::Storage
    ));

    // 批量创建所有Building Entity
    TArray<FMassEntityHandle> CreatedBuildings = DspManager->BatchSpawnBuildings(BuildingDataList);

    if (CreatedBuildings.Num() != 5)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create all building entities! Expected 5, got %d"), CreatedBuildings.Num());
        return;
    }

    // 提取EntityHandle用于连接传送带
    FMassEntityHandle MinerEntity1 = CreatedBuildings[0];
    FMassEntityHandle MinerEntity2 = CreatedBuildings[1];
    FMassEntityHandle MinerEntity3 = CreatedBuildings[2];
    FMassEntityHandle AssemblerEntity = CreatedBuildings[3];
    FMassEntityHandle StorageEntity = CreatedBuildings[4];

    // 创建传送带连接（与之前逻辑相同）
    DspManager->CreateAndLinkBeltForSlot(MinerEntity1, 0, AssemblerEntity, 2, ConveyorMesh);
    DspManager->CreateAndLinkBeltForSlot(MinerEntity2, 0, AssemblerEntity, 1, ConveyorMesh);
    DspManager->CreateAndLinkBeltForSlot(MinerEntity3, 0, AssemblerEntity, 0, ConveyorMesh);
    DspManager->CreateAndLinkBeltForSlot(AssemblerEntity, 0, StorageEntity, 0, ConveyorMesh);

    UE_LOG(LogTemp, Log, TEXT("Successfully initialized MassDsp factory: 3 Miners + 1 Assembler + 1 Storage (Pure ECS Mode)"));
}
