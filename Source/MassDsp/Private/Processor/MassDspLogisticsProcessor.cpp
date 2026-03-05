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

            if (TowerFrag.TowerMode == ELogisticsTowerMode::Supply)
            {
                // Supply 模式：库存超过阈值 → 提交供货请求
                // 以塔自身为取货源，PreferredTower 也指向自身，全局匹配逻辑会跟同类型 Demand 匹配
                if (InventoryCount > Threshold)
                {
                    const int32 SendQty = InventoryCount - Threshold;
                    Logistics->SubmitSupplyRequest(
                        TowerEntity,
                        TowerFrag.ItemType,
                        SendQty,
                        ELogisticsRequestPriority::Normal,
                        TowerEntity);
                }
            }
            else if (TowerFrag.TowerMode == ELogisticsTowerMode::Demand)
            {
                // Demand 模式：库存低于阈值 → 提交补货请求
                if (InventoryCount < Threshold)
                {
                    const int32 WantQty = Threshold - InventoryCount;
                    Logistics->SubmitDemandRequest(
                        TowerEntity,
                        TowerFrag.ItemType,
                        WantQty,
                        ELogisticsRequestPriority::Normal,
                        TowerEntity);
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
