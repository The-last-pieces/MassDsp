#include "Actors/MassDspStorage.h"

#include "MassEntityManager.h"

#include "Fragments/MassDspStorageFragment.h"

AMassDspStorage::AMassDspStorage()
{
    FBuildingSlotDef InputSlot;
    InputSlot.SlotType = EBuildingSlotType::Input;
    InputSlot.DebugColor = FColor::Orange;

    Slots.Add(InputSlot);
}

const UScriptStruct* AMassDspStorage::GetStaticStructForFragment() const
{
    return FMassDspStorageFragment::StaticStruct();
}

void AMassDspStorage::InitFragmentForEntity(FMassEntityManager& EntityManager, FMassEntityHandle EntityHandle) const
{
    Super::InitFragmentForEntity(EntityManager, EntityHandle);

    if (FMassDspStorageFragment* StorageFragment = EntityManager.GetFragmentDataPtr<FMassDspStorageFragment>(EntityHandle))
    {
        StorageFragment->MaxInventory = Capacity;
        StorageFragment->InventoryCount = 0;
    }
}
