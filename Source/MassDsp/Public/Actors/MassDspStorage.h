#pragma once

#include "CoreMinimal.h"
#include "Actors/MassDspBuilding.h"
#include "MassDspStorage.generated.h"

/**
 * 仓库
 * 负责接收传送带运送过来的物品
 */
UCLASS()
class MASSDSP_API AMassDspStorage : public AMassDspBuilding
{
    GENERATED_BODY()

public:
    AMassDspStorage();

protected:
    virtual const UScriptStruct* GetStaticStructForFragment() const override;
    virtual TArray<const UScriptStruct*> GetStaticStructs() const override;
    
    virtual void InitFragmentForEntity(FMassEntityManager& EntityManager, FMassEntityHandle EntityHandle, const FTransform& WorldTransform) const override;

public:
    // 存储容量
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Storage")
    int32 Capacity = 50;
};
