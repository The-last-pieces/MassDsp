#include "Actors/MassDspStorage.h"

AMassDspStorage::AMassDspStorage()
{
    // 配置默认槽口：只需要一个输入口
    FBuildingSlotDef InputSlot;
    InputSlot.SlotType = EBuildingSlotType::Input;
    // 假设输入口在后方 100 单位处
    InputSlot.LocalTransform = FTransform(FRotator(0, 180, 0), FVector(-50.0f, 0.0f, 0.0f));
    InputSlot.DebugColor = FColor::Orange;

    Slots.Add(InputSlot);
}
