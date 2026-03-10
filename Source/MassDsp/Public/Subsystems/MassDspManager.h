#pragma once

#include "CoreMinimal.h"
#include "GameConst.h"

#include "Subsystems/WorldSubsystem.h"
#include "MassEntityManager.h"
#include "MassDspBeltTypes.h"
#include "ProceduralMeshComponent.h"
#include "Actors/MassDspBuilding.h"

#include "Containers/SparseArray.h"
#include "Components/InstancedStaticMeshComponent.h"

#include "MassDspManager.generated.h"

class UProceduralMeshComponent;
class AMassDspGameMode;
class UMassEntityConfigAsset;

// 建造放置模式
UENUM(BlueprintType)
enum class EBuildPlaceMode : uint8
{
    None = 0 UMETA(DisplayName = "空闲"),
    Building = 1 UMETA(DisplayName = "放置建筑"),
    Belt = 2 UMETA(DisplayName = "连接传送带"),
};

// Building实体生成数据
struct FBuildingSpawnData
{
    FTransform WorldTransform;

    EBuildingType BuildingType = EBuildingType::None;

    FBuildingSpawnData(const FTransform& InTransform, EBuildingType InType)
        : WorldTransform(InTransform), BuildingType(InType)
    {
    }
};

// ============================================================
//  传送带 LUT 重建数据（存储于 Manager，供懒加载时按需重建 Spline）
// ============================================================

/** 存储重建传送带曲线所需的最小数据集，仅在创建时采集一次，持久保存；
 *  LUT 卸载后由 UpdateBeltLODs 使用此数据重新烘焙。 */
struct FBeltRebuildData
{
    EBeltRebuildType   RebuildType = EBeltRebuildType::Dubins;
    EBeltType          BeltType    = EBeltType::None;  ///< 用于 Chunk 网格生成
    FDubinsPathData    DubinsData;                     ///< RebuildType == Dubins 时有效
    FHermiteRebuildData HermiteData;                   ///< RebuildType == Hermite 时有效
};

UCLASS()
class MASSDSP_API UMassDspManager : public UWorldSubsystem
{
    GENERATED_BODY()

protected:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

public:
    TMap<FBeltHandle, FBeltData> BeltEntityRegistry;

    TSparseArray<FBeltTrajectory> BeltTrajectories;

    // 与 BeltTrajectories 下标同步的重建数据（下标即 BeltHandle.Index）
    // 存储 Dubins 或 Hermite 控制点，供 LUT 卸载后懒加载重建使用。
    TSparseArray<FBeltRebuildData> BeltRebuildData;

    // ── SoA 热数据（供 ProcessConveyor SIMD Pass 使用）──────────────────────
    // 与 BeltEntityRegistry 元素一一对应，通过 FBeltData::TickIdx 索引。
    TArray<float> Belt_TotalMove; // 对应 FBeltData::TotalMove
    TArray<float> Belt_Speed; // 对应 FBeltData::BeltSpeed（初始化后不变）
    TArray<FBeltData*> Belt_Ptrs; // 指向 BeltEntityRegistry 内元素
    int32 Belt_CachedCount = -1; // 触发重建的标记

    // ── SoA 剔除热数据（供 UpdateAllBeltItemTransforms 空间网格使用）────────
    TArray<FVector> Belt_RepPos; // BeltTrajectories[i].RepresentativePosition
    TArray<float> Belt_BoundRadius; // BeltTrajectories[i].BoundRadius
    TArray<int32> Belt_TrajIndex; // BeltTrajectories 直接寻址下标（= Handle.Index）

    // 空间哈希网格：格子坐标 → SoA 下标列表，格子边长 SpatialGridCellSize
    static constexpr float SpatialGridCellSize = 5000.f; // 50m
    TMap<FIntPoint, TArray<int32>> SpatialGrid;

    /** 当传送带数量变化时，O(N) 重建 SoA 平坦数组 + 空间哈希网格。*/
    void RebuildBeltSoA();

