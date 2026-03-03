#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"

#include "MassDspLogisticsProcessor.generated.h"

class UMassDspLogisticsSubsystem;
class UMassDspManager;

/**
 * 物流建筑兜底扫描 Processor
 *
 * 职责（仅兜底轮询，不处理设备状态机）：
 *   - 查询所有拥有 FMassDspLogisticsTowerFragment + FMassDspStorageFragment 的实体
 *   - 超过 ScanInterval 未触发事件推送时，主动扫描 CoverageRadius 内的建筑
 *   - 对库存过满的 Storage 提交 SubmitSupplyRequest
 *   - 对输入缓冲不足的 Assembler 提交 SubmitDemandRequest
 *   - 去重：已有 Pending 请求的建筑跳过，避免重复提交
 *
 * 不需要处理设备运动：无人机/小车/火车的状态机推进在
 * UMassDspLogisticsSubsystem::Tick 内完成（连续内存遍历，零虚调用）。
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
    /** 查询所有物流塔实体（LogisticsTowerFragment + StorageFragment + TransformFragment） */
    FMassEntityQuery TowerQuery;

    TWeakObjectPtr<UMassDspLogisticsSubsystem> LogisticsSubsystem;
    TWeakObjectPtr<UMassDspManager>            DspManager;

    UMassDspLogisticsSubsystem* GetLogisticsSubsystem(UWorld* World);
    UMassDspManager*            GetDspManager(UWorld* World);
};
