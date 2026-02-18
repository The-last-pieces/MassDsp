#include "Processor/MassDspBuildingProcessor.h"
#include "Fragments/MassDspBuildingFragment.h"
#include "Fragments/MassDspMinerFragment.h"
#include "Fragments/MassDspStorageFragment.h"
#include "Fragments/MassDspAssemblerFragment.h"
#include "Subsystems/MassDspManager.h"
#include "MassExecutionContext.h"
#include "MassCommonTypes.h"
#include "MassDspGameMode.h"

UMassDspBuildingProcessor::UMassDspBuildingProcessor()
    : MinerQuery(*this)
      , StorageQuery(*this)
      , AssemblerQuery(*this)
      , SlotQuery(*this)
{
    // 设置处理器执行顺序
    ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::SyncWorldToMass;
}

void UMassDspBuildingProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
    // 配置矿机Query - 查询拥有BuildingFragment和MinerFragment的实体
    MinerQuery.AddRequirement<FMassDspBuildingFragment>(EMassFragmentAccess::ReadWrite);
    MinerQuery.AddRequirement<FMassDspMinerFragment>(EMassFragmentAccess::ReadWrite);
    MinerQuery.RegisterWithProcessor(*this);

    // 配置仓库Query - 查询拥有BuildingFragment和StorageFragment的实体
    StorageQuery.AddRequirement<FMassDspBuildingFragment>(EMassFragmentAccess::ReadWrite);
    StorageQuery.AddRequirement<FMassDspStorageFragment>(EMassFragmentAccess::ReadWrite);
    StorageQuery.RegisterWithProcessor(*this);

    // 配置合成台Query - 查询拥有BuildingFragment和AssemblerFragment的实体
    AssemblerQuery.AddRequirement<FMassDspBuildingFragment>(EMassFragmentAccess::ReadWrite);
    AssemblerQuery.AddRequirement<FMassDspAssemblerFragment>(EMassFragmentAccess::ReadWrite);
    AssemblerQuery.RegisterWithProcessor(*this);

    // 配置槽口Query - 查询拥有BuildingFragment和SlotsFragment的实体（所有建筑都有）
    SlotQuery.AddRequirement<FMassDspBuildingFragment>(EMassFragmentAccess::ReadWrite);
    SlotQuery.AddRequirement<FMassDspBuildingSlotsFragment>(EMassFragmentAccess::ReadWrite);
    SlotQuery.RegisterWithProcessor(*this);
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

    const float DeltaTime = Context.GetDeltaTimeSeconds();

    // --- 1. 处理矿机生产逻辑 ---
    MinerQuery.ForEachEntityChunk(Context, [DeltaTime](FMassExecutionContext& Context)
    {
        const int32 NumEntities = Context.GetNumEntities();
        const TArrayView<FMassDspBuildingFragment> BuildingFragments = Context.GetMutableFragmentView<FMassDspBuildingFragment>();
        const TArrayView<FMassDspMinerFragment> MinerFragments = Context.GetMutableFragmentView<FMassDspMinerFragment>();

        for (int32 i = 0; i < NumEntities; ++i)
        {
            FMassDspBuildingFragment& Building = BuildingFragments[i];
            FMassDspMinerFragment& Miner = MinerFragments[i];

            // 如果库存未满，继续生产
            if (Building.InventoryCount < Building.MaxInventory)
            {
                Miner.ProductionProgress += DeltaTime / (Miner.ProductionInterval > 0 ? Miner.ProductionInterval : 1.0f);
                if (Miner.ProductionProgress >= 1.0f)
                {
                    Building.InventoryCount++;
                    Miner.ProductionProgress -= 1.0f;
                }
            }
        }
    });

    // --- 2. 处理仓库存储逻辑 ---
    // 仓库本身不需要Tick处理，其InventoryCount由槽口输入逻辑更新
    // 这里可以添加其他仓库相关的逻辑，目前为空
    StorageQuery.ForEachEntityChunk(Context, [](FMassExecutionContext& Context)
    {
        // 仓库逻辑：目前不需要额外处理
        // InventoryCount 由槽口输入逻辑增加
    });

    // --- 3. 处理合成台合成逻辑 ---
    AssemblerQuery.ForEachEntityChunk(Context, [DeltaTime](FMassExecutionContext& Context)
    {
        const int32 NumEntities = Context.GetNumEntities();
        const TArrayView<FMassDspBuildingFragment> BuildingFragments = Context.GetMutableFragmentView<FMassDspBuildingFragment>();
        const TArrayView<FMassDspAssemblerFragment> AssemblerFragments = Context.GetMutableFragmentView<FMassDspAssemblerFragment>();

        for (int32 i = 0; i < NumEntities; ++i)
        {
            FMassDspBuildingFragment& Building = BuildingFragments[i];
            FMassDspAssemblerFragment& Assembler = AssemblerFragments[i];

            // TODO: 实现合成逻辑
            // 1. 检查输入缓冲区是否有足够的原料
            // 2. 更新合成进度
            // 3. 完成时从输入缓冲区消耗原料，向输出缓冲区添加产物 
        }
    });

    // --- 4. 处理槽口输入输出逻辑 ---
    SlotQuery.ForEachEntityChunk(Context, [this, GameMode](FMassExecutionContext& Context)
    {
        const int32 NumEntities = Context.GetNumEntities();
        const TArrayView<FMassDspBuildingFragment> Buildings = Context.GetMutableFragmentView<FMassDspBuildingFragment>();
        const TArrayView<FMassDspBuildingSlotsFragment> SlotsList = Context.GetMutableFragmentView<FMassDspBuildingSlotsFragment>();

        for (int32 i = 0; i < NumEntities; ++i)
        {
            FMassDspBuildingFragment& Building = Buildings[i];
            FMassDspBuildingSlotsFragment& SlotsData = SlotsList[i];

            // 遍历所有槽口
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
                    // 如果建筑库存未满
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