    // 已创建的建筑 Mass Entity 数量（在 CreateBuildingEntityInternal 中自增）
    int32 BuildingEntityCount = 0;

    // 已生成建筑实体列表（用于槽口搜索）
    TArray<FMassEntityHandle> SpawnedBuildingEntities;

    // 建筑实体 -> 建筑类型 映射（用于快速查找最近建筑的类型）
    TMap<FMassEntityHandle, EBuildingType> BuildingEntityTypeRegistry;

    TWeakObjectPtr<AMassDspGameMode> GameMode;

    // ISM 物品渲染池，按物品类型分组，一种物品一个 ISM 组件
    // 注意：只存储近处物品（距离 < NearDistanceThreshold），远处物品不放入 ISM
    UPROPERTY()
    TMap<EItemType, UInstancedStaticMeshComponent*> ItemISMPool;

    // 每帧（降频）重建的 Transform 缓存（仅近处物品），避免堆分配
    TMap<EItemType, TArray<FTransform>> CachedTransformsByType;

    TMap<EBuildingType, FStaticMeshInstanceVisualizationDescHandle> CachedBuildingMeshDesc;

    // Cache: EBuildingType => Archetype（避免每次重建，在 CreateBuildingEntityInternal / BatchSpawnBuildings 中懒初始化）
    TMap<EBuildingType, FMassArchetypeHandle> CachedBuildingArchetypes;

    // Transform 同步累计时间（~30fps）
    float SyncAccum = 0.f;
    // LOD + Chunk 可视性更新累计时间（~1Hz）
    float LodAccum  = 1.f;

    // 最大渲染距离（cm）：超过此距离的传送带即使在视锥内也不渲染
    // 解决飞高时视锥覆盖大量传送带的问题，默认 500m
    float MaxRenderDistance = 50000.f;

    // ── 传送带 LOD 分级参数 ─────────────────────────────────────────────────────
    // LOD0: <3000cm，LOD1: 3000-8000cm，LOD2: 8000-20000cm，LOD3: >=20000cm
    static constexpr float BeltLODDistances[4]       = {3000.f, 8000.f, 20000.f, FLT_MAX};
    static constexpr float BeltLODAngleThresholds[4] = {3.f,    5.f,    12.f,    20.f};
    static constexpr float BeltLODMaxSegLengths[4]   = {80.f,   150.f,  400.f,   1000.f};

    /** 超过此距离（cm）的传送带卸载 LUT（释放插值查询内存）*/
    static constexpr float LUTUnloadDistance  = 25000.f; // 250m
    /** 防抖窗口：从 Unload 距离向内收缩此距离才重新加载，避免边界抖动 */
    static constexpr float LUTLoadHysteresis  = 3000.f;  // 30m

    /** 根据距离（cm）返回 LOD 等级（0-3）*/
    static FORCEINLINE int32 ComputeBeltLODLevel(float DistanceCm)
    {
        for (int32 L = 0; L < 3; ++L)
            if (DistanceCm < BeltLODDistances[L]) return L;
        return 3;
    }

protected:
    UPROPERTY()
    AActor* BeltsContainerActor;

private:
    TWeakObjectPtr<AMassDspGameMode> TryGetGameMode();

    // ── 传送带网格分块（Chunk）管理 ──────────────────────────────────────────────
    // 以 SpatialGridCellSize 为格子边长将世界划分为块，每块独立 PMC。
    // 只有视距内的块才生成 ProceduralMesh 几何，远处的块清空 Section 节省 GPU 内存。
    struct FBeltChunk
    {
        // 注意：PMC 附加到 BeltsContainerActor 并 RegisterComponent，GC 由 Actor 持有。
        UProceduralMeshComponent* PMC = nullptr;
        TArray<int32> BeltTrajectoryIndices; ///< 属于此 chunk 的 BeltTrajectories 稀疏数组下标
        bool  bMeshDirty   = false; ///< 标记需要重建网格
        int32 CurrentMeshLOD = -1;  ///< 当前 PMC 已生成的 LOD 等级（-1 = 无几何）
    };
    TMap<FIntPoint, FBeltChunk> BeltChunks;

