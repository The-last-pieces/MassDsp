#pragma once

#include "CoreMinimal.h"
#include "Actors/MassDspBuilding.h"
#include "MassDspMiner.generated.h"

/**
 * 矿机
 * 负责生产物品并输出到传送带
 */
UCLASS()
class MASSDSP_API AMassDspMiner : public AMassDspBuilding
{
    GENERATED_BODY()
    
public:
    AMassDspMiner();

    // 生产间隔（秒）
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Miner")
    float ProductionInterval = 2.0f;
};
