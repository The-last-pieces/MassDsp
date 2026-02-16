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

    /**
     * 尝试在指定车道的特定距离生成一个物品
     * @param LaneHandle 车道句柄
     * @param Distance 生成距离
     * @return 是否生成成功
     */
    bool SpawnItemOnLane(FZoneGraphLaneHandle LaneHandle, float Distance, FMassCommandBuffer& CommandBuffer);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
    TObjectPtr<UMassEntityConfigAsset> DefaultItemConfig;

    /**
     * 尝试从指定车道的末端消耗一个物品
     * @param LaneHandle 车道句柄
     * @return 是否成功消耗
     */
    bool ConsumeItemFromLane(FZoneGraphLaneHandle LaneHandle, FMassCommandBuffer& CommandBuffer);

    /**
     * 建立槽口与车道的连接关系 (寻找并在 Slot 中缓存 LaneHandle)
     * @param SlotState 槽口状态引用
     * @param SearchRadius 搜索半径
     * @return 是否找到并连接
     */
    bool FindAndConnectLaneForSlot(struct FBuildingSlotState& SlotState, float SearchRadius = 50.0f);

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
