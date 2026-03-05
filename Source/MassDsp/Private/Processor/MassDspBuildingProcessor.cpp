#include "Processor/MassDspBuildingProcessor.h"
#include "Fragments/MassDspBuildingSlotsFragment.h"
#include "Fragments/MassDspMinerFragment.h"
#include "Fragments/MassDspStorageFragment.h"
#include "Fragments/MassDspAssemblerFragment.h"
#include "Subsystems/MassDspManager.h"
#include "MassExecutionContext.h"
#include "MassCommonTypes.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

UMassDspBuildingProcessor::UMassDspBuildingProcessor()
    : MinerQuery(*this)
      , StorageQuery(*this)
      , AssemblerQuery(*this)
{
    // 设置处理器执行顺序
    ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::SyncWorldToMass;
    bRequiresGameThreadExecution = true;
}

void UMassDspBuildingProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
    // 配置矿机Query - 查询拥有MinerFragment和SlotsFragment的实体
    MinerQuery.AddRequirement<FMassDspMinerFragment>(EMassFragmentAccess::ReadWrite);
    MinerQuery.AddRequirement<FMassDspBuildingSlotsFragment>(EMassFragmentAccess::ReadWrite);
    MinerQuery.RegisterWithProcessor(*this);

    // 配置仓库Query - 查询拥有StorageFragment和SlotsFragment的实体
    StorageQuery.AddRequirement<FMassDspStorageFragment>(EMassFragmentAccess::ReadWrite);
    StorageQuery.AddRequirement<FMassDspBuildingSlotsFragment>(EMassFragmentAccess::ReadWrite);
    StorageQuery.RegisterWithProcessor(*this);

    // 配置合成台Query - 查询拥有 AssemblerFragment、SlotsFragment 和 RecipeSharedFragment 的实体
    AssemblerQuery.AddRequirement<FMassDspAssemblerFragment>(EMassFragmentAccess::ReadWrite);
    AssemblerQuery.AddRequirement<FMassDspBuildingSlotsFragment>(EMassFragmentAccess::ReadWrite);
    AssemblerQuery.AddSharedRequirement<FMassDspRecipeSharedFragment>(EMassFragmentAccess::ReadOnly);
    AssemblerQuery.RegisterWithProcessor(*this);
}

void UMassDspBuildingProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
    // TRACE_CPUPROFILER_EVENT_SCOPE(MassDspBuildingProcessor);

    UWorld* World = EntityManager.GetWorld();
    if (!World) return;

    if (!DspManager.IsValid())
    {
        DspManager = World->GetSubsystem<UMassDspManager>();
    }
    // 将 IsValid 检查提升到 Execute 顶层一次，避免在每个实体的 ProcessSlots 里重复检查
    if (!DspManager.IsValid()) return;

    const float WorldTime = World->GetTimeSeconds();

    ProcessBuilding<FMassDspMinerFragment>(MinerQuery, Context, WorldTime);
    ProcessBuilding<FMassDspStorageFragment>(StorageQuery, Context, WorldTime);
    ProcessBuilding<FMassDspAssemblerFragment>(AssemblerQuery, Context, WorldTime);
}

template <class TT> requires IsDspBuildFragment<TT>
void UMassDspBuildingProcessor::ProcessBuilding(FMassEntityQuery& Query, FMassExecutionContext& Context, float WorldTime) const
{
    Query.ParallelForEachEntityChunk(Context, [this, WorldTime](FMassExecutionContext& InContext)
    {
        const int32 NumEntities = InContext.GetNumEntities();
        const TArrayView<TT> BuildingFragments = InContext.GetMutableFragmentView<TT>();
        const TArrayView<FMassDspBuildingSlotsFragment> SlotsList = InContext.GetMutableFragmentView<FMassDspBuildingSlotsFragment>();

        // 对于合成台，在 Chunk 级别获取 SharedFragment 配方指针；
        // 同一 Chunk 内所有实体共享同一配方，该指针在 Chunk 遍历期间常驻 L1
        if constexpr (std::is_same_v<TT, FMassDspAssemblerFragment>)
        {
            const FRecipeDataForFragment& Recipe = InContext.GetSharedFragment<FMassDspRecipeSharedFragment>().Recipe;
            if (Recipe.RecipeType == ERecipeType::None) return;

            for (int32 i = 0; i < NumEntities; ++i)
            {
                BuildingFragments[i].TickExecute(WorldTime, Recipe);
                ProcessSlots(SlotsList[i], InContext, BuildingFragments[i], WorldTime, &Recipe);
            }
        }
        else if constexpr (std::is_same_v<TT, FMassDspMinerFragment>)
        {
            for (int32 i = 0; i < NumEntities; ++i)
            {
                BuildingFragments[i].TickExecute(WorldTime);
                ProcessSlots(SlotsList[i], InContext, BuildingFragments[i], WorldTime);
            }
        }
        else
        {
            const float DeltaTime = InContext.GetDeltaTimeSeconds();
            for (int32 i = 0; i < NumEntities; ++i)
            {
                BuildingFragments[i].TickExecute(DeltaTime);
                ProcessSlots(SlotsList[i], InContext, BuildingFragments[i], WorldTime);
            }
        }
    });
}

