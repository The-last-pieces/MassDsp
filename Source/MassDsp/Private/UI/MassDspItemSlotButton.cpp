#include "UI/MassDspItemSlotButton.h"

UMassDspItemSlotButton::UMassDspItemSlotButton(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
}

void UMassDspItemSlotButton::PostInitProperties()
{
    Super::PostInitProperties();

    if (!HasAnyFlags(RF_ClassDefaultObject))
    {
        OnClicked.RemoveAll(this);
        OnClicked.AddDynamic(this, &UMassDspItemSlotButton::HandleButtonClicked);
    }
}

void UMassDspItemSlotButton::HandleButtonClicked()
{
    OnItemSlotClicked.Broadcast(this);
}