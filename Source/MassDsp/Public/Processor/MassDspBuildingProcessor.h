#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "MassDspBuildingProcessor.generated.h"

class UMassDspManager;
class UZoneGraphSubsystem;

/**
 * 建筑逻辑处理器
 * 负责处理矿机生产、仓库存储以及槽口的输入输出逻辑
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
    // 查询拥有建筑片段和槽口片段的实体
    FMassEntityQuery BuildingQuery;

    // 缓存子系统引用
    TWeakObjectPtr<UMassDspManager> DspManager;
    TWeakObjectPtr<UZoneGraphSubsystem> ZoneGraphSubsystem;
};
