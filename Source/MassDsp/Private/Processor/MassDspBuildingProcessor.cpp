#include "Processor/MassDspBuildingProcessor.h"
#include "Fragments/MassDspBuildingSlotsFragment.h"
#include "Fragments/MassDspMinerFragment.h"
#include "Fragments/MassDspStorageFragment.h"
#include "Fragments/MassDspAssemblerFragment.h"
#include "Subsystems/MassDspManager.h"
#include "MassExecutionContext.h"
#include "MassCommonTypes.h"
#include "MassRepresentationFragments.h"

#include "ProfilingDebugging/CpuProfilerTrace.h"

UMassDspBuildingProcessor::UMassDspBuildingProcessor()
    : MinerQuery(*this)
      , StorageQuery(*this)
      , AssemblerQuery(*this)
      , AssemblerRenderQuery(*this)
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

    // 配置合成台Query - 查询拥有 AssemblerFragment、SlotsFragment 和 RecipeSharedFragment 的实体
    AssemblerRenderQuery.AddRequirement<FMassDspAssemblerFragment>(EMassFragmentAccess::ReadOnly);
    AssemblerRenderQuery.AddRequirement<FMassRepresentationFragment>(EMassFragmentAccess::ReadOnly);
    AssemblerRenderQuery.AddRequirement<FMassRepresentationLODFragment>(EMassFragmentAccess::ReadOnly);
    AssemblerRenderQuery.AddSharedRequirement<FMassDspRecipeSharedFragment>(EMassFragmentAccess::ReadOnly);
    AssemblerRenderQuery.AddSharedRequirement<FMassRepresentationSubsystemSharedFragment>(EMassFragmentAccess::ReadWrite);
    AssemblerRenderQuery.RegisterWithProcessor(*this);
}

struct FAssemblerToTextureData
{
    float TimeOffset = 0.0f;
};

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

    // ── Pass 1: TickExecute + 输出槽（Provide）──────────────────────────────
    // 每条传送带只有 1 个 Provide 方 → 各线程写不同 FBeltData，ParallelFor 安全
    ProcessBuildingOutputs<FMassDspMinerFragment>(MinerQuery, Context, WorldTime);
    ProcessBuildingOutputs<FMassDspStorageFragment>(StorageQuery, Context, WorldTime);
    ProcessBuildingOutputs<FMassDspAssemblerFragment>(AssemblerQuery, Context, WorldTime);

    // ── Pass 2: 输入槽（Consume）────────────────────────────────────────────
    // 每条传送带只有 1 个 Consume 方 → 各线程写不同 FBeltData，ParallelFor 安全
    // Pass 1 全部线程归栅后才进入 Pass 2 → Provide/Consume 时间上不重叠，无需锁
    ProcessBuildingInputs<FMassDspStorageFragment>(StorageQuery, Context, WorldTime);
    ProcessBuildingInputs<FMassDspAssemblerFragment>(AssemblerQuery, Context, WorldTime);
    // 矿机无 Input Slot，不参与 Pass 2

    AssemblerRenderQuery.ForEachEntityChunk(Context, [this, WorldTime](FMassExecutionContext& InContext)
    {
        const FRecipeDataForFragment& Recipe = InContext.GetSharedFragment<FMassDspRecipeSharedFragment>().Recipe;
        if (Recipe.RecipeType == ERecipeType::None) return;

        const int32 NumEntities = InContext.GetNumEntities();
        auto AssemblerFragments = InContext.GetFragmentView<FMassDspAssemblerFragment>();
        auto RepresentationFragments = InContext.GetFragmentView<FMassRepresentationFragment>();
        auto RepresentationLODFragments = InContext.GetFragmentView<FMassRepresentationLODFragment>();

        UMassRepresentationSubsystem* RepresentationSubsystem = InContext.GetSharedFragment<FMassRepresentationSubsystemSharedFragment>().RepresentationSubsystem;
        FMassInstancedStaticMeshInfoArrayView IsmInfo = RepresentationSubsystem->GetMutableInstancedStaticMeshInfos();

        for (int32 i = 0; i < NumEntities; ++i)
        {
            auto AssemblerFragment = AssemblerFragments[i];
            auto RepresentationFragment = RepresentationFragments[i];
            auto RepresentationLODFragment = RepresentationLODFragments[i];
            if ((RepresentationLODFragment.LOD != EMassLOD::Off || RepresentationLODFragment.PrevLOD != EMassLOD::Off) && RepresentationFragment.CurrentRepresentation ==
                EMassRepresentationType::StaticMeshInstance)
            {
                auto& Ism = IsmInfo[RepresentationFragment.StaticMeshDescHandle.ToIndex()];

                Ism.AddBatchedCustomData(
                    AssemblerFragment.GetCraftingProgress(WorldTime, Recipe),
                    RepresentationLODFragment.LODSignificance, RepresentationFragment.PrevLODSignificance
                );
            }
        }
    });
}

