#pragma once

#include "CoreMinimal.h"
#include "GameConst.h"

#include "GameFramework/GameModeBase.h"
#include "MassEntityConfigAsset.h"
#include "MassDspGameMode.generated.h"

class AMassDspMiner;
class AMassDspStorage;
class AMassDspAssembler;
class AMassDspLogisticsTower;

UCLASS()
class MASSDSP_API AMassDspGameMode : public AGameModeBase
{
    GENERATED_BODY()

protected:
    AMassDspGameMode();

    virtual void BeginPlay() override;

    virtual void Tick(float DeltaTime) override;

    void ProcessConveyor(float DeltaTime) const;

public:
    UPROPERTY(EditDefaultsOnly, Category = "DSP")
    TObjectPtr<UGameConfigData> GameConfig;

    // 传送带网格
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DSP")
    TObjectPtr<UStaticMesh> ConveyorMesh;

    // 传送带材质
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DSP")
    TObjectPtr<UMaterialInterface> ConveyorMaterial;

    // 传送带上的物品配置 TODO 这个改成动态生成
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DSP")
    TObjectPtr<UMassEntityConfigAsset> BeltItemConfigAsset;

    // 矿机类
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DSP|Building")
    TSubclassOf<AMassDspMiner> MinerClass;

    // 仓库类
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DSP|Building")
    TSubclassOf<AMassDspStorage> StorageClass;

    // 合成器类
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DSP|Building")
    TSubclassOf<AMassDspAssembler> AssemblerClass;

    // 物流塔类
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DSP|Building")
    TSubclassOf<AMassDspLogisticsTower> LogisticsTowerClass;

    // 无人机 ISM 网格（用于物流演示场景）
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DSP|Logistics")
    TObjectPtr<UStaticMesh> DroneMesh;

private:
    /** 演示场景：周期性提交 Supply+Demand 请求的 Timer */
    FTimerHandle DemoRequestTimer;
};
