#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"

#include "MassDspInventoryWidget.generated.h"

class UButton;
class UTextBlock;
class UMassDspPlayerInventoryComponent;

UCLASS(Blueprintable)
class MASSDSP_API UMassDspInventoryWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    void CloseWidget();

protected:
    virtual void NativeConstruct() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
    void RefreshInventory();
    UMassDspPlayerInventoryComponent* GetInventoryComponent() const;

    UFUNCTION()
    void OnCloseButtonClicked();

    UPROPERTY(EditAnywhere, Category = "MassDsp|Inventory", meta = (ClampMin = "0.016"))
    float RefreshInterval = 0.1f;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UButton> Button_Close;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> TextBlock_Title;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> TextBlock_Capacity;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> TextBlock_Hint;

    UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> TextBlock_Item_0;
    UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> TextBlock_Item_1;
    UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> TextBlock_Item_2;
    UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> TextBlock_Item_3;
    UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> TextBlock_Item_4;
    UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> TextBlock_Item_5;
    UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> TextBlock_Item_6;
    UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> TextBlock_Item_7;
    UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> TextBlock_Item_8;
    UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> TextBlock_Item_9;
    UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> TextBlock_Item_10;
    UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> TextBlock_Item_11;

    float RefreshAccum = 0.f;
};