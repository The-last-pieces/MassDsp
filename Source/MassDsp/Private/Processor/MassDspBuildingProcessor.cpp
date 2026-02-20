#include "Processor/MassDspBuildingProcessor.h"
#include "Fragments/MassDspBuildingSlotsFragment.h"
#include "Fragments/MassDspMinerFragment.h"
#include "Fragments/MassDspStorageFragment.h"
#include "Fragments/MassDspAssemblerFragment.h"
#include "Subsystems/MassDspManager.h"
#include "MassExecutionContext.h"
#include "MassCommonTypes.h"

UMassDspBuildingProcessor::UMassDspBuildingProcessor()
    : MinerQuery(*this)
      , StorageQuery(*this)
      , AssemblerQuery(*this)
{
    // 设置处理器执行顺序
    ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::SyncWorldToMass;
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

    // 配置合成台Query - 查询拥有AssemblerFragment和SlotsFragment的实体
    AssemblerQuery.AddRequirement<FMassDspAssemblerFragment>(EMassFragmentAccess::ReadWrite);
    AssemblerQuery.AddRequirement<FMassDspBuildingSlotsFragment>(EMassFragmentAccess::ReadWrite);
    AssemblerQuery.RegisterWithProcessor(*this);
}

void UMassDspBuildingProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
    // 获取必要的子系统
    UWorld* World = EntityManager.GetWorld();
    if (!World) return;

    if (!DspManager.IsValid())
    {
        DspManager = World->GetSubsystem<UMassDspManager>();
    }

    if (!DspManager.IsValid()) return;

    ProcessBuilding<FMassDspMinerFragment>(MinerQuery, Context);
    ProcessBuilding<FMassDspStorageFragment>(StorageQuery, Context);
    ProcessBuilding<FMassDspAssemblerFragment>(AssemblerQuery, Context);
}

template <class TT> requires IsDspBuildFragment<TT>
void UMassDspBuildingProcessor::ProcessBuilding(FMassEntityQuery& Query, FMassExecutionContext& Context) const
{
    Query.ForEachEntityChunk(Context, [this](FMassExecutionContext& InContext)
    {
        const int32 NumEntities = InContext.GetNumEntities();
        const TArrayView<TT> BuildingFragments = InContext.GetMutableFragmentView<TT>();
        const TArrayView<FMassDspBuildingSlotsFragment> SlotsList = InContext.GetMutableFragmentView<FMassDspBuildingSlotsFragment>();

        for (int32 i = 0; i < NumEntities; ++i)
        {
            TT& Building = BuildingFragments[i];
            FMassDspBuildingSlotsFragment& SlotsData = SlotsList[i];

            ProcessSlots(SlotsData, InContext, Building);
        }
    });
}

template <class TT> requires IsDspBuildFragment<TT>
void UMassDspBuildingProcessor::ProcessSlots(FMassDspBuildingSlotsFragment& SlotsData, const FMassExecutionContext& Context, TT& Fragment) const
{
    if (!DspManager.IsValid()) return;

    auto DeltaTime = Context.GetDeltaTimeSeconds();

    Fragment.TickExecute(DeltaTime);

    bool AnySuc = false;

    auto OutputsNum = SlotsData.GetOutputSlots().Num();

    for (int Idx = 0; Idx < OutputsNum; ++Idx)
    {
        auto& Slot = SlotsData.GetOutputSlotRotated(Idx);

        if (!Slot.ConnectedLaneHandle.IsValid()) continue;

        if (!Slot.CheckCooldown(DeltaTime)) continue;

        if (DspManager->ProvideItemToBelt(Context.Defer(), Slot.ConnectedLaneHandle, [&Fragment,Idx]()
        {
            return Fragment.TryProvideItemToSlot(Idx);
        }))
        {
            Slot.ResetCooldown();
            AnySuc = true;
        }
    }

    if (AnySuc)
    {
        SlotsData.AddOutputSlotOffset();
    }

    AnySuc = false;

    auto InputsNum = SlotsData.GetInputSlots().Num();

    for (int Idx = 0; Idx < InputsNum; ++Idx)
    {
        auto& Slot = SlotsData.GetInputSlotRotated(Idx);

        if (!Slot.ConnectedLaneHandle.IsValid()) continue;

        if (!Slot.CheckCooldown(DeltaTime)) continue;

        if (DspManager->ConsumeItemFromBelt(Context.Defer(), Slot.ConnectedLaneHandle, [&Fragment](auto ItemType)
        {
            return Fragment.TryConsumeItemFromSlot(ItemType);
        }) != EItemType::None)
        {
            Slot.ResetCooldown();
            AnySuc = true;
        }
    }

    if (AnySuc)
    {
        SlotsData.AddInputSlotOffset();
    }
}
