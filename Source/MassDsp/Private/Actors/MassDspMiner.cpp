#include "Actors/MassDspMiner.h"

#include "MassEntityManager.h"

#include "Fragments/MassDspMinerFragment.h"

AMassDspMiner::AMassDspMiner()
{
    FBuildingSlotDef OutputSlot;
    OutputSlot.SlotType = EBuildingSlotType::Output;
    OutputSlot.DebugColor = FColor::Blue;

    Slots.Add(OutputSlot);
}

const UScriptStruct* AMassDspMiner::GetStaticStructForFragment() const
{
    return FMassDspMinerFragment::StaticStruct();
}

void AMassDspMiner::InitFragmentForEntity(FMassEntityManager& EntityManager, FMassEntityHandle EntityHandle, const FTransform& WorldTransform) const
{
    Super::InitFragmentForEntity(EntityManager, EntityHandle, WorldTransform);

    FMassDspMinerFragment& MinerFragment = EntityManager.GetFragmentDataChecked<FMassDspMinerFragment>(EntityHandle);

    MinerFragment.ProductionInterval = ProductionInterval;
    MinerFragment.ProductionProgress = 0.0f;
    MinerFragment.InventoryCount = 0;
    MinerFragment.MaxInventory = 50;
    MinerFragment.StoredItemType = EItemType::IronOre; // TODO
}
