#include "MassDspGameMode.h"
#include "Subsystems/MassDspManager.h"
#include "Actors/MassDspMiner.h"
#include "Actors/MassDspStorage.h"
#include "Fragments/MassDspBuildingFragment.h"
#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "Misc/CoreDelegates.h"

void AMassDspGameMode::BeginPlay()
{
    Super::BeginPlay();

    UWorld* World = GetWorld();
    UMassDspManager* DspManager = World->GetSubsystem<UMassDspManager>();
    if (!DspManager) return;

    // 设置 Manager 的 DefaultItemConfig，以便 Miner 可以生产物品
    if (BeltItemConfigAsset)
    {
        DspManager->DefaultItemConfig = BeltItemConfigAsset;
    }

    if (!MinerClass || !StorageClass)
    {
        UE_LOG(LogTemp, Warning, TEXT("MinerClass or StorageClass not set in GameMode!"));
        return;
    }

    // 1. 创建并注册矿机
    FVector MinerLocation(0, 0, 0);
    AMassDspMiner* MinerActor = World->SpawnActor<AMassDspMiner>(MinerClass, MinerLocation, FRotator::ZeroRotator);
    FMassEntityHandle MinerEntity = DspManager->RegisterBuildingEntity(MinerActor);

    // 2. 创建并注册仓库 (在 X 轴正方向 1000 单位处)
    FVector StorageLocation(1000, 0, 0);
    AMassDspStorage* StorageActor = World->SpawnActor<AMassDspStorage>(StorageClass, StorageLocation, FRotator::ZeroRotator);
    FMassEntityHandle StorageEntity = DspManager->RegisterBuildingEntity(StorageActor);

    // 3. 连接逻辑
    if (MinerActor && StorageActor)
    {
        TArray<FTransform> MinerOutputs = MinerActor->GetSlotTransformsByType(EBuildingSlotType::Output);
        TArray<FTransform> StorageInputs = StorageActor->GetSlotTransformsByType(EBuildingSlotType::Input);

        if (MinerOutputs.Num() > 0 && StorageInputs.Num() > 0)
        {
            FVector StartPoint = MinerOutputs[0].GetLocation();
            FVector EndPoint = StorageInputs[0].GetLocation();

            TArray<FVector> BeltPoints;
            BeltPoints.Add(StartPoint);
            BeltPoints.Add(EndPoint);

            // 创建运行时传送带
            if (FZoneGraphDataHandle BeltHandle = DspManager->CreateRuntimeBelt(BeltPoints, ConveyorMesh); BeltHandle.IsValid())
            {
                FZoneGraphLaneHandle LaneHandle(0, BeltHandle);

                FMassEntityManager& EntityManager = World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();

                // 连接 Miner 的输出槽到传送带起点
                if (FMassDspBuildingSlotsFragment* MinerSlots = EntityManager.GetFragmentDataPtr<FMassDspBuildingSlotsFragment>(MinerEntity))
                {
                    for (auto& Slot : MinerSlots->GetSlots())
                    {
                        if (Slot.Type == EBuildingSlotType::Output)
                        {
                            Slot.ConnectedLaneHandle = LaneHandle;
                            break;
                        }
                    }
                }

                // 连接 Storage 的输入槽到传送带终点
                if (FMassDspBuildingSlotsFragment* StorageSlots = EntityManager.GetFragmentDataPtr<FMassDspBuildingSlotsFragment>(StorageEntity))
                {
                    for (auto& Slot : StorageSlots->GetSlots())
                    {
                        if (Slot.Type == EBuildingSlotType::Input)
                        {
                            Slot.ConnectedLaneHandle = LaneHandle;
                            break;
                        }
                    }
                }
            }
        }
    }
}
