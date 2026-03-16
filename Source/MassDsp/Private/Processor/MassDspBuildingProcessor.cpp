#include "Processor/MassDspBuildingProcessor.h"
#include "Fragments/MassDspBuildingSlotsFragment.h"
#include "Fragments/MassDspMinerFragment.h"
#include "Fragments/MassDspStorageFragment.h"
#include "Fragments/MassDspWarehouseFragment.h"
#include "Fragments/MassDspAssemblerFragment.h"
#include "MassDspGameMode.h"
#include "Subsystems/MassDspManager.h"
#include "Subsystems/MassDspDebugStatsSubsystem.h"
#include "MassExecutionContext.h"
#include "MassCommonTypes.h"

#include "ProfilingDebugging/CpuProfilerTrace.h"

UMassDspBuildingProcessor::UMassDspBuildingProcessor()
    : MinerQuery(*this)
      , StorageQuery(*this)
      , WarehouseQuery(*this)
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

    // 配置传统存储Query - 查询拥有StorageFragment和SlotsFragment的实体（物流塔）
    StorageQuery.AddRequirement<FMassDspStorageFragment>(EMassFragmentAccess::ReadWrite);
    StorageQuery.AddRequirement<FMassDspBuildingSlotsFragment>(EMassFragmentAccess::ReadWrite);
    StorageQuery.RegisterWithProcessor(*this);

    // 配置泛型仓库Query - 查询拥有WarehouseFragment和SlotsFragment的实体
    WarehouseQuery.AddRequirement<FMassDspWarehouseFragment>(EMassFragmentAccess::ReadWrite);
    WarehouseQuery.AddRequirement<FMassDspBuildingSlotsFragment>(EMassFragmentAccess::ReadWrite);
    WarehouseQuery.RegisterWithProcessor(*this);

    // 配置合成台Query - 查询拥有 AssemblerFragment、SlotsFragment 和 RecipeSharedFragment 的实体
    AssemblerQuery.AddRequirement<FMassDspAssemblerFragment>(EMassFragmentAccess::ReadWrite);
    AssemblerQuery.AddRequirement<FMassDspBuildingSlotsFragment>(EMassFragmentAccess::ReadWrite);
    AssemblerQuery.RegisterWithProcessor(*this);
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

    UMassDspDebugStatsSubsystem* StatsSubsystem = DebugStatsSubsystem.IsValid()
        ? DebugStatsSubsystem.Get()
        : World->GetSubsystem<UMassDspDebugStatsSubsystem>();
    DebugStatsSubsystem = StatsSubsystem;
    if (StatsSubsystem)
    {
        StatsSubsystem->ResetTheoreticalRates();
    }

    const AMassDspGameMode* GameMode = Cast<AMassDspGameMode>(World->GetAuthGameMode());
    const UGameConfigData* GameConfig = GameMode ? GameMode->GameConfig.Get() : nullptr;

    const float WorldTime = World->GetTimeSeconds();

    // ── Pass 1: TickExecute + 输出槽（Provide）──────────────────────────────
    // 每条传送带只有 1 个 Provide 方 → 各线程写不同 FBeltData，ParallelFor 安全
    ProcessBuildingOutputs<FMassDspMinerFragment>(MinerQuery, Context, WorldTime, GameConfig, StatsSubsystem);
    ProcessBuildingOutputs<FMassDspStorageFragment>(StorageQuery, Context, WorldTime, GameConfig, StatsSubsystem);
    ProcessBuildingOutputs<FMassDspWarehouseFragment>(WarehouseQuery, Context, WorldTime, GameConfig, StatsSubsystem);
    ProcessBuildingOutputs<FMassDspAssemblerFragment>(AssemblerQuery, Context, WorldTime, GameConfig, StatsSubsystem);

    // ── Pass 2: 输入槽（Consume）────────────────────────────────────────────
    // 每条传送带只有 1 个 Consume 方 → 各线程写不同 FBeltData，ParallelFor 安全
    // Pass 1 全部线程归栅后才进入 Pass 2 → Provide/Consume 时间上不重叠，无需锁
    ProcessBuildingInputs<FMassDspStorageFragment>(StorageQuery, Context, WorldTime, GameConfig, nullptr);
    ProcessBuildingInputs<FMassDspWarehouseFragment>(WarehouseQuery, Context, WorldTime, GameConfig, nullptr);
    ProcessBuildingInputs<FMassDspAssemblerFragment>(AssemblerQuery, Context, WorldTime, GameConfig, StatsSubsystem);
    // 矿机无 Input Slot，不参与 Pass 2
}

