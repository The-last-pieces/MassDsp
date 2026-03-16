#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"

#include "MassDspSystemStatsWidget.generated.h"

class UButton;
class UListView;
class UTextBlock;
class UMassDspSystemStatsRowData;

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
    void ResetListItems();
    void AddStatRow(const FText& Title, const FText& Value, const FText& Details);

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
    TObjectPtr<UListView> ListView_Stats;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UMassDspSystemStatsRowData>> RowItems;

    float RefreshAccum = 0.f;
};