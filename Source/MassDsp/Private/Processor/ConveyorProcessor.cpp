#include "Processor/ConveyorProcessor.h"
#include "Fragments/BeltItemFragment.h"
#include "Subsystems/MassDspManager.h"

#include "GameConst.h"
#include "MassCommonFragments.h"
#include "MassExecutionContext.h"
#include "MassCommonTypes.h"
#include "ZoneGraphSubsystem.h"
#include "MassZoneGraphNavigationFragments.h"
#include "Async/ParallelFor.h"

#include <algorithm>

UConveyorProcessor::UConveyorProcessor() : EntityQuery(*this)
{
    bRequiresGameThreadExecution = false;
    ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::Movement;
}

void UConveyorProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
    EntityQuery.AddRequirement<FBeltItemFragment>(EMassFragmentAccess::ReadWrite);
    EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
    EntityQuery.AddRequirement<FMassZoneGraphCachedLaneFragment>(EMassFragmentAccess::ReadWrite);
    EntityQuery.AddRequirement<FMassZoneGraphLaneLocationFragment>(EMassFragmentAccess::ReadOnly);
    EntityQuery.RegisterWithProcessor(*this);
}

void UConveyorProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
    auto ZoneGraphSubsystem = Context.GetWorld()->GetSubsystem<UZoneGraphSubsystem>();
    auto MassDspManager = Context.GetWorld()->GetSubsystem<UMassDspManager>();
    if (!ZoneGraphSubsystem || !MassDspManager || MassDspManager->LaneRegistry.IsEmpty()) return;

    const float DeltaTime = Context.GetDeltaTimeSeconds();
    constexpr float Speed = 400.0f; // TODO 改成传送带变量

    // --- 优化 A: 获取并缓存所有活跃车道句柄 ---
    TArray<FZoneGraphLaneHandle> ActiveLanes;
    MassDspManager->LaneRegistry.GetKeys(ActiveLanes);

    // --- 优化 B: 并行处理车道 (ParallelFor) ---
    ParallelFor(ActiveLanes.Num(), [&](int32 LaneIdx)
    {
        const FZoneGraphLaneHandle& LaneHandle = ActiveLanes[LaneIdx];

        // 由于 TMap 的 Find 在不修改 Map 时是线程安全的
        auto* LaneData = MassDspManager->LaneRegistry.Find(LaneHandle);
        if (!LaneData || LaneData->Entities.IsEmpty()) return;

        const FZoneGraphStorage* ZoneStorage = ZoneGraphSubsystem->GetZoneGraphStorage(LaneHandle.DataHandle);
        if (!ZoneStorage) return;

        float LastItemTail = -1.0f;

        for (auto& Entity : LaneData->Entities)
        {
            // --- 优化 C: 查表虽然有开销，但在并行中分摊了 CPU 压力 ---
            FBeltItemFragment* Item = EntityManager.GetFragmentDataPtr<FBeltItemFragment>(Entity);
            FTransformFragment* Transform = EntityManager.GetFragmentDataPtr<FTransformFragment>(Entity);
            FMassZoneGraphCachedLaneFragment* CachedLane = EntityManager.GetFragmentDataPtr<FMassZoneGraphCachedLaneFragment>(Entity);

            // 如果实体在此时被异步销毁，需过滤
            if (!Item || !Transform || !CachedLane) continue;

            constexpr float Inflate = FGameConst::HalfLength * 3.f + FGameConst::MinSpacing;
            CachedLane->CacheLaneData(*ZoneStorage, LaneHandle, Item->DistanceAlongBelt, Item->DistanceAlongBelt + Speed * 2, Inflate);

            if (LastItemTail < 0)
            {
                LastItemTail = CachedLane->LaneLength - FGameConst::HalfLength;
            }

            // --- 核心更新逻辑 ---
            if (float DesiredDistance = Item->DistanceAlongBelt + Speed * DeltaTime; DesiredDistance > LastItemTail)
            {
                Item->DistanceAlongBelt = LastItemTail;
                Item->bIsBlocked = true;
            }
            else
            {
                Item->DistanceAlongBelt = DesiredDistance;
                Item->bIsBlocked = false;
            }

            LastItemTail = Item->DistanceAlongBelt - (FGameConst::HalfLength * 2) - FGameConst::MinSpacing;

            FVector OutPos, OutTangent;
            CachedLane->GetPointAndTangentAtDistance(Item->DistanceAlongBelt, OutPos, OutTangent);
            OutPos.Z += 20.f;

            FTransform& TargetTransform = Transform->GetMutableTransform();
            TargetTransform.SetLocation(OutPos);
            TargetTransform.SetRotation(OutTangent.Rotation().Quaternion());
        }

        // TStringBuilder<1000> Builder;
        // // log打印所有distance
        // for (auto& Entity : LaneData->Entities)
        // {
        //     auto Ptr = EntityManager.GetFragmentDataPtr<FBeltItemFragment>(Entity);
        //     auto Ptr2 = EntityManager.GetFragmentDataPtr<FTransformFragment>(Entity);
        //     if (Ptr && Ptr2)
        //     {
        //         auto Loc = Ptr2->GetTransform().GetLocation();
        //         Builder.Appendf(TEXT("%f:"), Ptr->DistanceAlongBelt);
        //         Builder.Appendf(TEXT("%f"), Loc.X);
        //         Builder.Append(";");
        //     }
        // }
        // UE_LOG(LogTemp, Warning, TEXT("%s"), Builder.ToString());
    });
}
