#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MassEntityConfigAsset.h"
#include "MassDspGameMode.generated.h"

class AMassDspMiner;
class AMassDspStorage;
class AMassDspAssembler;

UCLASS()
class MASSDSP_API AMassDspGameMode : public AGameModeBase
{
    GENERATED_BODY()

protected:
    virtual void BeginPlay() override;

public:
    // 传送带网格
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DSP")
    TObjectPtr<UStaticMesh> ConveyorMesh;

    // 传送带上的物品配置
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DSP")
    TObjectPtr<UMassEntityConfigAsset> BeltItemConfigAsset;

    // 矿机类
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DSP")
    TSubclassOf<AMassDspMiner> MinerClass;

    // 仓库类
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DSP")
    TSubclassOf<AMassDspStorage> StorageClass;

    // 合成器类
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DSP")
    TSubclassOf<AMassDspAssembler> AssemblerClass;
};