// ─────────────────────────────────────────────────────────────────────────────
//  Pass 1 驱动：TickExecute + 输出槽（Provide）
// ─────────────────────────────────────────────────────────────────────────────
template <class TT> requires IsDspBuildFragment<TT>
void UMassDspBuildingProcessor::ProcessBuildingOutputs(FMassEntityQuery& Query, FMassExecutionContext& Context, float WorldTime, const UGameConfigData* GameConfig, UMassDspDebugStatsSubsystem* StatsSubsystem) const
{
    Query.ParallelForEachEntityChunk(Context, [this, WorldTime, GameConfig, StatsSubsystem](FMassExecutionContext& InContext)
    {
        const int32 NumEntities = InContext.GetNumEntities();
        const TArrayView<TT> BuildingFragments = InContext.GetMutableFragmentView<TT>();
        const TArrayView<FMassDspBuildingSlotsFragment> SlotsList = InContext.GetMutableFragmentView<FMassDspBuildingSlotsFragment>();
        TArray<float> LocalProductionRates;
        TArray<float> LocalConsumptionRates;
        if (StatsSubsystem)
        {
            LocalProductionRates.Init(0.f, UMassDspDebugStatsSubsystem::TrackedItemTypeCount);
            LocalConsumptionRates.Init(0.f, UMassDspDebugStatsSubsystem::TrackedItemTypeCount);
        }

        if constexpr (std::is_same_v<TT, FMassDspAssemblerFragment>)
        {
            for (int32 i = 0; i < NumEntities; ++i)
            {
                if (!GameConfig) continue;
                const FRecipeConfigData* RecipeConfig = GameConfig->GetRecipeConfig(BuildingFragments[i].ActiveRecipeType);
                if (!RecipeConfig) continue;
                const FRecipeDataForFragment Recipe = RecipeConfig->ToFragment(BuildingFragments[i].ActiveRecipeType);
                int32 InputCountsBefore[FGameConst::SlotMaxCount - 1] = {};
                int32 OutputCountsBefore[FGameConst::SlotMaxCount - 1] = {};
                for (int32 BufferIndex = 0; BufferIndex < FGameConst::SlotMaxCount - 1; ++BufferIndex)
                {
                    InputCountsBefore[BufferIndex] = BuildingFragments[i].InputBuffers[BufferIndex].Amount;
                    OutputCountsBefore[BufferIndex] = BuildingFragments[i].OutputBuffers[BufferIndex].Amount;
                }

                BuildingFragments[i].TickExecute(WorldTime, Recipe);

                if (StatsSubsystem)
                {
                    for (int32 InputIndex = 0; InputIndex < Recipe.InputsCount; ++InputIndex)
                    {
                        const int32 ConsumedCount = FMath::Max(0, InputCountsBefore[InputIndex] - BuildingFragments[i].InputBuffers[InputIndex].Amount);
                        if (ConsumedCount > 0)
                        {
                            StatsSubsystem->RecordConsumedItem(Recipe.Inputs[InputIndex].ItemType, ConsumedCount);
                        }
                    }

                    for (int32 OutputIndex = 0; OutputIndex < Recipe.OutputsCount; ++OutputIndex)
                    {
                        const int32 ProducedCount = FMath::Max(0, BuildingFragments[i].OutputBuffers[OutputIndex].Amount - OutputCountsBefore[OutputIndex]);
                        if (ProducedCount > 0)
                        {
                            StatsSubsystem->RecordProducedItem(Recipe.Outputs[OutputIndex].ItemType, ProducedCount);
                        }
                    }
                }

                ProcessOutputSlots(SlotsList[i], BuildingFragments[i], WorldTime, &Recipe);

                if (StatsSubsystem && BuildingFragments[i].CanSustainTheoreticalRate(Recipe))
                {
                    for (int32 OutputIndex = 0; OutputIndex < Recipe.OutputsCount; ++OutputIndex)
                    {
                        const int32 ItemIndex = static_cast<uint8>(Recipe.Outputs[OutputIndex].ItemType);
                        if (LocalProductionRates.IsValidIndex(ItemIndex))
                        {
                            LocalProductionRates[ItemIndex] += BuildingFragments[i].GetTheoreticalOutputRate(Recipe, OutputIndex);
                        }
                    }
                }
            }
        }
        else if constexpr (std::is_same_v<TT, FMassDspMinerFragment>)
        {
            for (int32 i = 0; i < NumEntities; ++i)
            {
                const int32 InventoryBefore = BuildingFragments[i].InventoryCount;
                BuildingFragments[i].TickExecute(WorldTime);
                if (StatsSubsystem)
                {
                    const int32 ProducedCount = FMath::Max(0, BuildingFragments[i].InventoryCount - InventoryBefore);
                    if (ProducedCount > 0)
                    {
                        StatsSubsystem->RecordProducedItem(BuildingFragments[i].StoredItemType, ProducedCount);
                    }
                }
                ProcessOutputSlots(SlotsList[i], BuildingFragments[i], WorldTime);

                if (StatsSubsystem)
                {
                    const int32 ItemIndex = static_cast<uint8>(BuildingFragments[i].StoredItemType);
                    if (LocalProductionRates.IsValidIndex(ItemIndex))
                    {
                        LocalProductionRates[ItemIndex] += BuildingFragments[i].GetTheoreticalProductionRate();
                    }
                }
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

        if (StatsSubsystem)
        {
            StatsSubsystem->AccumulateTheoreticalRates(LocalProductionRates, LocalConsumptionRates);
        }
    });
}

// ─────────────────────────────────────────────────────────────────────────────
//  Pass 2 驱动：输入槽（Consume）
// ─────────────────────────────────────────────────────────────────────────────
template <class TT> requires IsDspBuildFragment<TT>
void UMassDspBuildingProcessor::ProcessBuildingInputs(FMassEntityQuery& Query, FMassExecutionContext& Context, float WorldTime, const UGameConfigData* GameConfig, UMassDspDebugStatsSubsystem* StatsSubsystem) const
{
    Query.ParallelForEachEntityChunk(Context, [this, WorldTime, GameConfig, StatsSubsystem](FMassExecutionContext& InContext)
    {
        const int32 NumEntities = InContext.GetNumEntities();
        const TArrayView<TT> BuildingFragments = InContext.GetMutableFragmentView<TT>();
        const TArrayView<FMassDspBuildingSlotsFragment> SlotsList = InContext.GetMutableFragmentView<FMassDspBuildingSlotsFragment>();
        TArray<float> LocalProductionRates;
        TArray<float> LocalConsumptionRates;
        if constexpr (std::is_same_v<TT, FMassDspAssemblerFragment>)
        {
            if (StatsSubsystem)
            {
                LocalProductionRates.Init(0.f, UMassDspDebugStatsSubsystem::TrackedItemTypeCount);
                LocalConsumptionRates.Init(0.f, UMassDspDebugStatsSubsystem::TrackedItemTypeCount);
            }
        }

        if constexpr (std::is_same_v<TT, FMassDspAssemblerFragment>)
        {
            for (int32 i = 0; i < NumEntities; ++i)
            {
                if (!GameConfig) continue;
                const FRecipeConfigData* RecipeConfig = GameConfig->GetRecipeConfig(BuildingFragments[i].ActiveRecipeType);
                if (!RecipeConfig) continue;
                const FRecipeDataForFragment Recipe = RecipeConfig->ToFragment(BuildingFragments[i].ActiveRecipeType);
                ProcessInputSlots(SlotsList[i], BuildingFragments[i], WorldTime, &Recipe);

                if (StatsSubsystem && BuildingFragments[i].CanSustainTheoreticalRate(Recipe))
                {
                    for (int32 InputIndex = 0; InputIndex < Recipe.InputsCount; ++InputIndex)
                    {
                        const int32 ItemIndex = static_cast<uint8>(Recipe.Inputs[InputIndex].ItemType);
                        if (LocalConsumptionRates.IsValidIndex(ItemIndex))
                        {
                            LocalConsumptionRates[ItemIndex] += BuildingFragments[i].GetTheoreticalInputRate(Recipe, InputIndex);
                        }
                    }
                }
            }

            if (StatsSubsystem)
            {
                StatsSubsystem->AccumulateTheoreticalRates(LocalProductionRates, LocalConsumptionRates);
            }
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
    if constexpr (std::is_same_v<TT, FMassDspWarehouseFragment>)
        if (Fragment.GetInventoryCount() == 0) return;

    const int32 OutputsNum = SlotsData.GetOutputSlots().Num();
    bool AnySuc = false;
    for (int Idx = 0; Idx < OutputsNum; ++Idx)
    {
        auto& Slot = SlotsData.GetOutputSlotRotated(Idx);
        if (!Slot.ConnectedLaneHandle.IsValid()) continue;
        if (!Slot.IsReady(WorldTime)) continue;

        if (DspManager->ProvideItemToBelt(Slot.ConnectedLaneHandle, [&Fragment, Idx, InRecipe]()
        {
            if constexpr (std::is_same_v<TT, FMassDspAssemblerFragment>)
                return Fragment.TryProvideItemToSlot(Idx, *InRecipe);
            else
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
    if constexpr (std::is_same_v<TT, FMassDspWarehouseFragment>)
        if (Fragment.GetInventoryCount() >= Fragment.GetMaxInventory()) return;

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
