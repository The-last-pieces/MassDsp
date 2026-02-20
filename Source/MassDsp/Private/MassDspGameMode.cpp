#include "MassDspGameMode.h"
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

    if (!MinerClass || !StorageClass)
    {
        UE_LOG(LogTemp, Warning, TEXT("MinerClass or StorageClass not set in GameMode!"));
        return;
    }

    // 3个矿机 + 1个合成器 + 1个仓库的简单测试场景

    FVector MinerLocation1(0, 0, 0);
    AMassDspMiner* MinerActor1 = World->SpawnActor<AMassDspMiner>(MinerClass, MinerLocation1, FRotator(0, 90, 0));

    FVector MinerLocation2(1000, 0, 0);
    AMassDspMiner* MinerActor2 = World->SpawnActor<AMassDspMiner>(MinerClass, MinerLocation2, FRotator(0, 90, 0));

    FVector MinerLocation3(2000, 0, 0);
    AMassDspMiner* MinerActor3 = World->SpawnActor<AMassDspMiner>(MinerClass, MinerLocation3, FRotator(0, 90, 0));

    FVector AssemblerLocation(1000, 1000, 0);
    AMassDspAssembler* AssemblerActor = World->SpawnActor<AMassDspAssembler>(AssemblerClass, AssemblerLocation, FRotator(0, 0, 0));

    FVector StorageLocation(1000, 2000, 0);
    AMassDspStorage* StorageActor = World->SpawnActor<AMassDspStorage>(StorageClass, StorageLocation, FRotator(0, 180, 0));

    DspManager->CreateAndLinkBeltForSlot(MinerActor1, 0, AssemblerActor, 2, ConveyorMesh);
    DspManager->CreateAndLinkBeltForSlot(MinerActor2, 0, AssemblerActor, 1, ConveyorMesh);
    DspManager->CreateAndLinkBeltForSlot(MinerActor3, 0, AssemblerActor, 0, ConveyorMesh);

    DspManager->CreateAndLinkBeltForSlot(AssemblerActor, 0, StorageActor, 0, ConveyorMesh);
}
