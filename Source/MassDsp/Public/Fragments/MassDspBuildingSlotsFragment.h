#pragma once

#include "CoreMinimal.h"
#include "GameConst.h"
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

    // 传送带速度
    UPROPERTY()
    float BeltSpeed = 0.f;

    // Slot 允许下次传输的绝对世界时间（代替剩余冷却秒数，避免每帧写入 Cache Line）
    UPROPERTY()
    float ReadyAtTime = 0.f;

    // 纯只读比较，不写内存
    bool IsReady(float WorldTime) const
    {
        return WorldTime >= ReadyAtTime;
    }

    void SetReadyAt(float WorldTime)
    {
        if (BeltSpeed > 0.f)
        {
            ReadyAtTime = WorldTime + (FGameConst::HalfLength * 2 + FGameConst::MinSpacing) / BeltSpeed;
        }
    }
};

/**
 * 存储建筑所有槽口状态的 Fragment
 * 注意：固定大小数组的性能比TArray更好，更符合ECS的缓存友好性
 */
USTRUCT()
struct MASSDSP_API FMassDspBuildingSlotsFragment : public FMassFragment
{
    GENERATED_BODY()

private:
    UPROPERTY()
    FBuildingSlotState OutputSlots[FGameConst::SlotMaxCount];

    UPROPERTY()
    int32 OutputSlotCount = 0;

    UPROPERTY()
    FBuildingSlotState InputSlots[FGameConst::SlotMaxCount];

    UPROPERTY()
    int32 InputSlotCount = 0;

    UPROPERTY()
    int32 OutputOffset = 0;

    UPROPERTY()
    int32 InputOffset = 0;

public:
    // 已接入传送带的 Output/Input 槽数量（ProcessSlots 顶层早退用）。
    // 仅在 CreateAndLinkBeltForSlot 成功时由 Manager 自增。
    int32 ConnectedOutputCount = 0;
    int32 ConnectedInputCount  = 0;

    FORCEINLINE void MarkOutputConnected() { ++ConnectedOutputCount; }
    FORCEINLINE void MarkInputConnected()  { ++ConnectedInputCount; }

    void AddSlot(const FBuildingSlotState& NewSlot);

    TArrayView<FBuildingSlotState> GetOutputSlots();

    TArrayView<FBuildingSlotState> GetInputSlots();

    FBuildingSlotState& GetOutputSlotRotated(int32 Index);

    FBuildingSlotState& GetInputSlotRotated(int32 Index);

    void AddInputSlotOffset();

    void AddOutputSlotOffset();
};
