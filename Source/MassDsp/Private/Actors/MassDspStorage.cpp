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

void AMassDspStorage::InitFragmentForEntity(FMassEntityManager& EntityManager, FMassEntityHandle EntityHandle, const FTransform& WorldTransform) const
{
    Super::InitFragmentForEntity(EntityManager, EntityHandle, WorldTransform);

    FMassDspStorageFragment& StorageFragment = EntityManager.GetFragmentDataChecked<FMassDspStorageFragment>(EntityHandle);

    StorageFragment.MaxInventory = Capacity;
    StorageFragment.InventoryCount = 0;
}
