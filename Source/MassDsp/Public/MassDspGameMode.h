#pragma once

#include "CoreMinimal.h"
#include "GameConst.h"

#include "GameFramework/GameModeBase.h"
#include "MassDspGameMode.generated.h"

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
    void TestCase1() const;

    void TestCase2() const;

public:
    UPROPERTY(EditDefaultsOnly, Category = "DSP")
    TObjectPtr<UGameConfigData> GameConfig;
};
