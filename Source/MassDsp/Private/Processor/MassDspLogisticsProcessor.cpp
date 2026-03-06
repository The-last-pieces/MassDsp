#include "Processor/MassDspLogisticsProcessor.h"

#include "MassExecutionContext.h"
#include "MassCommonTypes.h"
#include "MassEntityManager.h"

#include "Fragments/MassDspLogisticsTowerFragment.h"
#include "Fragments/MassDspStorageFragment.h"

#include "Subsystems/MassDspLogisticsSubsystem.h"

UMassDspLogisticsProcessor::UMassDspLogisticsProcessor()
    : TowerQuery(*this)
{
    ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::SyncWorldToMass;
    bRequiresGameThreadExecution  = true;
}

void UMassDspLogisticsProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
    // 物流塔实体 = 同时拥有这两种 Fragment（由 AMassDspLogisticsTower::GetStaticStructs 保证）
    TowerQuery.AddRequirement<FMassDspLogisticsTowerFragment>(EMassFragmentAccess::ReadWrite);
    TowerQuery.AddRequirement<FMassDspStorageFragment>(EMassFragmentAccess::ReadOnly);
    TowerQuery.RegisterWithProcessor(*this);
}

void UMassDspLogisticsProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
    UWorld* World = EntityManager.GetWorld();
    if (!World) return;

    UMassDspLogisticsSubsystem* Logistics = GetLogisticsSubsystem(World);
    if (!Logistics) return;

    const float Now = World->GetTimeSeconds();

    TowerQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& InContext)
    {
        const int32 NumEntities = InContext.GetNumEntities();
        const TArrayView<FMassDspLogisticsTowerFragment> TowerFrags =
            InContext.GetMutableFragmentView<FMassDspLogisticsTowerFragment>();
        const TConstArrayView<FMassDspStorageFragment> StorageFrags =
            InContext.GetFragmentView<FMassDspStorageFragment>();

        for (int32 i = 0; i < NumEntities; ++i)
        {
            FMassDspLogisticsTowerFragment& TowerFrag = TowerFrags[i];
            const FMassDspStorageFragment&  SelfStorage = StorageFrags[i];

            // DSP 风格：模式为 Storage 则不参与调度
            if (TowerFrag.TowerMode == ELogisticsTowerMode::Storage) continue;

            // 未配置物品类型则跳过
            if (TowerFrag.ItemType == EItemType::None) continue;

            // 不接受请求则跳过
            if (!TowerFrag.bAcceptsRequests) continue;

            // 事件推送已覆盖（bDirty）则跳过兜底扫描
            if (TowerFrag.bDirty) continue;

            // 兜底间隔未到
            if (Now - TowerFrag.LastScanTime < TowerFrag.ScanInterval) continue;

            TowerFrag.LastScanTime = Now;

            const FMassEntityHandle TowerEntity = InContext.GetEntity(i);
            const int32 InventoryCount = SelfStorage.InventoryCount;
            const int32 Threshold      = TowerFrag.RequestThreshold;

            // 调度语义：只有需求塔主动发出请求，供应塔被动等候调度系统查询。
            // RequestThreshold 对供应塔仅作「本地储备下限」（用户手动配置），无调度意义。
            if (TowerFrag.TowerMode == ELogisticsTowerMode::Demand)
            {
                // Demand 模式：
                //   触发条件 — 库存低于 RequestThreshold
                //   请求数量 — 填满至 MaxInventory（调动足够多无人机）
                if (InventoryCount < Threshold)
                {
                    const int32 InTransitTo = Logistics->ComputeInTransitToEntity(TowerEntity);
                    const int32 WantQty = SelfStorage.MaxInventory - InventoryCount - InTransitTo;
                    if (WantQty > 0)
                    {
                        Logistics->SubmitDemandRequest(
                            TowerEntity,
                            TowerFrag.ItemType,
                            WantQty,
                            ELogisticsRequestPriority::Normal,
                            TowerEntity);
                    }
                }
            }
        }
    });
}

UMassDspLogisticsSubsystem* UMassDspLogisticsProcessor::GetLogisticsSubsystem(UWorld* World)
{
    if (!LogisticsSubsystem.IsValid())
        LogisticsSubsystem = World->GetSubsystem<UMassDspLogisticsSubsystem>();
    return LogisticsSubsystem.Get();
}
