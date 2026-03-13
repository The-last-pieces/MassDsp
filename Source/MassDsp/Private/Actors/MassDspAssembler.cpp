#include "Actors/MassDspAssembler.h"

#include "MassDspGameMode.h"
#include "MassEntityManager.h"

#include "Fragments/MassDspAssemblerFragment.h"

AMassDspAssembler::AMassDspAssembler()
{
    // 设置默认槽位配置：3个输入 + 1个输出
    Slots.Empty();

    // 输入槽1 - 左侧
    FBuildingSlotDef InputSlot1;
    InputSlot1.LocalTransform.SetLocation(FVector(-100.0f, -100.0f, 0.0f));
    InputSlot1.LocalTransform.SetRotation(FQuat::Identity);
    InputSlot1.SlotType = EBuildingSlotType::Input;
    InputSlot1.SlotExtend = 100.0f;
    InputSlot1.DebugColor = FColor::Green;
    Slots.Add(InputSlot1);

    // 输入槽2 - 中间
    FBuildingSlotDef InputSlot2;
    InputSlot2.LocalTransform.SetLocation(FVector(-100.0f, 0.0f, 0.0f));
    InputSlot2.LocalTransform.SetRotation(FQuat::Identity);
    InputSlot2.SlotType = EBuildingSlotType::Input;
    InputSlot2.SlotExtend = 100.0f;
    InputSlot2.DebugColor = FColor::Green;
    Slots.Add(InputSlot2);

    // 输入槽3 - 右侧
    FBuildingSlotDef InputSlot3;
    InputSlot3.LocalTransform.SetLocation(FVector(-100.0f, 100.0f, 0.0f));
    InputSlot3.LocalTransform.SetRotation(FQuat::Identity);
    InputSlot3.SlotType = EBuildingSlotType::Input;
    InputSlot3.SlotExtend = 100.0f;
    InputSlot3.DebugColor = FColor::Green;
    Slots.Add(InputSlot3);

    // 输出槽 - 前方
    FBuildingSlotDef OutputSlot;
    OutputSlot.LocalTransform.SetLocation(FVector(100.0f, 0.0f, 0.0f));
    OutputSlot.LocalTransform.SetRotation(FQuat::Identity);
    OutputSlot.SlotType = EBuildingSlotType::Output;
    OutputSlot.SlotExtend = 100.0f;
    OutputSlot.DebugColor = FColor::Red;
    Slots.Add(OutputSlot);

    RecipeType = ERecipeType::IronPlate;
}

const UScriptStruct* AMassDspAssembler::GetStaticStructForFragment() const
{
    return FMassDspAssemblerFragment::StaticStruct();
}

void AMassDspAssembler::InitFragmentForEntity(FMassEntityManager& EntityManager, FMassEntityHandle EntityHandle, const FTransform& WorldTransform) const
{
    Super::InitFragmentForEntity(EntityManager, EntityHandle, WorldTransform);

    FMassDspAssemblerFragment& AssemblerFragment = EntityManager.GetFragmentDataChecked<FMassDspAssemblerFragment>(EntityHandle);

    // 配方数据已通过 FMassDspRecipeSharedFragment 在 CreateBuildingEntityInternal 中注入 Archetype，
    // 此处只需设置实体独有的运行时数值。
    AssemblerFragment.ActiveRecipeType = RecipeType;
    AssemblerFragment.CraftingSpeedMultiplier = CraftingSpeedMultiplier;
    AssemblerFragment.InputBufferCapacity = InputBufferCapacity;
    AssemblerFragment.OutputBufferCapacity = OutputBufferCapacity;
}
