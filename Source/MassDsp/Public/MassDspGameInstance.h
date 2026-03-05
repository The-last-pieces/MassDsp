#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "MassDspGameInstance.generated.h"

UCLASS()
class MASSDSP_API UMassDspGameInstance : public UGameInstance
{
    GENERATED_BODY()

public:
    virtual void Init() override;
};
