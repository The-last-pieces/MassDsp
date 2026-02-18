#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "MassDspBuildingProcessor.generated.h"

class UMassDspManager;

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
    // 矿机生产逻辑Query
    FMassEntityQuery MinerQuery;

    // 仓库存储逻辑Query
    FMassEntityQuery StorageQuery;

    // 合成台合成逻辑Query
    FMassEntityQuery AssemblerQuery;

    // 槽口输入输出逻辑Query
    FMassEntityQuery SlotQuery;

    // 缓存子系统引用
    TWeakObjectPtr<UMassDspManager> DspManager;
};
