// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ZoneGraphTypes.h"
#include "MassExecutionContext.h"
#include "MassEntityTemplate.h" // 添加这行
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

    /**
     * 将建筑 Actor 注册到 Mass 系统中创建实体
     * @param BuildingActor 可以在场景中放置的建筑 Actor
     * @return 创建的 Mass 实体句柄
     */
    FMassEntityHandle RegisterBuildingEntity(class AMassDspBuilding* BuildingActor);
    
    /**
     * 基础的创建实体封装 (底层方法)
     * 封装所有 Entity 创建操作，确保集中管理
     */ 
    FMassEntityHandle CreateEntityWithFragments(const struct FMassEntityTemplate& EntityTemplate);
};