    // PMC 对象池：Chunk 离开视距时归还，进入视距时优先复用，上限防止池子无限膨胀
    static constexpr int32 MaxFreePMCPoolSize = 64;
    UPROPERTY()
    TArray<UProceduralMeshComponent*> FreePMCPool;

    // MID 按 BeltType 缓存：同种传送带所有 Chunk 共享同一个 MID，避免每次 FlushChunk 泄漏新 MID
    UPROPERTY()
    TMap<EBeltType, UMaterialInstanceDynamic*> BeltMIDCache;

    // 全局共享样条单例：LUT 烘焙、包围球计算、Chunk 几何生成都复用它。
    // 仅在 Initialize() 中 RegisterComponent 一次，永不销毁，零 GC 压力。
    UPROPERTY()
    USplineComponent* SharedSplineHelper = nullptr;

    // 分帧 Chunk 刷新队列：UpdateBeltChunkVisibility 入队，TickBeltMeshFlush 每帧处理 ChunksPerFrame 个
    TArray<FIntPoint> PendingFlushQueue;
    TSet<FIntPoint>   PendingFlushSet;
    static constexpr int32 ChunksPerFrame = 2; ///< 每帧最多刷新的 Chunk 数

    /** 根据世界位置计算所属 Chunk 的格子坐标 */
    static FORCEINLINE FIntPoint GetChunkKey(const FVector& Pos)
    {
        return FIntPoint(
            FMath::FloorToInt(Pos.X / SpatialGridCellSize),
            FMath::FloorToInt(Pos.Y / SpatialGridCellSize));
    }

    /** 从重建数据（Dubins 或 Hermite 控制点）重新生成临时 Spline 并烘焙 LUT */
    void RebuildLUTForTrajectory(int32 TrajIndex, int32 LODLevel);

    /** 刷新单个 Chunk 的 PMC 网格（按 LOD 等级生成或清除）*/
    void FlushChunk(FIntPoint ChunkKey, FBeltChunk& Chunk, const FVector& CameraPos);

    FBeltHandle CreateRuntimeBelt(const FBeltRebuildData& RebuildData, EBeltType BeltType,
                                   const FVector& CameraPos = FVector::ZeroVector);

    // 网格数据临时缓冲（每次 FlushChunk 局部构建，不持久化存储以节省内存）
    struct FMergedBeltMeshData
    {
        TArray<FVector>       Vertices;
        TArray<int32>         Triangles;
        TArray<FVector>       Normals;
        TArray<FVector2D>     UVs;
        TArray<FProcMeshTangent> Tangents;
        TArray<FLinearColor>  Colors;
    };

    static constexpr float C_Width        = 110.0f;
    static constexpr float C_BeltThickness = 20.0f;
    static constexpr float C_UVScale      = 100.0f;

    /**
     * 静态生成单条传送带网格（追加模式，写入 OutMesh）
     * @param OutMesh         目标网格数据（追加，不清空）
     * @param Spline          定义路径的样条线组件
     * @param Width           传送带宽度
     * @param Thickness       传送带厚度
     * @param UVScale         UV 平铺比例（100.0 = 1m 重复一次）
     * @param AngleThreshold  自适应细分角度阈值（度）
     * @param MaxSegmentLength 强制分段的最大距离（cm）
     */
    static void GenerateConveyorMesh(
        FMergedBeltMeshData& OutMesh,
        const USplineComponent* Spline,
        float Width           = 110.0f,
        float Thickness       = 20.0f,
        float UVScale         = 100.0f,
        float AngleThreshold  = 5.0f,
        float MaxSegmentLength = 150.0f
    );

public:
    FBeltHandle CreateAndLinkBeltForSlot(
        FMassEntityHandle SBuilding, int32 StartSlotIndex,
        FMassEntityHandle EBuilding, int32 EndSlotIndex,
        EBeltType BeltType,
        EBeltSplineType SplineType = EBeltSplineType::Default);

