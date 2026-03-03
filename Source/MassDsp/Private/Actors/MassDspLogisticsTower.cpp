#include "Actors/MassDspLogisticsTower.h"

#include "MassEntityManager.h"
#include "Fragments/MassDspStorageFragment.h"
#include "Fragments/MassDspLogisticsTowerFragment.h"

AMassDspLogisticsTower::AMassDspLogisticsTower()
{
    // 默认添加一个输入槽口（传送带联动，可在蓝图中自由扩展）
    // 注：父类 AMassDspStorage 构造函数已添加一个 Input 槽，此处为物流塔再加一个 Output 槽
    FBuildingSlotDef OutputSlot;
    OutputSlot.SlotType   = EBuildingSlotType::Output;
    OutputSlot.DebugColor = FColor::Cyan;

    Slots.Add(OutputSlot);
}

const UScriptStruct* AMassDspLogisticsTower::GetStaticStructForFragment() const
{
    // 物流塔的"主" Fragment 是 StorageFragment（继承父类）
    // LogisticsTowerFragment 作为附加 Fragment 在 GetStaticStructs() 中追加
    return FMassDspStorageFragment::StaticStruct();
}

TArray<const UScriptStruct*> AMassDspLogisticsTower::GetStaticStructs() const
{
    // 调用父类获取 [StorageFragment, BuildingSlotsFragment]，再追加 LogisticsTowerFragment
    TArray<const UScriptStruct*> Structs = Super::GetStaticStructs();
    Structs.Add(FMassDspLogisticsTowerFragment::StaticStruct());
    return Structs;
}

void AMassDspLogisticsTower::InitFragmentForEntity(
    FMassEntityManager& EntityManager,
    FMassEntityHandle   EntityHandle,
    const FTransform&   WorldTransform) const
{
    // 先初始化父类（StorageFragment.MaxInventory + BuildingSlotsFragment 槽口世界位置）
    Super::InitFragmentForEntity(EntityManager, EntityHandle, WorldTransform);

    // 初始化物流塔专属 Fragment
    FMassDspLogisticsTowerFragment& TowerFrag =
        EntityManager.GetFragmentDataChecked<FMassDspLogisticsTowerFragment>(EntityHandle);

    TowerFrag.CoverageRadius   = CoverageRadius;
    TowerFrag.ScanInterval     = ScanInterval;
    TowerFrag.LastScanTime     = 0.f;
    TowerFrag.bDirty           = false;
    TowerFrag.bAcceptsRequests = true;
    TowerFrag.DesiredItemType  = DesiredItemType;
    TowerFrag.SupplyTriggerRatio = SupplyTriggerRatio;
    TowerFrag.DemandTriggerRatio = DemandTriggerRatio;
}
