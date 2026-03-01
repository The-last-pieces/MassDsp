#pragma once

#include "CoreMinimal.h"
#include "Components/SplineComponent.h"
#include "GameConst.h"

#include "Containers/Deque.h"

#include "MassDspBeltTypes.generated.h"

struct FTransformFragment;

// 传送带物品的逻辑数据，连续内存存储，不依赖 Mass Entity
struct MASSDSP_API FBeltItemCache
{
    float DistanceAlongBelt = 0.0f;
    bool bIsBlocked = false;
    EItemType ItemType = EItemType::None;
};

// 传送带逻辑数据（替代 FBeltEntityArray）
struct MASSDSP_API FBeltData
{
    // 按传送带顺序排列的物品缓存，连续内存，Cache 友好
    TDeque<FBeltItemCache> ItemCache;
    float BeltLength = 0.0f;
    float BeltSpeed = 0.0f;
};

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

// 预烘焙的 Spline 采样点，用于 O(1) 查表插值（取代每帧调用 USplineComponent）
struct MASSDSP_API FBeltLUTSample
{
    FVector Position  = FVector::ZeroVector;
    FQuat   Rotation  = FQuat::Identity;
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

    // 预烘焙 LUT：传送带创建后调用 BakeLUT() 一次，之后不再访问 SplineComponent
    TArray<FBeltLUTSample> LUT;
    float LUTStep = 50.0f; // LUT 采样间距（单位：cm），50cm 误差 < 0.5cm

    /** 传送带中点世界坐标（BakeLUT 时计算），用于每帧 O(1) 摄像机距离判断 */
    FVector RepresentativePosition = FVector::ZeroVector;

    /**
     * 将 SplineComponent 预烘焙为离散采样表，之后 GetTransformAtDistance 用此表插值。
     * 必须在 SplineComponent 完成初始化且 TotalLength 已赋值后调用一次。
     * @param Step  采样间距（cm），越小精度越高但内存越大，50cm 通常足够
     */
    void BakeLUT(float Step = 50.0f);

    bool IsValid() const;

    FVector GetLocationAtDistance(float Distance) const;

    FVector GetTangentAtDistance(float Distance) const;

    void ApplyTransform(FTransformFragment& Transform, float Distance) const;

    // 直接计算 FTransform，不依赖 FTransformFragment（供 ISM 批量更新使用）
    // 优先从 LUT 查表，LUT 为空时 fallback 到 SplineComponent
    void GetTransformAtDistance(float Distance, FTransform& OutTransform) const;
};
