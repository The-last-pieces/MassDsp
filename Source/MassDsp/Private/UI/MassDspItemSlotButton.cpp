#include "UI/MassDspItemSlotButton.h"

UMassDspItemSlotButton::UMassDspItemSlotButton(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
}

void UMassDspItemSlotButton::BindClickForwarder()
{
    OnClicked.RemoveDynamic(this, &UMassDspItemSlotButton::HandleButtonClicked);
    OnClicked.AddDynamic(this, &UMassDspItemSlotButton::HandleButtonClicked);
}

void UMassDspItemSlotButton::HandleButtonClicked()
{
    OnItemSlotClicked.Broadcast(this);
}