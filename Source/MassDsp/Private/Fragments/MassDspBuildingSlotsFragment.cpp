#include "Fragments/MassDspBuildingSlotsFragment.h"

void FMassDspBuildingSlotsFragment::AddSlot(const FBuildingSlotState& NewSlot)
{
    if (SlotCount < FGameConst::SlotMaxCount)
    {
        Slots[SlotCount] = NewSlot;
        SlotCount++;
    }
}

TArrayView<const FBuildingSlotState> FMassDspBuildingSlotsFragment::GetSlots() const
{
    return TArrayView(Slots, SlotCount);
}

TArrayView<FBuildingSlotState> FMassDspBuildingSlotsFragment::GetSlots()
{
    return TArrayView(Slots, SlotCount);
}
