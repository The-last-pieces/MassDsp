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
    // 自由物品的位置修正量（插入时记录，仅自由物品有效）：
    //   有效位置 = Offset + BeltData.TotalMove
    //   插入时令 Offset = HalfLength - TotalMove，使初始有效位置 = HalfLength
    // 阻塞组物品（idx < BlockedCount）不使用此字段。
    float Offset = 0.0f;
    EItemType ItemType = EItemType::None;
};

// 传送带逻辑数据（替代 FBeltEntityArray）
struct MASSDSP_API FBeltData
{
    FMassEntityHandle StartBuildingEntity;
    FMassEntityHandle EndBuildingEntity;
    int32 StartSlotIndex = INDEX_NONE;
    int32 EndSlotIndex = INDEX_NONE;
    int32 RenderId = INDEX_NONE;

    // TODO 考虑特化数据结构,弃用Deque
    // 物品缓存：[0] = 出口端(Tail)，[Last] = 入口端(Front)
    TDeque<FBeltItemCache> ItemCache;
    float BeltLength = 0.0f;
    float BeltSpeed = 0.0f;

    // 全局累积偏移，每帧无条件 += BeltSpeed * DeltaTime
    float TotalMove = 0.f;

    // SoA 平坦数组中的下标（Manager::Belt_TotalMove[TickIdx] / Belt_Speed[TickIdx]）
    // RebuildBeltSoA() 构建时赋值，传送带生命期内稳定。
    int32 TickIdx = -1;

    // ── 刚体阻塞组（出口侧） ────────────────────────────────────────────────
    // ItemCache[0..BlockedCount-1] 属于阻塞组，作为刚体整体运动。
    //
    // 组头有效位置（随 TotalMove 前进，最大到出口）：
    //   GroupFront = min(GroupFrontOffset + TotalMove, BeltLen - HalfLen)
    //
    // 组内 index i 的有效位置：
    //   GroupFront - i * ItemSpace
    //
    // ConsumeFromTail（O(1)）：
    //   PopFirst，BlockedCount--，GroupFrontOffset 后退一格。
    //   GroupFront 从新起点（原组头 - ItemSpace）随 TotalMove 重新前进，
    //     整组所有物品同步前进，无需遍历。
    //
    // Tick（O(1) 摊还）：
    //   检查前沿自由物品（index = BlockedCount）是否追上组尾，追上则合并。
    int32 BlockedCount = 0;
    float GroupFrontOffset = 0.f; // GroupFront = min(GroupFrontOffset + TotalMove, BeltLen-HalfLen)

    /** 组头当前位置（cm）。BlockedCount==0 时返回值无意义。 */
    FORCEINLINE float GetGroupFront() const
    {
        return FMath::Min(GroupFrontOffset + TotalMove,
                          BeltLength - FGameConst::HalfLength);
    }

    /**
     * 严格 O(1) 获取指定物品的有效传送带位置（cm）。
     *
     * 双区模型：
     *   阻塞组（idx < BlockedCount）→ GroupFront - idx * ItemSpace
     *     整组共享 GroupFrontOffset，随 TotalMove 作为刚体整体流动。
     *   自由区（idx >= BlockedCount） → Offset + TotalMove
     *     各物品独立，共享 TotalMove，相对距离恒定（刚体）。
     *
     * @param ItemIdx  物品在 ItemCache 中的索引（0 = Tail/出口端）
     */
    FORCEINLINE float GetEffectivePosition(int32 ItemIdx) const
    {
        if (ItemIdx < BlockedCount)
            return GetGroupFront() - static_cast<float>(ItemIdx) * FGameConst::ItemSpace;
        return ItemCache[ItemIdx].Offset + TotalMove;
    }
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

// ============================================================
//  传送带样条类型（提前定义，FBeltTrajectory 的重建数据字段需要 FDubinsPathData）
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
    Left,     ///< CCW 圆弧
    Straight, ///< 直线
    Right,    ///< CW  圆弧
};

/**
 * Dubins 路径2D 计算结果封装。
 * 存储最短路径类型、三段实际长度（cm）以及重建曲线所需的全部输入数据。
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

    /** 3D 起点 Z 坐标（用于 Z 轴线性插值）*/
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

// ============================================================
//  LUT 懒加载重建数据
// ============================================================

