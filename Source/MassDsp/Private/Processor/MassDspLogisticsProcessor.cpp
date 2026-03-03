#include "Processor/MassDspLogisticsProcessor.h"

#include "MassExecutionContext.h"
#include "MassCommonTypes.h"
#include "MassEntityManager.h"
#include "MassCommonFragments.h"   // FTransformFragment

#include "Fragments/MassDspLogisticsTowerFragment.h"
#include "Fragments/MassDspStorageFragment.h"
#include "Fragments/MassDspAssemblerFragment.h"
#include "Fragments/MassDspBuildingSlotsFragment.h"

#include "Subsystems/MassDspManager.h"
#include "Subsystems/MassDspLogisticsSubsystem.h"

UMassDspLogisticsProcessor::UMassDspLogisticsProcessor()
    : TowerQuery(*this)
{
    ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::SyncWorldToMass;
    bRequiresGameThreadExecution  = true;
}

void UMassDspLogisticsProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
    // 物流塔实体 = 同时拥有这三种 Fragment（由 AMassDspLogisticsTower::GetStaticStructs 保证）
    TowerQuery.AddRequirement<FMassDspLogisticsTowerFragment>(EMassFragmentAccess::ReadWrite);
    TowerQuery.AddRequirement<FMassDspStorageFragment>(EMassFragmentAccess::ReadOnly);
    TowerQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
    TowerQuery.RegisterWithProcessor(*this);
}

void UMassDspLogisticsProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
    UWorld* World = EntityManager.GetWorld();
    if (!World) return;

    UMassDspLogisticsSubsystem* Logistics = GetLogisticsSubsystem(World);
    UMassDspManager*            DspMgr    = GetDspManager(World);
    if (!Logistics || !DspMgr) return;

    const float Now = World->GetTimeSeconds();

    TowerQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& InContext)
    {
        const int32 NumEntities = InContext.GetNumEntities();
        const TArrayView<FMassDspLogisticsTowerFragment> TowerFrags =
            InContext.GetMutableFragmentView<FMassDspLogisticsTowerFragment>();
        const TConstArrayView<FMassDspStorageFragment> StorageFrags =
            InContext.GetFragmentView<FMassDspStorageFragment>();
        const TConstArrayView<FTransformFragment> Transforms =
            InContext.GetFragmentView<FTransformFragment>();

        for (int32 i = 0; i < NumEntities; ++i)
        {
            FMassDspLogisticsTowerFragment& TowerFrag = TowerFrags[i];

            //  事件推送已覆盖（bDirty）则跳过兜底扫描 
            if (TowerFrag.bDirty) continue;

            //  兜底间隔未到 
            if (Now - TowerFrag.LastScanTime < TowerFrag.ScanInterval) continue;

            TowerFrag.LastScanTime = Now;

            const FVector TowerLocation = Transforms[i].GetTransform().GetLocation();

            //  扫描覆盖范围内的建筑 
            TArray<FMassEntityHandle> NearbyEntities;
            DspMgr->FindBuildingsInRadius(TowerLocation, TowerFrag.CoverageRadius, NearbyEntities);

            const FMassEntityHandle TowerEntity = InContext.GetEntity(i);

            for (const FMassEntityHandle& NearbyEntity : NearbyEntities)
            {
                if (NearbyEntity == TowerEntity) continue; // 跳过自身

                //  Storage：库存过满  Supply 请求 
                if (const FMassDspStorageFragment* NearbyStorage =
                        EntityManager.GetFragmentDataPtr<FMassDspStorageFragment>(NearbyEntity))
                {
                    if (NearbyStorage->MaxInventory > 0)
                    {
                        const float FillRatio = static_cast<float>(NearbyStorage->InventoryCount)
                                              / static_cast<float>(NearbyStorage->MaxInventory);
                        // 默认阈值 80%（TODO: 从塔的 CDO 配置读取 SupplyTriggerRatio）
                        if (FillRatio >= 0.8f && NearbyStorage->StoredItemType != EItemType::None)
                        {
                            // 去重：子系统内部按 SourceEntity + ItemType 检查已有 Pending 请求
                            Logistics->SubmitSupplyRequest(
                                NearbyEntity,
                                NearbyStorage->StoredItemType,
                                NearbyStorage->InventoryCount / 2, // 搬走一半
                                ELogisticsRequestPriority::Normal,
                                TowerEntity);
                        }
                    }
                }

                //  Assembler：输入缓冲不足  Demand 请求 
                if (const FMassDspAssemblerFragment* NearbyAssembler =
                        EntityManager.GetFragmentDataPtr<FMassDspAssemblerFragment>(NearbyEntity))
                {
                    // TODO[ASSEMBLER]: 检查每个 InputBuffer 缺口，按配方要求提交 Demand 请求
                    // 示例（待完整实现）：
                    // for (int32 SlotIdx = 0; SlotIdx < NearbyAssembler->GetInputBufferCount(); ++SlotIdx)
                    // {
                    //     auto [ItemType, Current, Max] = NearbyAssembler->GetInputBufferInfo(SlotIdx);
                    //     if (static_cast<float>(Current) / Max < 0.2f)
                    //         Logistics->SubmitDemandRequest(NearbyEntity, ItemType, Max - Current,
                    //             ELogisticsRequestPriority::Normal, TowerEntity);
                    // }
                    (void)NearbyAssembler;
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

UMassDspManager* UMassDspLogisticsProcessor::GetDspManager(UWorld* World)
{
    if (!DspManager.IsValid())
        DspManager = World->GetSubsystem<UMassDspManager>();
    return DspManager.Get();
}
