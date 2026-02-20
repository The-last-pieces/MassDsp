#pragma once

#include "CoreMinimal.h"
#include "Components/SplineComponent.h"
#include "MassDspBeltTypes.generated.h"

struct FTransformFragment;

USTRUCT()
struct MASSDSP_API FBeltHandle
{
    GENERATED_BODY()

    UPROPERTY()
    int32 Index = -1;

    UPROPERTY()
    int32 Generation = 0;

    bool IsValid() const { return Index >= 0; }

    bool operator==(const FBeltHandle& Other) const
    {
        return Index == Other.Index && Generation == Other.Generation;
    }

    friend uint32 GetTypeHash(const FBeltHandle& Handle)
    {
        return HashCombine(GetTypeHash(Handle.Index), GetTypeHash(Handle.Generation));
    }
};

// 传送带轨迹数据封装
struct MASSDSP_API FBeltTrajectory
{
    // 我们持有 SplineComponent 的指针。
    // 注意：在 Mass Processor 多线程中访问 UObject 需要确保该 Object 不会被主线程修改或销毁。
    // MassDspManager 将负责管理这些 Component 的生命周期。
    USplineComponent* SplineComponent = nullptr;

    float TotalLength = 0.0f;

    float Speed = 400.0f; // 传送带速度，可以根据需要调整或从配置中读取

    bool IsValid() const;

    FVector GetLocationAtDistance(float Distance) const;

    FVector GetTangentAtDistance(float Distance) const;

    void ApplyTransform(FTransformFragment& Transform, float Distance) const;
};
