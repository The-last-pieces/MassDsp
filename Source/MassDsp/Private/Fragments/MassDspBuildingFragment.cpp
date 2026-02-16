#include "Fragments/MassDspBuildingFragment.h"

void FMassDspBuildingSlotsFragment::AddSlot(const FBuildingSlotState& NewSlot)
{
    if (SlotCount < FGameConst::SlotMaxCount)
    {
        Slots[SlotCount++] = NewSlot;
    }
}

TArrayView<const FBuildingSlotState> FMassDspBuildingSlotsFragment::GetSlots() const
{
    return MakeArrayView(Slots, SlotCount);
}

TArrayView<FBuildingSlotState> FMassDspBuildingSlotsFragment::GetSlots()
{
    return MakeArrayView(Slots, SlotCount);
}
