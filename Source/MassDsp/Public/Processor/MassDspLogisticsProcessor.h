#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"

#include "MassDspLogisticsProcessor.generated.h"

class UMassDspLogisticsSubsystem;

/**
 * 物流塔内层扫描 Processor（DSP 风格）
 *
 * 职责：
 *   - 查询所有 FMassDspLogisticsTowerFragment + FMassDspStorageFragment 实体
 *   - Supply 模式：库存 > RequestThreshold → SubmitSupplyRequest
 *   - Demand 模式：库存 < RequestThreshold → SubmitDemandRequest
 *   - Storage 模式：跳过，不参与调度
 *   - bDirty=true （事件推送已触发）时跳过兜底扫描
 *
 * 设备状态机在 UMassDspLogisticsSubsystem::Tick 内推进，本 Processor 不涉及设备运动。
 */
UCLASS()
class MASSDSP_API UMassDspLogisticsProcessor : public UMassProcessor
{
    GENERATED_BODY()

public:
    UMassDspLogisticsProcessor();

protected:
    virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
    virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
    /** 查询所有物流塔实体（LogisticsTowerFragment + StorageFragment） */
    FMassEntityQuery TowerQuery;

    TWeakObjectPtr<UMassDspLogisticsSubsystem> LogisticsSubsystem;

    UMassDspLogisticsSubsystem* GetLogisticsSubsystem(UWorld* World);
};
