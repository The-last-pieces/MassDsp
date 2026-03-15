#pragma once

#include "CoreMinimal.h"
#include "Components/Button.h"
#include "MassDspItemSlotButton.generated.h"

class UMassDspItemSlotButton;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMassDspItemSlotClickedSignature, UMassDspItemSlotButton*, Button);

UCLASS()
class MASSDSP_API UMassDspItemSlotButton : public UButton
{
    GENERATED_BODY()

public:
    UMassDspItemSlotButton(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

    void BindClickForwarder();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|UI")
    int32 SlotIndex = INDEX_NONE;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|UI")
    FName SlotGroup;

    UPROPERTY(BlueprintAssignable, Category = "MassDsp|UI")
    FMassDspItemSlotClickedSignature OnItemSlotClicked;

private:
    UFUNCTION()
    void HandleButtonClicked();
};