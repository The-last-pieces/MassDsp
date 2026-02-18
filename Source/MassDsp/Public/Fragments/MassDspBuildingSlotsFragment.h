#pragma once

#include "CoreMinimal.h"
#include "GameConst.h"
#include "MassEntityTypes.h"
#include "MassDspBeltTypes.h"
#include "Actors/MassDspBuilding.h"
#include "MassDspBuildingSlotsFragment.generated.h"

/**
 * 建筑槽口的运行时数据
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

    // 传送带延申长度
    UPROPERTY()
    float SlotExtend = 100.f;

    // 类型
    UPROPERTY()
    EBuildingSlotType Type = EBuildingSlotType::Input;

    // 连接的传送带句柄 (缓存)
    UPROPERTY()
    FBeltHandle ConnectedLaneHandle;
};

/**
 * 存储建筑所有槽口状态的 Fragment
 * 注意：固定大小数组的性能比TArray更好，更符合ECS的缓存友好性
 */
USTRUCT()
struct MASSDSP_API FMassDspBuildingSlotsFragment : public FMassFragment
{
    GENERATED_BODY()

public:
    UPROPERTY()
    FBuildingSlotState Slots[FGameConst::SlotMaxCount];

private:
    UPROPERTY()
    int32 SlotCount = 0;

public:
    void AddSlot(const FBuildingSlotState& NewSlot);

    TArrayView<const FBuildingSlotState> GetSlots() const;

    TArrayView<FBuildingSlotState> GetSlots();
};