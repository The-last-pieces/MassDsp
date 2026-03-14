#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/MassDspItemGridUtils.h"

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
    static constexpr int32 GridSlotCount = 16;

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

    TArray<FMassDspItemGridSlotRefs> GridSlots;

    float RefreshAccum = 0.f;
};