    /**
     * 刷新所有脏（bMeshDirty）Chunk 的 PMC 网格；按相机距离决定 LOD 密度。
     * @param CameraPos  当前相机世界坐标；ZeroVector 表示不做距离筛选（批量执行 LOD1）
     */
    void FlushBeltMesh(const FVector& CameraPos = FVector::ZeroVector);

    /**
     * 低频更新（~0.5Hz）：根据相机位置懒加载/卸载各传送带的 LUT 并切换精度等级。
     * 应由 PlayerController / GameMode 在合适频率主动调用。
     */
    void UpdateBeltLODs(const FVector& CameraPos);

    /**
     * 低频更新：检测相机移动，激活视距内 Chunk 网格、停用远处 Chunk 网格，
     * LOD 等级发生切换时标记 Chunk dirty 并加入 PendingFlushQueue（分帧处理）。
     */
    void UpdateBeltChunkVisibility(const FVector& CameraPos);

    /**
     * 每帧调用：从 PendingFlushQueue 取出最多 ChunksPerFrame 个 Chunk 刷新网格。
     * 应在 GameMode/PlayerController 的 Tick 中调用。
     */
    void TickBeltMeshFlush(const FVector& CameraPos);

    /** 上游建筑将物品放入传送带入口端（Front，distance ≈ 0）。
     *  若入口无空间返回 false，不消耗 GetItemFunc。
     *  接受任意可调用类型（lambda/functor），编译器可完全内联，消除 TFunction 虚分派开销。*/
    template <typename TGetItem>
    bool ProvideItemToBelt(FBeltHandle BeltHandle, TGetItem&& GetItemFunc)
    {
        if (!BeltHandle.IsValid()) return false;

        if (const FBeltData* BeltData = BeltEntityRegistry.Find(BeltHandle))
        {
            if (!BeltData->ItemCache.IsEmpty())
            {
                if (BeltData->GetEffectivePosition(BeltData->ItemCache.Num() - 1)
                    <= FGameConst::HalfLength * 3 + FGameConst::MinSpacing)
                    return false;
            }
        }

        const EItemType ItemType = GetItemFunc();
        if (ItemType == EItemType::None) return false;

        // 并行安全：运行时所有传送带均已预注册，此处只需 Find（纯读 TMap 结构）
        FBeltData* BeltDataPtr = BeltEntityRegistry.Find(BeltHandle);
        if (!ensureMsgf(BeltDataPtr, TEXT("ProvideItemToBelt: belt [%d] not registered"), BeltHandle.Index))
            return false;
        FBeltData& BeltData = *BeltDataPtr;

        // 如果还没有初始化传送带参数，从轨迹同步
        if (BeltData.BeltLength <= 0.f && BeltTrajectories.IsValidIndex(BeltHandle.Index))
        {
            const FBeltTrajectory& Belt = BeltTrajectories[BeltHandle.Index];
            BeltData.BeltLength = Belt.TotalLength;
            BeltData.BeltSpeed = Belt.Speed;
            if (BeltData.TickIdx >= 0 && BeltData.TickIdx < Belt_Speed.Num())
                Belt_Speed[BeltData.TickIdx] = BeltData.BeltSpeed;
        }

        FBeltItemCache NewItem;
        NewItem.Offset = FGameConst::HalfLength - BeltData.TotalMove;
        NewItem.ItemType = ItemType;
        BeltData.ItemCache.PushLast(NewItem);
        return true;
    }

