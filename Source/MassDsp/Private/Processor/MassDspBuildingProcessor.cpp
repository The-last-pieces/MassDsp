#include "Processor/MassDspBuildingProcessor.h"
#include "Fragments/MassDspBuildingFragment.h"
#include "Fragments/BeltItemFragment.h"
#include "Subsystems/MassDspManager.h"
#include "ZoneGraphSubsystem.h"
#include "MassCommonFragments.h"
#include "MassExecutionContext.h"
#include "MassCommonTypes.h"
#include "Actors/MassDspMiner.h"
#include "Actors/MassDspStorage.h"

UMassDspBuildingProcessor::UMassDspBuildingProcessor(): BuildingQuery(*this)
{
	// 设置处理器执行顺序
    ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::SyncWorldToMass;
	
    // 自动注册，不需要手动添加
	bAutoRegisterWithProcessingPhases = true;
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

    if (!DspManager.IsValid())
    {
        DspManager = World->GetSubsystem<UMassDspManager>();
    }
    if (!ZoneGraphSubsystem.IsValid())
    {
        ZoneGraphSubsystem = World->GetSubsystem<UZoneGraphSubsystem>();
    }

    if (!DspManager.IsValid() || !ZoneGraphSubsystem.IsValid()) return;

    // 遍历所有建筑实体
	BuildingQuery.ForEachEntityChunk(EntityManager, Context, [this, World](FMassExecutionContext& Context)
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
                // 矿机逻辑：持续生产
                // 简单起见，我们直接增加 InventoryCount，直到满
                if (Building.InventoryCount < Building.MaxInventory)
                {
                    Building.ProductionProgress += DeltaTime / MinerActor->ProductionInterval;
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
                // 如果槽口未连接且未检测过，尝试寻找最近的传送带
                // 这里的检测逻辑比较简陋，实际应该使用更高效的空间查询(HashGrid等)
                // 或者在建造传送带时建立连接
                if (!Slot.bConnected && !Slot.ConnectedLaneHandle.IsValid())
                {
                    // 简单的寻找最近 Lane 逻辑 (性能较差，仅作演示，实际应在建造时完成连接)
                    // TODO: 移至 OnBuildingPlaced 或 OnBeltPlaced 事件处理
                    /* 
                    FZoneGraphTagMask TagMask = FZoneGraphTagMask::All;
                    float SearchRadius = 100.0f; 
                    // 需要调用 ZoneGraphSubsystem->FindNearestLane(...) 
                    // 但该API通常需要 Box 或 Sphere 查询
                    */
                    
                    // 暂时跳过自动寻找，假设在 GameMode 中或 Manager 中已经建立连接
                    // 为了演示，我们暂时不做运行时自动连接
                    continue; 
                }

                if (!Slot.ConnectedLaneHandle.IsValid()) continue;

                // --- 输出槽口逻辑 (Building -> Belt) ---
                if (Slot.Type == EBuildingSlotType::Output)
                {
                    // 如果建筑有库存，且传送带口有位置
                    if (Building.InventoryCount > 0)
                    {
                        // 检查车道是否拥堵
                        // 我们需要访问 Manager 的 LaneRegistry 来查看车道上的物品
                        if (FBeltEntityArray* BeltItems = DspManager->LaneRegistry.Find(Slot.ConnectedLaneHandle))
                        {
                            bool bLaneBlocked = false;
                            // 检查最近的物品是否阻挡了入口 (Distance ~ 0)
                            float MinDist = 99999.0f;
                            for (const FMassEntityHandle& ItemHandle : BeltItems->Entities)
                            {
                                // 这里需要获取 Item 的位置信息。
                                // 由于在 Processor 中直接获取其他实体的 Fragment 数据比较慢（随机访问），
                                // 理想做法是分开处理或利用 Tag。
                                // 这里为了演示，我们假设如果车道上有物体且距离很近则阻塞。
                                // 实际需优化：通过 LaneRegistry 缓存头部和尾部距离
                            }
                            
                             // 简化逻辑：直接调用 Manager 尝试生成
                             if (DspManager->SpawnItemOnLane(Slot.ConnectedLaneHandle, 0.0f, Context.Defer()))
                             {
                                 Building.InventoryCount--;
                             }
                        }
                    }
                }
                
                // --- 输入槽口逻辑 (Belt -> Building) ---
                else if (Slot.Type == EBuildingSlotType::Input)
                {
                    // 如果仓库未满
                    if (Building.InventoryCount < Building.MaxInventory)
                    {
                         // 检查车道末端是否有物品
                         // 同样需要访问 LaneRegistry
                         // 简化逻辑：调用 Manager 尝试消耗末端物品
                         if (DspManager->ConsumeItemFromLane(Slot.ConnectedLaneHandle, Context.Defer()))
                         {
                             Building.InventoryCount++;
                         }
                    }
                }
            }
		}
	});
}
