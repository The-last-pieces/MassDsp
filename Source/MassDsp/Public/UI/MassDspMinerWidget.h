#pragma once

#include "CoreMinimal.h"
#include "UI/MassDspBuildingWidget.h"
#include "Fragments/MassDspMinerFragment.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "MassDspMinerWidget.generated.h"

/**
 * 矿机交互界面
 *
 * 蓝图中按以下命名规范放置控件，C++ 自动绑定并每 RefreshInterval 秒刷新一次：
 *
 *  ProgressBar_Production   生产进度条       [0, 1]
 *  TextBlock_ItemType       当前矿产物品名称
 *  TextBlock_Inventory      "当前库存 / 最大库存"
 *  TextBlock_Interval       "每 X 秒产出 1 个"
 *
 * 加 Optional 后缀的控件可省略。
 */
UCLASS(Blueprintable)
class MASSDSP_API UMassDspMinerWidget : public UMassDspBuildingWidget
{
    GENERATED_BODY()

protected:
    virtual void RefreshWidgets() override;

private:
    const FMassDspMinerFragment* GetFragment() const;

    //  绑定控件（必须存在于蓝图，否则编译警告） 
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UProgressBar> ProgressBar_Production;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UTextBlock> TextBlock_ItemType;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UTextBlock> TextBlock_Inventory;

    //  可选控件 
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> TextBlock_Interval;
};
