#pragma once

#include "CoreMinimal.h"
#include "Components/Button.h"
#include "GameConst.h"

#include "MassDspTechNodeButton.generated.h"

class UMassDspTechNodeButton;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMassDspTechNodeClickedSignature, UMassDspTechNodeButton*, Button);

UCLASS()
class MASSDSP_API UMassDspTechNodeButton : public UButton
{
    GENERATED_BODY()

public:
    UMassDspTechNodeButton(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

    void BindClickForwarder();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Tech")
    ETechNodeId NodeId = ETechNodeId::None;

    UPROPERTY(BlueprintAssignable, Category = "MassDsp|Tech")
    FMassDspTechNodeClickedSignature OnTechNodeClicked;

private:
    UFUNCTION()
    void HandleButtonClicked();
};