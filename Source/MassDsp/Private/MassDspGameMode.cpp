// Fill out your copyright notice in the Description page of Project Settings.

#include "MassDspGameMode.h"
#include "Subsystems/MassDspManager.h"

void AMassDspGameMode::BeginPlay()
{
    Super::BeginPlay();

    auto* DysonSub = GetWorld()->GetSubsystem<UMassDspManager>();

    TArray<FVector> CurvePoints;
    CurvePoints.Add(FVector(0, 0, 100));
    CurvePoints.Add(FVector(1000, 500, 100));
    CurvePoints.Add(FVector(2000, -500, 100));
    CurvePoints.Add(FVector(5000, 0, 100));

    FZoneGraphDataHandle Handle = DysonSub->CreateRuntimeBelt(CurvePoints, ConveyorMesh);
    DysonSub->SpawnItemsOnBelt(Handle, BeltItemConfigAsset, 10);
}
