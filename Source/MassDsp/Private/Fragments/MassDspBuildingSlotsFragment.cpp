#include "Fragments/MassDspBuildingSlotsFragment.h"

void FMassDspBuildingSlotsFragment::AddSlot(const FBuildingSlotState& NewSlot)
{
    if (NewSlot.Type == EBuildingSlotType::Input)
    {
        if (InputSlotCount < FGameConst::SlotMaxCount)
        {
            InputSlots[InputSlotCount] = NewSlot;
            InputSlotCount++;
        }
    }
    else
    {
        if (OutputSlotCount < FGameConst::SlotMaxCount)
        {
            OutputSlots[OutputSlotCount] = NewSlot;
            OutputSlotCount++;
        }
    }
}

TArrayView<FBuildingSlotState> FMassDspBuildingSlotsFragment::GetOutputSlots()
{
    return TArrayView(OutputSlots, OutputSlotCount);
}

TArrayView<FBuildingSlotState> FMassDspBuildingSlotsFragment::GetInputSlots()
{
    return TArrayView(InputSlots, InputSlotCount);
}

FBuildingSlotState& FMassDspBuildingSlotsFragment::GetOutputSlotRotated(int32 Index)
{
    int32 ActualIndex = (Index + OutputOffset) % OutputSlotCount;
    return OutputSlots[ActualIndex];
}

FBuildingSlotState& FMassDspBuildingSlotsFragment::GetInputSlotRotated(int32 Index)
{
    int32 ActualIndex = (Index + InputOffset) % InputSlotCount;
    return InputSlots[ActualIndex];
}

void FMassDspBuildingSlotsFragment::AddInputSlotOffset()
{
    if (InputSlotCount <= 1)return;
    InputOffset = (InputOffset + 1) % InputSlotCount;
}

void FMassDspBuildingSlotsFragment::AddOutputSlotOffset()
{
    if (OutputSlotCount <= 1)return;
    OutputOffset = (OutputOffset + 1) % OutputSlotCount;
}
