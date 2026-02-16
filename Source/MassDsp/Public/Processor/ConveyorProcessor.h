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
    virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
    virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
    FMassEntityQuery EntityQuery;
};
