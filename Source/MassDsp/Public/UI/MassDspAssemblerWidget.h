#pragma once

#include "CoreMinimal.h"
#include "UI/MassDspBuildingWidget.h"
#include "Fragments/MassDspAssemblerFragment.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "MassDspAssemblerWidget.generated.h"

/**
 * 合成台交互界面
 *
 * 蓝图控件命名规范：
 *  ProgressBar_Crafting     当前合成进度 [0, 1]
 *  TextBlock_RecipeType     当前配方名称
 *  TextBlock_Speed          合成速度倍率（可选）
 *
 *  输入缓冲区槽（共 SlotMaxCount-1 = 4 个，可选，按需放置）：
 *    TextBlock_Input_0  TextBlock_Input_3
 *    格式："物品名 x 当前数量"
 *
 *  输出缓冲区槽（同上）：
 *    TextBlock_Output_0  TextBlock_Output_3
 */
UCLASS(Blueprintable)
class MASSDSP_API UMassDspAssemblerWidget : public UMassDspBuildingWidget
{
    GENERATED_BODY()

protected:
    virtual void NativeConstruct() override;
    virtual void RefreshWidgets() override;

private:
    const FMassDspAssemblerFragment* GetFragment() const;
    void ChangeRecipe(int32 Direction);
    virtual EItemType GetSuggestedTransferItemType() const override;

    UFUNCTION()
    void OnPrevRecipeClicked();

    UFUNCTION()
    void OnNextRecipeClicked();

    //  必填控件 
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UProgressBar> ProgressBar_Crafting;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UTextBlock> TextBlock_RecipeType;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UButton> Button_PrevRecipe;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UButton> Button_NextRecipe;

    //  可选控件 
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> TextBlock_Speed;

    // 输入槽 0-3
    UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> TextBlock_Input_0;
    UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> TextBlock_Input_1;
    UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> TextBlock_Input_2;
    UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> TextBlock_Input_3;

    // 输出槽 0-3
    UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> TextBlock_Output_0;
    UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> TextBlock_Output_1;
    UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> TextBlock_Output_2;
    UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> TextBlock_Output_3;

    /**
     * 内部：刷新缓冲区槽位 TextBlock 数组
     * @param Buffers       当前缓冲区数组（实时数量）
     * @param RecipeItems   对应的配方条目（用于获取物品名和需求量），可为 nullptr
     * @param Count         配方中实际使用的槽位数
     * @param Slots         目标 TextBlock 指针数组（长度 4）
     */
    void RefreshBufferSlots(
        const FBufferEntry* Buffers,
        const FRecipeEntry* RecipeItems,
        int32 Count,
        UTextBlock* Slots[4]) const;
};
