#include "Actors/MassDspStorage.h"

#include "MassEntityManager.h"

#include "Fragments/MassDspWarehouseFragment.h"
#include "Fragments/MassDspBuildingSlotsFragment.h"

AMassDspStorage::AMassDspStorage()
{
    FBuildingSlotDef InputSlot;
    InputSlot.SlotType = EBuildingSlotType::Input;
    InputSlot.DebugColor = FColor::Orange;

    Slots.Add(InputSlot);
}

const UScriptStruct* AMassDspStorage::GetStaticStructForFragment() const
{
    return FMassDspWarehouseFragment::StaticStruct();
}

TArray<const UScriptStruct*> AMassDspStorage::GetStaticStructs() const
{
    return {
        FMassDspWarehouseFragment::StaticStruct(),
        FMassDspBuildingSlotsFragment::StaticStruct(),
    };
}

void AMassDspStorage::InitFragmentForEntity(FMassEntityManager& EntityManager, FMassEntityHandle EntityHandle, const FTransform& WorldTransform) const
{
    Super::InitFragmentForEntity(EntityManager, EntityHandle, WorldTransform);

    FMassDspWarehouseFragment& WarehouseFragment = EntityManager.GetFragmentDataChecked<FMassDspWarehouseFragment>(EntityHandle);
    WarehouseFragment.Initialize(Capacity);
}
