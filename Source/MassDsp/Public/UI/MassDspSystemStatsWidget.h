#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"

#include "MassDspSystemStatsWidget.generated.h"

class UButton;
class UTextBlock;

UCLASS(Blueprintable)
class MASSDSP_API UMassDspSystemStatsWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    void CloseWidget();

protected:
    virtual void NativeConstruct() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
    void RefreshStats();

    UFUNCTION()
    void OnCloseButtonClicked();

    UFUNCTION()
    void OnRefreshButtonClicked();

    UPROPERTY(EditAnywhere, Category = "MassDsp|Stats", meta = (ClampMin = "0.05"))
    float RefreshInterval = 0.2f;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UButton> Button_Close;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UButton> Button_Refresh;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> TextBlock_Title;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> TextBlock_Performance;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> TextBlock_WorldScale;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> TextBlock_Logistics;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> TextBlock_Bottleneck;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> TextBlock_BusiestTower;

    UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> TextBlock_ItemDelta_0;
    UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> TextBlock_ItemDelta_1;
    UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> TextBlock_ItemDelta_2;
    UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> TextBlock_ItemDelta_3;

    float RefreshAccum = 0.f;
};