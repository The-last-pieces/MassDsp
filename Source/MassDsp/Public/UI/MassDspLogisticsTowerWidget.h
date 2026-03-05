#pragma once

#include "CoreMinimal.h"
#include "UI/MassDspBuildingWidget.h"
#include "Fragments/MassDspStorageFragment.h"
#include "Fragments/MassDspLogisticsTowerFragment.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "MassDspLogisticsTowerWidget.generated.h"

/**
 * 物流塔交互界面
 *
 * 蓝图控件命名规范（必须严格一致，否则 BindWidget 报错）：
 *  ProgressBar_Storage      库存占用百分比 [0, 1]（绿/青色进度条）
 *  TextBlock_ItemType       期望/当前存储物品名称
 *  TextBlock_Inventory      "当前数量 / 最大容量"
 *  TextBlock_CoverageRadius 服务覆盖半径（可选，格式 "XXX m"）
 *  TextBlock_DroneCount     最大归属无人机上限（可选，格式 "上限 X 架"）
 *  TextBlock_Status         运行状态（可选，idle / 接受请求 / 暂停）
 *
 * 数据来源：
 *  - FMassDspStorageFragment        库存（物品类型、当前数量、最大容量）
 *  - FMassDspLogisticsTowerFragment  物流配置（覆盖半径、脏标、状态开关、期望物品）
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
    TObjectPtr<UTextBlock> TextBlock_CoverageRadius;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> TextBlock_DroneCount;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> TextBlock_Status;
};
