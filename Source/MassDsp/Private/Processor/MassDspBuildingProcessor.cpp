#include "Processor/MassDspBuildingProcessor.h"
#include "Fragments/MassDspBuildingSlotsFragment.h"
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

    auto GameMode = Cast<AMassDspGameMode>(World->GetAuthGameMode());
    if (!GameMode) return;

    if (!DspManager.IsValid())
    {
        DspManager = World->GetSubsystem<UMassDspManager>();
    }

    if (!DspManager.IsValid()) return;

    const float DeltaTime = Context.GetDeltaTimeSeconds();

    // --- 1. 处理矿机生产逻辑 + 输出槽口 ---
    MinerQuery.ForEachEntityChunk(Context, [this, DeltaTime, GameMode](FMassExecutionContext& InContext)
    {
        const int32 NumEntities = InContext.GetNumEntities();
        const TArrayView<FMassDspMinerFragment> MinerFragments = InContext.GetMutableFragmentView<FMassDspMinerFragment>();
        const TArrayView<FMassDspBuildingSlotsFragment> SlotsList = InContext.GetMutableFragmentView<FMassDspBuildingSlotsFragment>();

        for (int32 i = 0; i < NumEntities; ++i)
        {
            FMassDspMinerFragment& Miner = MinerFragments[i];
            FMassDspBuildingSlotsFragment& SlotsData = SlotsList[i];

            // 生产逻辑：如果库存未满，继续生产
            if (Miner.InventoryCount < Miner.MaxInventory)
            {
                Miner.ProductionProgress += DeltaTime / (Miner.ProductionInterval > 0 ? Miner.ProductionInterval : 1.0f);
                if (Miner.ProductionProgress >= 1.0f)
                {
                    Miner.InventoryCount++;
                    Miner.ProductionProgress -= 1.0f;
                }
            }

            // 槽口逻辑：只有库存有物品时才处理输出槽口
            if (Miner.InventoryCount > 0)
            {
                ProcessOutputSlots(SlotsData, Miner.InventoryCount, InContext, GameMode);
            }
        }
    });

    // --- 2. 处理仓库存储逻辑 + 输入槽口 ---
    StorageQuery.ForEachEntityChunk(Context, [this](FMassExecutionContext& InContext)
    {
        const int32 NumEntities = InContext.GetNumEntities();
        const TArrayView<FMassDspStorageFragment> StorageFragments = InContext.GetMutableFragmentView<FMassDspStorageFragment>();
        const TArrayView<FMassDspBuildingSlotsFragment> SlotsList = InContext.GetMutableFragmentView<FMassDspBuildingSlotsFragment>();

        for (int32 i = 0; i < NumEntities; ++i)
        {
            FMassDspStorageFragment& Storage = StorageFragments[i];
            FMassDspBuildingSlotsFragment& SlotsData = SlotsList[i];

            // 槽口逻辑：只有仓库未满时才处理输入槽口
            if (Storage.InventoryCount < Storage.MaxInventory)
            {
                ProcessInputSlots(SlotsData, Storage.InventoryCount, Storage.MaxInventory, InContext);
            }
            if (Storage.InventoryCount > 0)
            {
                ProcessOutputSlots(SlotsData, Storage.InventoryCount, InContext, Cast<AMassDspGameMode>(InContext.GetWorld()->GetAuthGameMode()));
            }
        }
    });

    // --- 3. 处理合成台合成逻辑 + 槽口 ---
    AssemblerQuery.ForEachEntityChunk(Context, [this, DeltaTime, GameMode](FMassExecutionContext& Context)
    {
        const int32 NumEntities = Context.GetNumEntities();
        const TArrayView<FMassDspAssemblerFragment> AssemblerFragments = Context.GetMutableFragmentView<FMassDspAssemblerFragment>();
        const TArrayView<FMassDspBuildingSlotsFragment> SlotsList = Context.GetMutableFragmentView<FMassDspBuildingSlotsFragment>();

        for (int32 i = 0; i < NumEntities; ++i)
        {
            FMassDspAssemblerFragment& Assembler = AssemblerFragments[i];
            FMassDspBuildingSlotsFragment& SlotsData = SlotsList[i];

            // TODO: 实现合成逻辑
            // 1. 检查输入缓冲区是否有足够的原料
            // 2. 更新合成进度
            // 3. 完成时从输入缓冲区消耗原料，向输出缓冲区添加产物

            Assembler.CraftingProgress += DeltaTime;
            if (Assembler.CraftingProgress >= 1.0f)
            {
                // 假设每次合成需要消耗1个输入缓冲区的物品，并生产1个输出缓冲区的物品
                if (Assembler.OutputBufferCount < Assembler.MaxInventory)
                {
                    Assembler.OutputBufferCount++;
                }
                Assembler.CraftingProgress -= 1.0f;
            }

            // 槽口逻辑：只有输出缓冲有物品时才处理输出槽口
            if (Assembler.OutputBufferCount > 0)
            {
                ProcessOutputSlots(SlotsData, Assembler.OutputBufferCount, Context, GameMode);
            }

            // 输入槽口逻辑：可以根据具体InputBuffers的状态来处理
            // 这里暂时使用OutputBufferCount作为示例
            if (Assembler.OutputBufferCount < Assembler.MaxInventory)
            {
                // ProcessInputSlots可以根据需要调整为处理合成台的特殊输入逻辑
            }
        }
    });
}

// 处理输出槽口的通用逻辑
void UMassDspBuildingProcessor::ProcessOutputSlots(
    FMassDspBuildingSlotsFragment& SlotsData, int32& InventoryCount, const FMassExecutionContext& Context, const AMassDspGameMode* GameMode
) const
{
    if (!DspManager.IsValid() || !GameMode) return;

    for (FBuildingSlotState& Slot : SlotsData.GetSlots())
    {
        if (!Slot.ConnectedLaneHandle.IsValid()) continue;

        if (Slot.Type == EBuildingSlotType::Output)
        {
            // 如果建筑有库存，且传送带口有位置
            if (InventoryCount > 0)
            {
                if (DspManager->ProvideItemToBelt(Context.Defer(), Slot.ConnectedLaneHandle, GameMode->BeltItemConfigAsset))
                {
                    InventoryCount--;
                }
            }
        }
    }
}

// 处理输入槽口的通用逻辑
void UMassDspBuildingProcessor::ProcessInputSlots(
    FMassDspBuildingSlotsFragment& SlotsData, int32& InventoryCount, int32 MaxInventory, const FMassExecutionContext& Context
) const
{
    if (!DspManager.IsValid()) return;

    for (FBuildingSlotState& Slot : SlotsData.GetSlots())
    {
        if (!Slot.ConnectedLaneHandle.IsValid()) continue;

        if (Slot.Type == EBuildingSlotType::Input)
        {
            // 如果建筑库存未满
            if (InventoryCount < MaxInventory)
            {
                if (DspManager->ConsumeItemFromBelt(Context.Defer(), Slot.ConnectedLaneHandle))
                {
                    InventoryCount++;
                }
            }
        }
    }
}
