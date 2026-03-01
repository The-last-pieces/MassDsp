#pragma once

#include "CoreMinimal.h"
#include "GameConst.h"

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
    AMassDspGameMode();

    virtual void BeginPlay() override;

    virtual void Tick(float DeltaTime) override;
    
    void ProcessConveyor(float DeltaTime) const;

private:
    // 帧率统计
    TArray<float> FrameTimeHistory;
    int32 MaxHistorySize = 300; // 保留5秒数据(60fps)
    float StatUpdateInterval = 1.0f; // 每秒更新统计
    float TimeSinceLastUpdate = 0.0f;

    float AverageFPS = 0.0f;
    float OnePercentLowFPS = 0.0f;

    void UpdateFrameStats();

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
};
