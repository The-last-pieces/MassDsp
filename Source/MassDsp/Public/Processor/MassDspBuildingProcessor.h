#pragma once

#include "CoreMinimal.h"
#include "GameConst.h"
#include "MassExecutionContext.h"
#include "MassProcessor.h"
#include "MassDspBuildingProcessor.generated.h"

class AMassDspBuilding;
class UMassDspManager;
class AMassDspGameMode;
struct FMassDspBuildingSlotsFragment;
struct FBeltHandle;
class UMassEntityConfigAsset;

/**
 * 建筑逻辑处理器
 * 负责处理矿机生产、仓库存储、合成台合成以及槽口的输入输出逻辑
 * 使用分离的Query来处理不同类型的建筑，符合ECS数据导向设计
 */
UCLASS()
class MASSDSP_API UMassDspBuildingProcessor : public UMassProcessor
{
    GENERATED_BODY()

public:
    UMassDspBuildingProcessor();

protected:
    virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
    virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
    // 矿机生产逻辑Query（包含槽口处理）
    FMassEntityQuery MinerQuery;

    // 仓库存储逻辑Query（包含槽口处理）
    FMassEntityQuery StorageQuery;

    // 合成台合成逻辑Query（包含槽口处理）
    FMassEntityQuery AssemblerQuery;

    // 合成台渲染逻辑Query
    FMassEntityQuery AssemblerRenderQuery;

    // 缓存子系统引用
    TWeakObjectPtr<UMassDspManager> DspManager;

    // ── Pass-1: TickExecute + 输出槽（Provide）─────────────────────────────
    // 每条传送带仅 1 个 Provide 方，各线程写不同 FBeltData，无竞争
    template <class TT> requires IsDspBuildFragment<TT>
    void ProcessBuildingOutputs(FMassEntityQuery& Query, FMassExecutionContext& Context, float WorldTime) const;

    // ── Pass-2: 输入槽（Consume）──────────────────────────────────────────
    // 每条传送带仅 1 个 Consume 方，Pass-1 全部结束后才开始，天然无锁
    template <class TT> requires IsDspBuildFragment<TT>
    void ProcessBuildingInputs(FMassEntityQuery& Query, FMassExecutionContext& Context, float WorldTime) const;

    // InRecipe 仅 Assembler 路径传入非 nullptr
    template <class TT> requires IsDspBuildFragment<TT>
    void ProcessOutputSlots(FMassDspBuildingSlotsFragment& SlotsData, TT& Fragment, float WorldTime,
                            const FRecipeDataForFragment* InRecipe = nullptr) const;

    template <class TT> requires IsDspBuildFragment<TT>
    void ProcessInputSlots(FMassDspBuildingSlotsFragment& SlotsData, TT& Fragment, float WorldTime,
                           const FRecipeDataForFragment* InRecipe = nullptr) const;
};