    /** 下游建筑从传送带出口端（Tail，distance ≈ BeltLength）取走物品。
     *  物品尚未到达出口或 ValidateItemFunc 拒绝时返回 EItemType::None。
     *  接受任意可调用类型（lambda/functor），编译器可完全内联，消除 TFunction 虚分派开销。*/
    template <typename TValidate>
    EItemType ConsumeItemFromBelt(FBeltHandle BeltHandle, TValidate&& ValidateItemFunc)
    {
        FBeltData* BeltData = BeltEntityRegistry.Find(BeltHandle);
        if (!BeltData || BeltData->ItemCache.IsEmpty()) return EItemType::None;
        if (BeltData->BlockedCount <= 0) return EItemType::None;

        if (ValidateItemFunc(BeltData->ItemCache[0].ItemType))
        {
            const EItemType ConsumedType = BeltData->ItemCache[0].ItemType;
            BeltData->ItemCache.PopFirst();
            --BeltData->BlockedCount;

            // O(1)：只修改 GroupFrontOffset，整组同步获得新起点
            if (BeltData->BlockedCount > 0)
            {
                BeltData->GroupFrontOffset =
                    (BeltData->BeltLength - FGameConst::HalfLength - FGameConst::ItemSpace)
                    - BeltData->TotalMove;
            }
            return ConsumedType;
        }
        return EItemType::None;
    }

    // ISM 渲染：只把位于视锥体内且距离小于 MaxRenderDistance 的传送带物品放入 ISM
    // 平视：视锥剔除侧面/背面；飞高：距离上限截断覆盖面积，两者互补
    void UpdateAllBeltItemTransforms(const FConvexVolume& ViewFrustum, const FVector& CameraPos);

    // 新增：批量创建Building Entity（关卡初始化用）
    TArray<FMassEntityHandle> BatchSpawnBuildings(const TArray<FBuildingSpawnData>& SpawnDataList);

    // ──────────────────────────── 建造预览接口 ────────────────────────────

    /** 开始预览放置建筑；之后每帧调用 UpdateBuildingPreviewTransform 更新位置 */
    void BeginPreviewBuilding(EBuildingType BuildingType, const FTransform& InitialTransform);

    /** 每帧更新预览建筑的世界变换 */
    void UpdateBuildingPreviewTransform(const FTransform& WorldTransform) const;

    /** 确认放置：生成真实 Mass Entity，清除预览，返回新实体句柄 */
    FMassEntityHandle ConfirmPreviewBuilding();

    /** 取消建筑预览 */
    void CancelBuildingPreview();

    // ──────────────────────────── 传送带预览接口 ────────────────────────────

    /** 开始传送带连接预览，设置当前要放置的传送带类型 */
    void BeginPreviewBelt(EBeltType BeltType, EBeltSplineType SplineType = EBeltSplineType::Default);

    /**
     * 尝试在 WorldPos 附近自动吸附槽口
     * - 第 1 次调用：选中最近的 Output 槽作为起点
     * - 第 2 次调用：选中最近的 Input 槽作为终点，返回 true（两端锁定，可确认）
     */
    bool SelectBeltSlot(const FVector& WorldPos);

    /**
     * 已有起点时，每帧将预览终点刷新到 EndWorldPos（鼠标跟随）
     * @param EndWorldPos
     * @param EndSlotRotation  终点槽口的世界旋转（吸附到槽口时传入；无吸附时使用默认值）
     * @param EndSlotExtend    终点槽口的延伸距离（cm）；0 表示无吸附，自动推算方向
     */
    void UpdateBeltPreviewEndPoint(const FVector& EndWorldPos, const FQuat& EndSlotRotation = FQuat::Identity, float EndSlotExtend = 0.f);

    /** 确认创建传送带；返回 FBeltHandle，并清除预览 */
    FBeltHandle ConfirmPreviewBelt();

    /** 取消传送带预览 */
    void CancelBeltPreview();

    // ──────────────────────────── 通用工具 ────────────────────────────

    /** 取消任意当前预览（兼容两种模式） */
    void CancelAnyPreview();

    /** 传送带最大允许长度（cm），超出时预览变红且无法确认 */
    static constexpr float MaxBeltLength = 20000.f;

    static constexpr float MinBeltLength = 300.f;

    /** 显示附近槽口高亮时使用的搜索半径（cm） */
    static constexpr float SlotHighlightRadius = 5000.f;

