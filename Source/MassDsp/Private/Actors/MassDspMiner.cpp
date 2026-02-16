#include "Actors/MassDspMiner.h"

AMassDspMiner::AMassDspMiner()
{
    // 配置默认槽口：只需要一个输出口
    FBuildingSlotDef OutputSlot;
    OutputSlot.SlotType = EBuildingSlotType::Output;
    // 假设输出口在前方 100 单位处
    OutputSlot.LocalTransform = FTransform(FRotator::ZeroRotator, FVector(50.0f, 0.0f, 0.0f));
    OutputSlot.DebugColor = FColor::Blue;

    Slots.Add(OutputSlot);
}
