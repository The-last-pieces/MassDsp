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

    FVector MinerLocation(0, 0, 0);
    AMassDspMiner* MinerActor = World->SpawnActor<AMassDspMiner>(MinerClass, MinerLocation, FRotator::ZeroRotator);

    FVector StorageLocation(2000, 0, 0);
    AMassDspStorage* StorageActor = World->SpawnActor<AMassDspStorage>(StorageClass, StorageLocation, FRotator::ZeroRotator);

    DspManager->CreateAndLinkBeltForSlot(MinerActor, 0, StorageActor, 0, ConveyorMesh);
}