    EBuildPlaceMode GetCurrentPlaceMode() const { return CurrentPlaceMode; }
    bool IsPreviewingBuilding() const { return CurrentPlaceMode == EBuildPlaceMode::Building; }
    bool IsPreviewingBelt() const { return CurrentPlaceMode == EBuildPlaceMode::Belt; }
    bool BeltHasStartSlot() const { return bBeltHasStart; }
    EBuildingType GetPreviewBuildingType() const { return PreviewBuildingType; }
    EBeltType GetPreviewBeltType() const { return PreviewBeltType; }
    /** 起点槽口的世界坐标（Phase 2 时用于 HUD 绘制金色锁定圈） */
    FVector GetBeltStartSlotLocation() const { return BeltStartSlotLocation; }
    /** 当前预览传送带长度是否在允许范围内 */
    bool IsPreviewBeltValid() const { return bPreviewBeltDistanceValid; }

    /**
     * 收集附近所有建筑槽口的世界坐标，用于 HUD 高亮显示。
     * @param WorldPos        搜索中心（世界坐标）
     * @param HighlightRadius 搜索半径（cm）
     * @param OutOutputLocs   附近的 Output 槽口位置列表
     * @param OutInputLocs    附近的 Input  槽口位置列表
     */
    void GetNearbySlotsForHighlight(
        const FVector& WorldPos,
        float HighlightRadius,
        TArray<FVector>& OutOutputLocs,
        TArray<FVector>& OutInputLocs) const;

    /**
     * 搜索附近最近的建筑槽口
     * @param WorldPos        搜索中心（世界坐标）
     * @param SlotType        槽口类型（Input / Output）
     * @param SearchRadius    搜索半径（cm）
     * @param OutEntity       结果实体句柄
     * @param OutSlotIndex    槽口在 Input/Output 数组中的下标（0-based）
     * @param OutSlotLocation 槽口世界坐标
     * @param OutSlotRotation
     * @param OutSlotExtend
     * @return                是否找到有效槽口
     */
    bool FindNearestBuildingSlot(
        const FVector& WorldPos,
        EBuildingSlotType SlotType,
        float SearchRadius,
        FMassEntityHandle& OutEntity,
        int32& OutSlotIndex,
        FVector& OutSlotLocation,
        FQuat& OutSlotRotation,
        float& OutSlotExtend);

    /** 根据 EBuildingType 取对应建筑蓝图类（从 GameConfig 读取） */
    TSubclassOf<AMassDspBuilding> GetBuildingClassForType(EBuildingType BuildingType);

    /**
     * 在 PlayerLocation 附近搜索最近的建筑实体
     * @param PlayerLocation  搜索中心（世界坐标）
     * @param SearchRadius    搜索半径（cm）
     * @param OutEntity       结果实体句柄
     * @param OutBuildingType 最近建筑的类型
     * @param OutLocation     最近建筑的世界坐标
     * @param LocationFilter  可选过滤器，传入建筑世界坐标，返回 false 则跳过该建筑。可用于视锥检测等额外筛选。
     * @return                是否找到有效建筑 
     */
    bool FindNearestBuilding(
        const FVector& PlayerLocation,
        float SearchRadius,
        FMassEntityHandle& OutEntity,
        EBuildingType& OutBuildingType,
        FVector& OutLocation,
        const TFunction<bool(const FVector&)>& LocationFilter = nullptr);

    /**
     * 收集以 Center 为中心、半径 Radius 内所有建筑实体（公开接口，供物流 Processor 等外部系统使用）。
     * 内部走 BuildingHashGrid 空间哈希，复杂度 O(k) 其中 k=覆盖格子数。
     */
    void FindBuildingsInRadius(const FVector& Center, float Radius, TArray<FMassEntityHandle>& OutEntities) const;

private:
    // ──── 建筑空间哈希网格 (XY 二维) ─────────────────────────────────────────
    /** 网格单元尺寸 (cm)，每格 40m；查询时按 floor((Q±R)/CellSize) 范围遍历格子 */
    static constexpr float BuildingGridCellSize = 4000.f;

    /** 槽口相对建筑中心的最大偏移 (cm)，槽口查询时在建筑搜索半径外再扩展此量 */
    static constexpr float MaxBuildingSlotOffset = 1000.f;

