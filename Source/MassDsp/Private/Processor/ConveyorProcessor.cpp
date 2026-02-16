#include "Processor/ConveyorProcessor.h"
#include "Fragments/BeltItemFragment.h"
#include "Subsystems/MassDspManager.h"
#include "MassDspBeltTypes.h"

#include "GameConst.h"
#include "MassCommonFragments.h"
#include "MassExecutionContext.h"
#include "MassCommonTypes.h"
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
    EntityQuery.RegisterWithProcessor(*this);
}

void UConveyorProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
    auto MassDspManager = Context.GetWorld()->GetSubsystem<UMassDspManager>();
    if (!MassDspManager || MassDspManager->BeltEntityRegistry.IsEmpty()) return;

    const float DeltaTime = Context.GetDeltaTimeSeconds();

    // --- 优化 A: 获取并缓存所有活跃车道句柄 ---
    TArray<FBeltHandle> ActiveBelts;
    MassDspManager->BeltEntityRegistry.GetKeys(ActiveBelts);

    // --- 优化 B: 并行处理车道 (ParallelFor) ---
    ParallelFor(ActiveBelts.Num(), [&](int32 BeltIdx)
    {
        const FBeltHandle& BeltHandle = ActiveBelts[BeltIdx];

        // 获取 Trajectory
        if (!MassDspManager->BeltTrajectories.IsValidIndex(BeltHandle.Index)) return;
        const FBeltTrajectory& Trajectory = MassDspManager->BeltTrajectories[BeltHandle.Index];
        if (!Trajectory.IsValid()) return;

        // 获取实体列表
        auto* BeltData = MassDspManager->BeltEntityRegistry.Find(BeltHandle);
        if (!BeltData || BeltData->Entities.IsEmpty()) return;

        float Speed = Trajectory.Speed;

        float LastItemTail = -1.0f;

        for (auto& Entity : BeltData->Entities)
        {
            FBeltItemFragment* Item = EntityManager.GetFragmentDataPtr<FBeltItemFragment>(Entity);
            FTransformFragment* Transform = EntityManager.GetFragmentDataPtr<FTransformFragment>(Entity);

            if (!Item || !Transform) continue;

            if (LastItemTail < 0)
            {
                LastItemTail = Trajectory.TotalLength - FGameConst::HalfLength;
            }

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

            FVector OutPos = Trajectory.GetLocationAtDistance(Item->DistanceAlongBelt);
            FVector OutTangent = Trajectory.GetTangentAtDistance(Item->DistanceAlongBelt);

            OutPos.Z += 20.f;

            FTransform& TargetTransform = Transform->GetMutableTransform();
            TargetTransform.SetLocation(OutPos);
            TargetTransform.SetRotation(OutTangent.Rotation().Quaternion());
        }
    });
}
