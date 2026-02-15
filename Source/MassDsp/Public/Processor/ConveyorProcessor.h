// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "ConveyorProcessor.generated.h"

UCLASS()
class MASSDSP_API UConveyorProcessor : public UMassProcessor
{
    GENERATED_BODY()

public:
    UConveyorProcessor();

protected:
    // 核心：配置这个 Processor 需要提取哪些数据片
    virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;

    // 核心：每帧执行的批处理逻辑
    virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
    FMassEntityQuery EntityQuery;
};
