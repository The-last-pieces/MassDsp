#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameConst.h"

#include "MassDspTechTreeWidget.generated.h"

class UButton;
class UScrollBox;
class UTextBlock;
class UMassDspTechNodeButton;
class UMassDspTechTreeSubsystem;

UCLASS(Blueprintable)
class MASSDSP_API UMassDspTechTreeWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    void CloseWidget();

protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
    void RefreshTree();
    void RebuildNodeCards();
    FString BuildRefreshStateKey() const;
    void HandleTechTreeChanged();

    FString BuildPrerequisiteText(const FTechNodeConfig& Config) const;
    FString BuildCostText(const FTechNodeConfig& Config) const;
    FString BuildRewardText(const FTechNodeConfig& Config) const;
    FText BuildStatusText(ETechNodeId NodeId) const;
    FLinearColor GetStatusColor(ETechNodeId NodeId) const;

    UMassDspTechTreeSubsystem* GetTechTreeSubsystem() const;

    UFUNCTION()
    void OnCloseButtonClicked();

    UFUNCTION()
    void OnNodeButtonClicked(UMassDspTechNodeButton* ClickedButton);

    UPROPERTY(EditAnywhere, Category = "MassDsp|Tech", meta = (ClampMin = "0.05"))
    float RefreshInterval = 0.2f;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> TextBlock_Title;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> TextBlock_Summary;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> TextBlock_Hint;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UButton> Button_Close;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UScrollBox> ScrollBox_Nodes;

    UPROPERTY()
    TArray<TObjectPtr<UMassDspTechNodeButton>> NodeButtons;

    FString LastRefreshStateKey;

    float RefreshAccum = 0.0f;
};