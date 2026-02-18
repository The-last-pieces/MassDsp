#include "Actors/MassDspStorage.h"

AMassDspStorage::AMassDspStorage()
{
    FBuildingSlotDef InputSlot;
    InputSlot.SlotType = EBuildingSlotType::Input;
    InputSlot.DebugColor = FColor::Orange;

    Slots.Add(InputSlot);
}