    /** 建筑哈希网格：CellKey(int32 CX, int32 CY) -> 建筑实体列表 */
    TMap<uint64, TArray<FMassEntityHandle>> BuildingHashGrid;

    /** 将 2D 格坐标打包成唯一 uint64 Key */
    static FORCEINLINE uint64 MakeBuildingCellKey(int32 CX, int32 CY)
    {
        return (static_cast<uint64>(static_cast<uint32>(CX)) << 32) | static_cast<uint32>(CY);
    }

    /** 将建筑 (Entity, Location) 插入对应哈希格 */
    void RegisterBuildingInGrid(FMassEntityHandle Entity, const FVector& Location);

    /**
     * 收集与以 Center 为中心、半径为 Radius 的 AABB 相交的所有格子内的建筑实体。
     * 因每个建筑只注册到一个格（其原点所在格），结果集内无重复项。
     */
    void QueryBuildingGridRadius(const FVector& Center, float Radius, TArray<FMassEntityHandle>& OutEntities) const;

    // 按需懒创建指定物品类型的 ISM 组件
    UInstancedStaticMeshComponent* GetOrCreateIsmForItemType(EItemType ItemType);

    // ──────────────────────────── 建造预览状态 ────────────────────────────

    EBuildPlaceMode CurrentPlaceMode = EBuildPlaceMode::None;

    // 建筑预览
    EBuildingType PreviewBuildingType = EBuildingType::None;
    UPROPERTY()
    AActor* PreviewBuildingActor = nullptr;

    // 传送带预览
    EBeltType PreviewBeltType = EBeltType::None;
    EBeltSplineType PreviewBeltSplineType = EBeltSplineType::Spline;
    bool bBeltHasStart = false;
    bool bPreviewBeltDistanceValid = true;
    FMassEntityHandle BeltStartEntity;
    int32 BeltStartSlotIndex = -1;
    FVector BeltStartSlotLocation = FVector::ZeroVector;
    FQuat BeltStartSlotRotation = FQuat::Identity;
    float BeltStartSlotExtend = 100.f;
    FMassEntityHandle BeltEndEntity;
    int32 BeltEndSlotIndex = -1;

    UPROPERTY()
    UProceduralMeshComponent* PreviewBeltMesh = nullptr;
    UPROPERTY()
    USplineComponent* PreviewSpline = nullptr;

    /**
     * 将样条线抽象为通用接口（供 CreateAndLinkBeltForSlot 和预览共用）
     * 根据 A-B-C-D 四个控制点配置 Hermite 曲线，自动计算切线。
     * 传入的坐标为原始世界坐标（Z 偏移在函数内部处理）。
     */
    static void BuildBeltSplineFromPoints(USplineComponent* Spline, FVector A, FVector B, FVector C, FVector D);

    /**
     * 计算从起点到终点的最短 Dubins 路径2D。
     * @param StartPos      起点世界 XY
     * @param StartHeading  起点朝向（rad，从 +X 逆时针）
     * @param EndPos        终点世界 XY
     * @param EndHeading    终点朝向（rad）
     * @param r             最小转弯半径（cm）
     */
    static FDubinsPathData ComputeDubinsPath(
        const FVector2D& StartPos, float StartHeading,
        const FVector2D& EndPos, float EndHeading,
        float r = DubinsMinTurningRadius);

    /**
     * 根据 Dubins 路径数据构建样条（采样 50cm 间隔，Z 轴线性插岜）。
     */
    static void BuildBeltSplineFromDubins(USplineComponent* Spline, const FDubinsPathData& Path);

    /** 使用 PreviewSpline + GenerateConveyorMesh 重建预览传送带网格 */
    void RebuildPreviewBeltMesh(const FVector& EndWorldPos, const FQuat& EndSlotRotation = FQuat::Identity, float EndSlotExtend = 0.f);

    /** 清除预览传送带网格（不销毁组件）*/
    void ClearPreviewBeltMesh() const;
};
