#pragma once

#include "CoreMinimal.h"
#include "UI/MassDspBuildingWidget.h"
#include "Fragments/MassDspStorageFragment.h"
#include "Fragments/MassDspLogisticsTowerFragment.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "MassDspLogisticsTowerWidget.generated.h"

/**
 * 物流塔交互界面（戴森球计划行星内物流风格）
 *
 * 蓝图控件命名规范（必须严格一致，否则 BindWidget 报错）：
 *  ProgressBar_Storage   库存占用百分比 [0, 1]（绿/青色进度条）        [必须]
 *  TextBlock_ItemType    当前物品类型名称                               [必须]
 *  TextBlock_Inventory   "当前数量 / 最大容量"                         [必须]
 *  TextBlock_Mode        运行模式文本（供应 / 需求 / 仓储）             [可选]
 *  TextBlock_Threshold   请求阈值数量                                   [可选]
 *  TextBlock_DroneCount  单次无人机运量                                 [可选]
 *
 * 数据来源：
 *  - FMassDspStorageFragment        库存（物品类型、当前数量、最大容量）
 *  - FMassDspLogisticsTowerFragment  物流配置（运行模式、阈值、单次运量）
 */
UCLASS(Blueprintable)
class MASSDSP_API UMassDspLogisticsTowerWidget : public UMassDspBuildingWidget
{
    GENERATED_BODY()

protected:
    virtual void RefreshWidgets() override;

private:
    /** 同时返回 Storage + Tower 两个 Fragment 指针，任意一个无效则全返回 false */
    bool GetFragments(const FMassDspStorageFragment*& OutStorage,
                      const FMassDspLogisticsTowerFragment*& OutTower) const;

    //  BindWidget（必须，蓝图中必须存在同名控件） 

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UProgressBar> ProgressBar_Storage;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UTextBlock> TextBlock_ItemType;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UTextBlock> TextBlock_Inventory;

    //  BindWidgetOptional（可选，蓝图不放置时静默跳过） 

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> TextBlock_Mode;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> TextBlock_Threshold;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> TextBlock_DroneCount;
};