// ─────────────────────────────────────────────────────────────────────────────
//  Pass 1 驱动：TickExecute + 输出槽（Provide）
// ─────────────────────────────────────────────────────────────────────────────
template <class TT> requires IsDspBuildFragment<TT>
void UMassDspBuildingProcessor::ProcessBuildingOutputs(FMassEntityQuery& Query, FMassExecutionContext& Context, float WorldTime) const
{
    Query.ParallelForEachEntityChunk(Context, [this, WorldTime](FMassExecutionContext& InContext)
    {
        const int32 NumEntities = InContext.GetNumEntities();
        const TArrayView<TT> BuildingFragments = InContext.GetMutableFragmentView<TT>();
        const TArrayView<FMassDspBuildingSlotsFragment> SlotsList = InContext.GetMutableFragmentView<FMassDspBuildingSlotsFragment>();

        if constexpr (std::is_same_v<TT, FMassDspAssemblerFragment>)
        {
            const FRecipeDataForFragment& Recipe = InContext.GetSharedFragment<FMassDspRecipeSharedFragment>().Recipe;
            if (Recipe.RecipeType == ERecipeType::None) return;
            for (int32 i = 0; i < NumEntities; ++i)
            {
                BuildingFragments[i].TickExecute(WorldTime, Recipe);
                ProcessOutputSlots(SlotsList[i], BuildingFragments[i], WorldTime, &Recipe);
            }
        }
        else if constexpr (std::is_same_v<TT, FMassDspMinerFragment>)
        {
            for (int32 i = 0; i < NumEntities; ++i)
            {
                BuildingFragments[i].TickExecute(WorldTime);
                ProcessOutputSlots(SlotsList[i], BuildingFragments[i], WorldTime);
            }
        }
        else
        {
            const float DeltaTime = InContext.GetDeltaTimeSeconds();
            for (int32 i = 0; i < NumEntities; ++i)
            {
                BuildingFragments[i].TickExecute(DeltaTime);
                ProcessOutputSlots(SlotsList[i], BuildingFragments[i], WorldTime);
            }
        }
    });
}

// ─────────────────────────────────────────────────────────────────────────────
//  Pass 2 驱动：输入槽（Consume）
// ─────────────────────────────────────────────────────────────────────────────
template <class TT> requires IsDspBuildFragment<TT>
void UMassDspBuildingProcessor::ProcessBuildingInputs(FMassEntityQuery& Query, FMassExecutionContext& Context, float WorldTime) const
{
    Query.ParallelForEachEntityChunk(Context, [this, WorldTime](FMassExecutionContext& InContext)
    {
        const int32 NumEntities = InContext.GetNumEntities();
        const TArrayView<TT> BuildingFragments = InContext.GetMutableFragmentView<TT>();
        const TArrayView<FMassDspBuildingSlotsFragment> SlotsList = InContext.GetMutableFragmentView<FMassDspBuildingSlotsFragment>();

        if constexpr (std::is_same_v<TT, FMassDspAssemblerFragment>)
        {
            const FRecipeDataForFragment& Recipe = InContext.GetSharedFragment<FMassDspRecipeSharedFragment>().Recipe;
            if (Recipe.RecipeType == ERecipeType::None) return;
            for (int32 i = 0; i < NumEntities; ++i)
                ProcessInputSlots(SlotsList[i], BuildingFragments[i], WorldTime, &Recipe);
        }
        else
        {
            for (int32 i = 0; i < NumEntities; ++i)
                ProcessInputSlots(SlotsList[i], BuildingFragments[i], WorldTime);
        }
    });
}

// ─────────────────────────────────────────────────────────────────────────────
//  输出槽核心逻辑（仅 ProvideItemToBelt）
// ─────────────────────────────────────────────────────────────────────────────
template <class TT> requires IsDspBuildFragment<TT>
void UMassDspBuildingProcessor::ProcessOutputSlots(FMassDspBuildingSlotsFragment& SlotsData, TT& Fragment, float WorldTime, const FRecipeDataForFragment* InRecipe) const
{
    if (SlotsData.ConnectedOutputCount <= 0) return;

    if constexpr (std::is_same_v<TT, FMassDspMinerFragment>)
        if (Fragment.InventoryCount == 0) return;
    if constexpr (std::is_same_v<TT, FMassDspStorageFragment>)
        if (Fragment.InventoryCount == 0) return;

    const int32 OutputsNum = SlotsData.GetOutputSlots().Num();
    bool AnySuc = false;
    for (int Idx = 0; Idx < OutputsNum; ++Idx)
    {
        auto& Slot = SlotsData.GetOutputSlotRotated(Idx);
        if (!Slot.ConnectedLaneHandle.IsValid()) continue;
        if (!Slot.IsReady(WorldTime)) continue;

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
        SlotsData.AddOutputSlotOffset();
}

// ─────────────────────────────────────────────────────────────────────────────
//  输入槽核心逻辑（仅 ConsumeItemFromBelt）
// ─────────────────────────────────────────────────────────────────────────────
template <class TT> requires IsDspBuildFragment<TT>
void UMassDspBuildingProcessor::ProcessInputSlots(FMassDspBuildingSlotsFragment& SlotsData, TT& Fragment, float WorldTime, const FRecipeDataForFragment* InRecipe) const
{
    if (SlotsData.ConnectedInputCount <= 0) return;

    if constexpr (std::is_same_v<TT, FMassDspStorageFragment>)
        if (Fragment.InventoryCount >= Fragment.MaxInventory) return;

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
        SlotsData.AddInputSlotOffset();
}
