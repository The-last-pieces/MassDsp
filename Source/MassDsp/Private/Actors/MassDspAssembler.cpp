#include "Actors/MassDspAssembler.h"

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

    // 设置默认配方示例：铁板 + 铁板 -> 铁齿轮
    CurrentRecipe.RecipeName = TEXT("铁齿轮");
    CurrentRecipe.Inputs.Add(FRecipeInput(EItemType::IronPlate, 2));
    CurrentRecipe.Output = FRecipeOutput(EItemType::IronGear, 1);
    CurrentRecipe.CraftingTime = 2.0f;
}

const UScriptStruct* AMassDspAssembler::GetStaticStructForFragment() const
{
    return FMassDspAssemblerFragment::StaticStruct();
}

void AMassDspAssembler::InitFragmentForEntity(FMassEntityManager& EntityManager, FMassEntityHandle EntityHandle) const
{
    Super::InitFragmentForEntity(EntityManager, EntityHandle);

    if (FMassDspAssemblerFragment* AssemblerFragment = EntityManager.GetFragmentDataPtr<FMassDspAssemblerFragment>(EntityHandle))
    {
        AssemblerFragment->CurrentRecipe = CurrentRecipe.ToFragment();
        AssemblerFragment->CraftingSpeedMultiplier = CraftingSpeedMultiplier;
        AssemblerFragment->InputBufferCapacity = InputBufferCapacity;
        AssemblerFragment->OutputBufferCapacity = OutputBufferCapacity;
        AssemblerFragment->MaxInventory = OutputBufferCapacity;
        AssemblerFragment->CraftingProgress = 0.0f;
        AssemblerFragment->OutputBufferCount = 0;
    }
}
