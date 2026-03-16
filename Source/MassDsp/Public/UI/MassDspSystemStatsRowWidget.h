#pragma once

#include "CoreMinimal.h"
#include "Blueprint/IUserObjectListEntry.h"
#include "Blueprint/UserWidget.h"

#include "MassDspSystemStatsRowWidget.generated.h"

class UBorder;
class UTextBlock;

UCLASS(Blueprintable)
class MASSDSP_API UMassDspSystemStatsRowWidget : public UUserWidget, public IUserObjectListEntry
{
    GENERATED_BODY()

protected:
    virtual void NativeConstruct() override;
    virtual void NativeOnListItemObjectSet(UObject* ListItemObject) override;

private:
    void EnsureWidgetTreeBuilt();
    void ApplyTexts(const FText& InTitle, const FText& InValue, const FText& InDetails) const;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UBorder> Border_Row;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> TextBlock_Title;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> TextBlock_Value;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> TextBlock_Details;
};