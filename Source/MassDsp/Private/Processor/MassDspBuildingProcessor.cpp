#include "Processor/MassDspBuildingProcessor.h"
#include "Fragments/MassDspBuildingFragment.h"
#include "Subsystems/MassDspManager.h"
#include "MassExecutionContext.h"
#include "MassCommonTypes.h"
#include "MassDspGameMode.h"
#include "Actors/MassDspMiner.h"
#include "Actors/MassDspStorage.h"

UMassDspBuildingProcessor::UMassDspBuildingProcessor() : BuildingQuery(*this)
{
    // 设置处理器执行顺序
    ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::SyncWorldToMass;
}

void UMassDspBuildingProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
    BuildingQuery.AddRequirement<FMassDspBuildingFragment>(EMassFragmentAccess::ReadWrite);
    BuildingQuery.AddRequirement<FMassDspBuildingSlotsFragment>(EMassFragmentAccess::ReadWrite);
    BuildingQuery.RegisterWithProcessor(*this);
}

void UMassDspBuildingProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
    // 获取必要的子系统
    UWorld* World = EntityManager.GetWorld();
    if (!World) return;

    auto GameMode = Cast<AMassDspGameMode>(World->GetAuthGameMode());
    if (!GameMode) return;

    if (!DspManager.IsValid())
    {
        DspManager = World->GetSubsystem<UMassDspManager>();
    }

    if (!DspManager.IsValid()) return;

    // 遍历所有建筑实体
    BuildingQuery.ForEachEntityChunk(Context, [this, GameMode](FMassExecutionContext& Context)
    {
        const int32 NumEntities = Context.GetNumEntities();
        const TArrayView<FMassDspBuildingFragment> Buildings = Context.GetMutableFragmentView<FMassDspBuildingFragment>();
        const TArrayView<FMassDspBuildingSlotsFragment> SlotsList = Context.GetMutableFragmentView<FMassDspBuildingSlotsFragment>();
        const float DeltaTime = Context.GetDeltaTimeSeconds();

        for (int32 i = 0; i < NumEntities; ++i)
        {
            FMassDspBuildingFragment& Building = Buildings[i];
            FMassDspBuildingSlotsFragment& SlotsData = SlotsList[i];

            // --- 1. 建筑本身逻辑 (生产/消耗) ---
            if (AMassDspMiner* MinerActor = Cast<AMassDspMiner>(Building.BuildingActor.Get()))
            {
                if (Building.InventoryCount < Building.MaxInventory)
                {
                    Building.ProductionProgress += DeltaTime / (MinerActor->ProductionInterval > 0 ? MinerActor->ProductionInterval : 1.0f);
                    if (Building.ProductionProgress >= 1.0f)
                    {
                        Building.InventoryCount++;
                        Building.ProductionProgress = 0.0f;
                    }
                }
            }
            else if (AMassDspStorage* StorageActor = Cast<AMassDspStorage>(Building.BuildingActor.Get()))
            {
                // 仓库逻辑：只是存储，这里不需要Tick做什么，
                // 它的 InventoryCount 由输入槽口逻辑增加
                Building.MaxInventory = StorageActor->Capacity;
            }

            // --- 2. 槽口逻辑 (输入/输出) ---
            for (FBuildingSlotState& Slot : SlotsData.Slots)
            {
                if (!Slot.ConnectedLaneHandle.IsValid()) continue;

                // --- 输出槽口逻辑 (Building -> Belt) ---
                if (Slot.Type == EBuildingSlotType::Output)
                {
                    // 如果建筑有库存，且传送带口有位置
                    if (Building.InventoryCount > 0)
                    {
                        if (DspManager->ProvideItemToBelt(Context.Defer(), Slot.ConnectedLaneHandle, GameMode->BeltItemConfigAsset))
                        {
                            Building.InventoryCount--;
                        }
                    }
                }

                // --- 输入槽口逻辑 (Belt -> Building) ---
                else if (Slot.Type == EBuildingSlotType::Input)
                {
                    // 如果仓库未满
                    if (Building.InventoryCount < Building.MaxInventory)
                    {
                        if (DspManager->ConsumeItemFromBelt(Context.Defer(), Slot.ConnectedLaneHandle))
                        {
                            Building.InventoryCount++;
                        }
                    }
                }
            }
        }
    });
}
