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
    FVector Position = FVector::ZeroVector;
    FQuat Rotation = FQuat::Identity;
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

    /** 传送带中点世界坐标（BakeLUT 时计算），用于每帧视锥剔除 */
    FVector RepresentativePosition = FVector::ZeroVector;

    /** 围绕 RepresentativePosition 的包围球半径（BakeLUT 时计算），视锥剔除用 IntersectSphere 测试整条传送带 */
    float BoundRadius = 0.f;

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

// ============================================================
//  传送带样条类型
// ============================================================

/** 样条生成算法枚举 */
UENUM(BlueprintType)
enum class EBeltSplineType : uint8
{
    /** Hermite 4点样条（默认）*/
    Spline = 0 UMETA(DisplayName = "Hermite样条"),
    /** Dubins 最短路径 + Z 轴线性插值 */
    DubinsPath = 1 UMETA(DisplayName = "Dubins最短路径"),

    Default = DubinsPath UMETA(Hidden)
};

/** Dubins 路径最小转弯半径（cm）*/
static constexpr float DubinsMinTurningRadius = 300.f;

/** Dubins 路径词型（三段组合方式）*/
enum class EDubinsWordType : uint8
{
    LSL, LSR, RSL, RSR, LRL, RLR, Invalid
};

/** Dubins 段类型 */
enum class EDubinsSegType : uint8
{
    Left, ///< CCW 圆弧
    Straight, ///< 直线
    Right, ///< CW  圆弧
};

/**
 * Dubins 路径2D 计算结枚封装。
 * 存储最短賓型、三段实际长度（cm）以及重建效线所需的全部输入数据。
 */
struct MASSDSP_API FDubinsPathData
{
    /** 最优词型 */
    EDubinsWordType WordType = EDubinsWordType::Invalid;

    /** 三段实际长度（cm）：圆弧段 = 弧长，直线段 = 直线长 */
    float SegLen[3] = {0.f, 0.f, 0.f};

    /** 路径总长度（cm）*/
    float TotalLength = 0.f;

    /** 最小转弯半径（cm）*/
    float TurningRadius = DubinsMinTurningRadius;

    /** 2D 起始位置（世界 XY）*/
    FVector2D StartPos = FVector2D::ZeroVector;
    /** 起始朝向（rad，从 +X 轴逆时针为正）*/
    float StartHeading = 0.f;

    /** 2D 终点位置（世界 XY）*/
    FVector2D EndPos = FVector2D::ZeroVector;
    /** 终点朝向（rad）*/
    float EndHeading = 0.f;

    /** 3D 起点 Z 坐标（用于 Z 轴线性插岜）*/
    float StartZ = 0.f;
    /** 3D 终点 Z 坐标 */
    float EndZ = 0.f;

    /**
     * 起点槽口延伸（SlotExtend）3D 位置（即 A 点 = B - SlotExtend * forward）。
     * 若有效，BuildBeltSplineFromDubins 会在 Dubins 曲线前插入 A→B 直线段。
     */
    bool bHasStartExtend = false;
    FVector StartExtendPos = FVector::ZeroVector;

    /**
     * 终点槽口延伸（SlotExtend）3D 位置（即 D 点 = C - SlotExtend * forward）。
     * 若有效，BuildBeltSplineFromDubins 会在 Dubins 曲线后追加 C→D 直线段。
     */
    bool bHasEndExtend = false;
    FVector EndExtendPos = FVector::ZeroVector;

    bool IsValid() const { return WordType != EDubinsWordType::Invalid && TotalLength > 0.f; }
};
