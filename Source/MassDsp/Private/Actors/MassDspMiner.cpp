#include "Actors/MassDspMiner.h"

AMassDspMiner::AMassDspMiner()
{
    FBuildingSlotDef OutputSlot;
    OutputSlot.SlotType = EBuildingSlotType::Output;
    OutputSlot.DebugColor = FColor::Blue;

    Slots.Add(OutputSlot);
}
