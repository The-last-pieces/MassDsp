#include "Actors/MassDspLogisticsTower.h"

#include "MassEntityManager.h"

#include "Fragments/MassDspBuildingSlotsFragment.h"
#include "Fragments/MassDspStorageFragment.h"
#include "Fragments/MassDspLogisticsTowerFragment.h"

AMassDspLogisticsTower::AMassDspLogisticsTower()
{
    FBuildingSlotDef InputSlot;
    InputSlot.SlotType = EBuildingSlotType::Input;
    InputSlot.DebugColor = FColor::Orange;

    Slots.Add(InputSlot);

    FBuildingSlotDef OutputSlot;
    OutputSlot.SlotType = EBuildingSlotType::Output;
    OutputSlot.DebugColor = FColor::Cyan;

    Slots.Add(OutputSlot);
}

const UScriptStruct* AMassDspLogisticsTower::GetStaticStructForFragment() const
{
    return FMassDspStorageFragment::StaticStruct();
}

TArray<const UScriptStruct*> AMassDspLogisticsTower::GetStaticStructs() const
{
    TArray<const UScriptStruct*> Structs = {
        FMassDspStorageFragment::StaticStruct(),
        FMassDspBuildingSlotsFragment::StaticStruct(),
    };
    Structs.Add(FMassDspLogisticsTowerFragment::StaticStruct());
    return Structs;
}

void AMassDspLogisticsTower::InitFragmentForEntity(
    FMassEntityManager& EntityManager,
    FMassEntityHandle EntityHandle,
    const FTransform& WorldTransform) const
{
    Super::InitFragmentForEntity(EntityManager, EntityHandle, WorldTransform);

    FMassDspStorageFragment& StorageFrag =
        EntityManager.GetFragmentDataChecked<FMassDspStorageFragment>(EntityHandle);
    StorageFrag.MaxInventory = Capacity;
    StorageFrag.InventoryCount = 0;
    StorageFrag.StoredItemType = EItemType::None;

    FMassDspLogisticsTowerFragment& TowerFrag =
        EntityManager.GetFragmentDataChecked<FMassDspLogisticsTowerFragment>(EntityHandle);

    TowerFrag.TowerMode = TowerMode;
    TowerFrag.ItemType = ItemType;
    TowerFrag.RequestThreshold = RequestThreshold;
    TowerFrag.DroneCargoCount = DroneCargoCount;
    TowerFrag.CoverageRadius = CoverageRadius;
    TowerFrag.ScanInterval = ScanInterval;
    TowerFrag.LastScanTime = 0.f;
    TowerFrag.bDirty = false;
    TowerFrag.bAcceptsRequests = true;
}