template <class TT> requires IsDspBuildFragment<TT>
void UMassDspBuildingProcessor::ProcessSlots(FMassDspBuildingSlotsFragment& SlotsData, const FMassExecutionContext& Context, TT& Fragment, float WorldTime, const FRecipeDataForFragment* InRecipe) const
{
    // 矿机：InventoryCount == 0 时无法输出，直接跳过输出遍历
    // 仓库：InventoryCount == 0 时无法输出；>= MaxInventory 时无法接收输入

    bool bSkipOutput = false, bSkipInput = false;

    if constexpr (std::is_same_v<TT, FMassDspMinerFragment>)
    {
        bSkipOutput = Fragment.InventoryCount == 0;
    }
    else if constexpr (std::is_same_v<TT, FMassDspStorageFragment>)
    {
        bSkipOutput = Fragment.InventoryCount == 0;
        bSkipInput = Fragment.InventoryCount >= Fragment.MaxInventory;
    }

    // —— 输出 Slot 处理 ——
    // ConnectedOutputCount == 0：此建筑所有输出槽均未接入传送带，跳过全部遍历
    if (SlotsData.ConnectedOutputCount > 0 && !bSkipOutput)
    {
        const int32 OutputsNum = SlotsData.GetOutputSlots().Num();
        bool AnySuc = false;
        for (int Idx = 0; Idx < OutputsNum; ++Idx)
        {
            auto& Slot = SlotsData.GetOutputSlotRotated(Idx);

            if (!Slot.ConnectedLaneHandle.IsValid()) continue;
            if (!Slot.IsReady(WorldTime)) continue; // 纯只读比较，无内存写入

            if (DspManager->ProvideItemToBelt(Slot.ConnectedLaneHandle, [&Fragment, Idx]()
            {
                return Fragment.TryProvideItemToSlot(Idx);
            }))
            {
                Slot.SetReadyAt(WorldTime);
                AnySuc = true;
            }
        }
        if (AnySuc)
        {
            SlotsData.AddOutputSlotOffset();
        }
    }

    // —— 输入 Slot 处理 ——
    if (SlotsData.ConnectedInputCount > 0 && !bSkipInput)
    {
        const int32 InputsNum = SlotsData.GetInputSlots().Num();
        bool AnySuc = false;
        for (int Idx = 0; Idx < InputsNum; ++Idx)
        {
            auto& Slot = SlotsData.GetInputSlotRotated(Idx);

            if (!Slot.ConnectedLaneHandle.IsValid()) continue;
            if (!Slot.IsReady(WorldTime)) continue;

            if (DspManager->ConsumeItemFromBelt(Slot.ConnectedLaneHandle, [&Fragment, InRecipe](auto ItemType)
            {
                if constexpr (std::is_same_v<TT, FMassDspAssemblerFragment>)
                    return Fragment.TryConsumeItemFromSlot(ItemType, *InRecipe);
                else
                    return Fragment.TryConsumeItemFromSlot(ItemType);
            }) != EItemType::None)
            {
                Slot.SetReadyAt(WorldTime);
                AnySuc = true;
            }
        }
        if (AnySuc)
        {
            SlotsData.AddInputSlotOffset();
        }
    }
}
