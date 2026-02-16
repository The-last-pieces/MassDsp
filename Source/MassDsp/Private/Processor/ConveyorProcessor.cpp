// Fill out your copyright notice in the Description page of Project Settings.


#include "Processor/ConveyorProcessor.h"
#include "Fragments/BeltItemFragment.h"
#include "Subsystems/MassDspManager.h"

#include "MassCommonFragments.h"
#include "MassExecutionContext.h"
#include "MassProcessingPhaseManager.h"
#include "MassCommonTypes.h"
#include "ZoneGraphSubsystem.h"
#include "ZoneGraphQuery.h"
#include "MassZoneGraphNavigationFragments.h"
#include "MassActorSubSystem.h"
#include "MassRepresentationSubsystem.h"
#include "Kismet/GameplayStatics.h"
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
    // 修改为 ReadWrite 以便更新缓存
    EntityQuery.AddRequirement<FMassZoneGraphCachedLaneFragment>(EMassFragmentAccess::ReadWrite);
    EntityQuery.AddRequirement<FMassZoneGraphLaneLocationFragment>(EMassFragmentAccess::ReadOnly);
    EntityQuery.RegisterWithProcessor(*this);
}

// 优化后的轻量级结构体，用于排序和逻辑计算
struct FBeltWorkerData
{
    FMassEntityHandle Entity;
    float Distance = 0.0f;
    float HalfLength = 0.0f;
    FBeltItemFragment* ItemFrag = nullptr;
    FTransformFragment* TransFrag = nullptr;
    // 去掉 const，允许修改
    FMassZoneGraphCachedLaneFragment* CachedLaneFrag = nullptr; 
};

void UConveyorProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
    auto ZoneGraphSubsystem = Context.GetWorld()->GetSubsystem<UZoneGraphSubsystem>();
    auto MassDspManager = Context.GetWorld()->GetSubsystem<UMassDspManager>();
    if (!ZoneGraphSubsystem || !MassDspManager || MassDspManager->LaneRegistry.Num() == 0) return;

    const float DeltaTime = Context.GetDeltaTimeSeconds();
    const float Speed = 400.0f;
    const float MinSpacing = 20.0f;

    // --- 优化 A: 获取并缓存所有活跃车道句柄 ---
    TArray<FZoneGraphLaneHandle> ActiveLanes;
    MassDspManager->LaneRegistry.GetKeys(ActiveLanes);

    // --- 优化 B: 并行处理车道 (ParallelFor) ---
    ParallelFor(ActiveLanes.Num(), [&](int32 LaneIdx) {
        const FZoneGraphLaneHandle& LaneHandle = ActiveLanes[LaneIdx];

        // 由于 TMap 的 Find 在不修改 Map 时是线程安全的
        auto* LaneData = MassDspManager->LaneRegistry.Find(LaneHandle);
        if (!LaneData || LaneData->Entities.Num() == 0) return;

        const FZoneGraphStorage* ZoneStorage = ZoneGraphSubsystem->GetZoneGraphStorage(LaneHandle.DataHandle);
        if (!ZoneStorage) return;

        TArray<FMassEntityHandle>& Entities = LaneData->Entities;
        float LastItemTail = -1.0f;

        for (int32 i = 0; i < Entities.Num(); ++i)
        {
            FMassEntityHandle Entity = Entities[i];

            // --- 优化 C: 查表虽然有开销，但在并行中分摊了 CPU 压力 ---
            FBeltItemFragment* Item = EntityManager.GetFragmentDataPtr<FBeltItemFragment>(Entity);
            FTransformFragment* Transform = EntityManager.GetFragmentDataPtr<FTransformFragment>(Entity);
            FMassZoneGraphCachedLaneFragment* CachedLane = EntityManager.GetFragmentDataPtr<FMassZoneGraphCachedLaneFragment>(Entity);

            // 如果实体在此时被异步销毁，需过滤
            if (!Item || !Transform || !CachedLane) continue;

            if (i == 0) {
                LastItemTail = CachedLane->LaneLength;
            }

            // --- 核心更新逻辑 ---
            float DesiredDistance = Item->DistanceAlongBelt + Speed * DeltaTime;

            if (DesiredDistance > LastItemTail)
            {
                Item->DistanceAlongBelt = FMath::Max(Item->DistanceAlongBelt, LastItemTail);
                Item->bIsBlocked = true;
            }
            else
            {
                Item->DistanceAlongBelt = DesiredDistance;
                Item->bIsBlocked = false;
            }

            LastItemTail = Item->DistanceAlongBelt - (Item->HalfLength * 2) - MinSpacing;

            // --- 优化 D: 缓存更新控制 ---
            // 只有当距离变化时才更新缓存。InflateDistance 的设置应略大于 Agent 半径
            const float Inflate = Item->HalfLength * 3.f + MinSpacing;
            CachedLane->CacheLaneData(*ZoneStorage, LaneHandle, Item->DistanceAlongBelt, CachedLane->LaneLength, Inflate);

            FVector OutPos, OutTangent;
            CachedLane->GetPointAndTangentAtDistance(Item->DistanceAlongBelt, OutPos, OutTangent);
            OutPos.Z += 20.f;

            FTransform& TargetTransform = Transform->GetMutableTransform();
            TargetTransform.SetLocation(OutPos);
            TargetTransform.SetRotation(OutTangent.Rotation().Quaternion());
        }
        });
}
