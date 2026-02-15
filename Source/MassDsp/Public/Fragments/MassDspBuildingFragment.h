#pragma once

#include "CoreMinimal.h"
#include "MassEntityTypes.h"
#include "Actors/MassDspBuilding.h" // 引用 SlotType 枚举
#include "MassDspBuildingFragment.generated.h"

/**
 * 建筑的基础运行时数据 Fragment
 */
USTRUCT()
struct MASSDSP_API FMassDspBuildingFragment : public FMassFragment
{
    GENERATED_BODY()

    // 关联的 Actor 指针（如果仍然存在）- 这在混合模式下有用，纯ECS模式下可能不需要
    UPROPERTY(Transient)
    TWeakObjectPtr<AMassDspBuilding> BuildingActor;
    
    // 建筑当前的运行状态（例如：0=Idle, 1=Production, 2=Blocked）
    UPROPERTY()
    uint8 State = 0;

    // 当前生产进度 (0.0 - 1.0)
    UPROPERTY()
    float ProductionProgress = 0.0f;
};

/**
 * 建筑槽口的运行时数据 Item
 * 这个结构通常被存储在一个 Fragment 的数组中，或者采用 tag component 方式
 * 为了简单起见，作为共享Fragment或者Tag可能更合适，
 * 但如果每个槽口状态不同（如有物品占用），则需要动态 Fragment。
 * 这里简单定义一个单体 Fragment 来存储所有槽口的状态。
 */
USTRUCT()
struct FBuildingSlotState
{
    GENERATED_BODY()

    // 世界位置 (缓存)
    UPROPERTY()
    FVector WorldLocation = FVector::ZeroVector;

    // 世界旋转 (缓存)
    UPROPERTY()
    FQuat WorldRotation = FQuat::Identity;

    // 类型
    UPROPERTY()
    EBuildingSlotType Type = EBuildingSlotType::Input;

    // 当前占用的物品实体 (如果有)
    FMassEntityHandle OccupyingItem;

    // 是否被连接（例如连接了传送带）
    UPROPERTY()
    bool bConnected = false;
};

/**
 * 存储建筑所有槽口状态的 Fragment
 * 注意：TArray 在 ECS 中拷贝开销较大，且不利于缓存命中，生产环境可能需要优化
 * (例如使用 MassChunkFragments 或固定大小数组)
 */
USTRUCT()
struct MASSDSP_API FMassDspBuildingSlotsFragment : public FMassFragment
{
    GENERATED_BODY()

    UPROPERTY()
    FBuildingSlotState Slots[4];

    UPROPERTY()
    int SlotCount = 0;

    void AddSlot(const FBuildingSlotState& NewSlot)
    {
        if (SlotCount < 4)
        {
            Slots[SlotCount++] = NewSlot;
        }
	}
};
