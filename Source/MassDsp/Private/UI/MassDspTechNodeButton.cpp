#include "UI/MassDspTechNodeButton.h"

UMassDspTechNodeButton::UMassDspTechNodeButton(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
}

void UMassDspTechNodeButton::BindClickForwarder()
{
    OnClicked.RemoveDynamic(this, &UMassDspTechNodeButton::HandleButtonClicked);
    OnClicked.AddDynamic(this, &UMassDspTechNodeButton::HandleButtonClicked);
}

void UMassDspTechNodeButton::HandleButtonClicked()
{
    OnTechNodeClicked.Broadcast(this);
}