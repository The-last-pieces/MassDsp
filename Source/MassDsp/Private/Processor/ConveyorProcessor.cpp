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

//void UConveyorProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
//{
//    // 获取 ZoneGraphSubsystem 用于检索数据存储
//    UZoneGraphSubsystem* ZoneGraphSubsystem = Context.GetWorld()->GetSubsystem<UZoneGraphSubsystem>();
//    if (!ZoneGraphSubsystem) return;
//
//    // 1. 分组数据准备 (Key 是 LaneHandle, Value 是该车道上的物体列表)
//    // 使用 TMap 缓存，建议将此 Map 定义为类成员以复用内存，避免每帧分配
//    TMap<FZoneGraphLaneHandle, TArray<FBeltWorkerData>> LaneGroups;
//
//    EntityQuery.ForEachEntityChunk(Context, ([&LaneGroups](FMassExecutionContext& QueryContext) {
//        const int32 EntityCount = QueryContext.GetNumEntities();
//        auto ItemList = QueryContext.GetMutableFragmentView<FBeltItemFragment>();
//        auto TransList = QueryContext.GetMutableFragmentView<FTransformFragment>();
//        auto LaneLocList = QueryContext.GetFragmentView<FMassZoneGraphLaneLocationFragment>();
//        auto CachedLaneList = QueryContext.GetMutableFragmentView<FMassZoneGraphCachedLaneFragment>();
//
//        for (int32 i = 0; i < EntityCount; ++i)
//        {
//            FZoneGraphLaneHandle Lane = LaneLocList[i].LaneHandle;
//            FBeltWorkerData Data;
//            Data.Entity = QueryContext.GetEntity(i);
//            Data.Distance = ItemList[i].DistanceAlongBelt;
//            Data.HalfLength = ItemList[i].HalfLength;
//            Data.ItemFrag = &ItemList[i];
//            Data.TransFrag = &TransList[i];
//            Data.CachedLaneFrag = &CachedLaneList[i];
//
//            LaneGroups.FindOrAdd(Lane).Add(Data);
//        }
//        }));
//
//    // 2. 将分组结果转为数组，方便 ParallelFor 处理
//    TArray<FZoneGraphLaneHandle> ActiveLanes;
//    LaneGroups.GetKeys(ActiveLanes);
//
//    // 3. 并行处理每一条车道 (ParallelFor)
//    const float DeltaTime = Context.GetDeltaTimeSeconds();
//    const float Speed = 300.0f;
//    const float MinSpacing = 10.0f;
//
//    ParallelFor(ActiveLanes.Num(), [&](int32 LaneIdx) {
//        FZoneGraphLaneHandle LaneHandle = ActiveLanes[LaneIdx];
//        
//        // 关键点：每条车道只获取一次存储指针
//        const FZoneGraphStorage* ZoneStorage = ZoneGraphSubsystem->GetZoneGraphStorage(LaneHandle.DataHandle);
//        if (!ZoneStorage) return;
//
//        TArray<FBeltWorkerData>& ItemsOnLane = LaneGroups[LaneHandle];
//
//        // 只对这一条车道内的物体进行排序 (N 非常小，排序极快)
//        ItemsOnLane.Sort([](const FBeltWorkerData& A, const FBeltWorkerData& B) {
//            return A.Distance > B.Distance;
//            });
//
//        float LastItemTail = ItemsOnLane[0].CachedLaneFrag->LaneLength;
//
//        for (FBeltWorkerData& Data : ItemsOnLane)
//        {
//            // 策略点：使用较大的 InflateDistance (如 500单位) 减少真正的重采样发生频率
//            // 对于传送带，TargetDistance 可以直接设为 LaneLength (车道总长)
//            constexpr float InflateDistance = 1000.0f;
//            Data.CachedLaneFrag->CacheLaneData(*ZoneStorage, LaneHandle, 
//                Data.Distance, Data.CachedLaneFrag->LaneLength, InflateDistance);
//
//            // 业务逻辑计算
//            float DesiredDistance = Data.Distance + Speed * DeltaTime;
//            float MaxPos = LastItemTail - Data.HalfLength - MinSpacing;
//
//            if (DesiredDistance > MaxPos)
//            {
//                Data.ItemFrag->DistanceAlongBelt = FMath::Max(Data.Distance, MaxPos);
//                Data.ItemFrag->bIsBlocked = true;
//            }
//            else
//            {
//                Data.ItemFrag->DistanceAlongBelt = DesiredDistance;
//                Data.ItemFrag->bIsBlocked = false;
//            }
//
//            LastItemTail = Data.ItemFrag->DistanceAlongBelt - Data.HalfLength;
//
//            // 更新 Transform
//            FVector OutPos, OutTangent;
//            Data.CachedLaneFrag->GetPointAndTangentAtDistance(Data.ItemFrag->DistanceAlongBelt, OutPos, OutTangent);
//            OutPos.Z += 20.f;
//
//            FTransform& TargetTransform = Data.TransFrag->GetMutableTransform();
//            TargetTransform.SetLocation(OutPos);
//            TargetTransform.SetRotation(OutTangent.Rotation().Quaternion());
//        }
//        }, EParallelForFlags::None); // 如果物体极多，开启此并行
//}

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