/** LUT 重建算法枚举（对应两种 Spline 构建方式）*/
enum class EBeltRebuildType : uint8 { Dubins, Hermite };

/** Hermite 样条重建所需的四个控制点（A-B 为起点延伸段，C-D 为终点延伸段）*/
struct MASSDSP_API FHermiteRebuildData
{
    FVector A = FVector::ZeroVector;
    FVector B = FVector::ZeroVector;
    FVector C = FVector::ZeroVector;
    FVector D = FVector::ZeroVector;
};

// ============================================================
//  传送带轨迹数据封装
// ============================================================

// 传送带轨迹数据封装
// SplineComponent 已完全移除：LUT 烘焙/包围球计算通过 Manager 持有的 SharedSplineHelper 传入，
// FBeltTrajectory 内不持有任何 UObject，零 GC 压力。
struct MASSDSP_API FBeltTrajectory
{
    float TotalLength = 0.0f;

    float Speed = 400.0f; // 传送带速度，可以根据需要调整或从配置中读取

    // 预烘焙 LUT：按需懒加载。BakeLUTForLOD() 烘焙，UnloadLUT() 释放。
    TArray<FBeltLUTSample> LUT;
    float LUTStep = 50.0f; // LUT 采样间距（单位：cm）

    /** 传送带中点世界坐标（ComputeBoundsOnly/BakeLUT 时计算），用于每帧视锥剔除 */
    FVector RepresentativePosition = FVector::ZeroVector;

    /** 围绕 RepresentativePosition 的包围球半径，视锥剔除用 IntersectSphere 测试整条传送带 */
    float BoundRadius = 0.f;

    // ── LUT 懒加载状态 ──────────────────────────────────────────────────────────
    /** 当前已烘焙的 LUT 精度等级（-1 = 未加载；0=最高精度 LOD0，3=最低精度 LOD3）*/
    int32 CurrentLOD = -1;
    // 注意：重建数据（DubinsPathData / HermiteRebuildData）仅存一份，在 Manager::BeltRebuildData[Index] 中。
    // FBeltTrajectory 不再冒一份，140 bytes × 45万条 = 63 MB 内存省略。

    // ── 方法 ────────────────────────────────────────────────────────────────────

    /**
     * 将 SplineComponent 预烘焙为离散采样表，之后 GetTransformAtDistance 用此表插值。
     * 必须在 SplineComponent 完成初始化且 TotalLength 已赋值后调用一次。
     * @param Step  采样间距（cm），越小精度越高但内存越大
     */
    void BakeLUT(const USplineComponent* Spline, float Step = 50.0f);

    /** 仅计算 RepresentativePosition + BoundRadius，不填充 LUT（视距外初始化用）
     *  @param Spline 已 UpdateSpline() 的样条（调用方持有，FBeltTrajectory 不存储） */
    void ComputeBoundsOnly(const USplineComponent* Spline, float CoarseStep = 500.f);

    /** 释放 LUT 数组（RepresentativePosition/BoundRadius 保留），重置 CurrentLOD = -1 */
    void UnloadLUT();

    /** 按 LOD 等级选步长烘焙 LUT（LOD0=20cm，LOD1=50cm，LOD2=150cm，LOD3=500cm）
     *  @param Spline 已 UpdateSpline() 的样条（调用方持有） */
    void BakeLUTForLOD(const USplineComponent* Spline, int32 LODLevel);

    // ── Dubins 解析重载（不依赖 USplineComponent，初始化与 LUT 懒加载均可用）──────────

    /** Dubins 解析包围球：仅用 FDubinsPathData 纯数学采样，零 USplineComponent API 调用 */
    void ComputeBoundsOnly(const FDubinsPathData& DPath, float CoarseStep = 500.f);

    /** Dubins 解析 LUT 烘焙（内部被 BakeLUTForLOD 调用，也可直接使用指定步长） */
    void BakeLUT(const FDubinsPathData& DPath, float Step);

    /** Dubins 解析版 BakeLUTForLOD，完全跳过 USplineComponent */
    void BakeLUTForLOD(const FDubinsPathData& DPath, int32 LODLevel);

    bool IsValid() const;

    // 优先 LUT 查表插值（O(1)，线程安全）；LUT 未加载时返回 Scale=ZeroVector（ISM 不渲染）
    void GetTransformAtDistance(float Distance, FTransform& OutTransform) const;
};
