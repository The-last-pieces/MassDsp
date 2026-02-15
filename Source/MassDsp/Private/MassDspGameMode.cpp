// Fill out your copyright notice in the Description page of Project Settings.

#include "MassDspGameMode.h"
#include "Subsystems/MassDspManager.h"
#include "Actors/MassDspMiner.h"
#include "Actors/MassDspStorage.h"

void AMassDspGameMode::BeginPlay()
{
    Super::BeginPlay();

    UMassDspManager* DspManager = GetWorld()->GetSubsystem<UMassDspManager>();
    if (!DspManager) return;

    if (!MinerClass || !StorageClass)
    {
        UE_LOG(LogTemp, Warning, TEXT("MinerClass or StorageClass not set in GameMode!"));
        return;
    }

    // 1. 创建矿机
    FVector MinerLocation(0, 0, 0);
    // 这里使用 GetWorld()->SpawnActor 而不是直接 CreateEntity，
    // 因为我们需要 Actor 来提供变换和 Mesh，而且 MassDspBuilding 会自动注册自己
    AMassDspMiner* MinerActor = GetWorld()->SpawnActor<AMassDspMiner>(MinerClass, MinerLocation, FRotator::ZeroRotator);

    // 2. 创建仓库 (在 X 轴正方向 2000 单位处)
    FVector StorageLocation(2000, 0, 0);
    AMassDspStorage* StorageActor = GetWorld()->SpawnActor<AMassDspStorage>(StorageClass, StorageLocation, FRotator::ZeroRotator);

    // 3. 连接传送带
    if (MinerActor && StorageActor)
    {
        // 确保 Actor 初始化完成，获取槽口位置
        // 注意：在同一帧 Spawn 后立即获取 Slots 可能需要强制更新 Transforms 或直接计算
        // 这里基于我们已知的设计，GetSlotTransformsByType 使用的是 Slot.LocalTransform * ActorTransform，这是安全的
        
        TArray<FTransform> MinerOutputs = MinerActor->GetSlotTransformsByType(EBuildingSlotType::Output);
        TArray<FTransform> StorageInputs = StorageActor->GetSlotTransformsByType(EBuildingSlotType::Input);

        if (MinerOutputs.Num() > 0 && StorageInputs.Num() > 0)
        {
            FVector StartPoint = MinerOutputs[0].GetLocation();
            FVector EndPoint = StorageInputs[0].GetLocation();
            
            // 为了美观，添加一些控制点让传送带有些弧度，或者是直连
            TArray<FVector> BeltPoints;
            BeltPoints.Add(StartPoint);
            
            // 简单的直线插值点
            BeltPoints.Add(FMath::Lerp(StartPoint, EndPoint, 0.33f));
            BeltPoints.Add(FMath::Lerp(StartPoint, EndPoint, 0.66f));
            
            BeltPoints.Add(EndPoint);

            FZoneGraphDataHandle BeltHandle = DspManager->CreateRuntimeBelt(BeltPoints, ConveyorMesh);

            // 4. (测试用) 在传送带上生成一些物品，模拟矿机产出
            if (BeltItemConfigAsset)
            {
                DspManager->SpawnItemsOnBelt(BeltHandle, BeltItemConfigAsset);
            }
        }
    }
}
