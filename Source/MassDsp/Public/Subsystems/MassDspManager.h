// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ZoneGraphTypes.h"
#include "MassExecutionContext.h"
#include "MassDspManager.generated.h"

// 为了能在 TMap 中使用，包装一下数组
USTRUCT()
struct FBeltEntityArray
{
    GENERATED_BODY()
    UPROPERTY()
    TArray<FMassEntityHandle> Entities;
};

UCLASS()
class MASSDSP_API UMassDspManager : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    // 创建传送带逻辑
    FZoneGraphDataHandle CreateRuntimeBelt(const TArray<FVector>& ControlPoints, UStaticMesh* BeltMesh, int32 SegmentsPerSection = 10);

    // 孵化逻辑
    bool SpawnItemsOnBelt(FZoneGraphDataHandle DataHandle, class UMassEntityConfigAsset* ItemConfig);

    // 存储车道实体的注册表
    UPROPERTY()
    TMap<FZoneGraphLaneHandle, FBeltEntityArray> LaneRegistry;
};
