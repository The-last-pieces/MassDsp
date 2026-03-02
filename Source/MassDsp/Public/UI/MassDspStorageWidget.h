#pragma once

#include "CoreMinimal.h"
#include "UI/MassDspBuildingWidget.h"
#include "Fragments/MassDspStorageFragment.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "MassDspStorageWidget.generated.h"

/**
 * 仓库交互界面
 *
 * 蓝图控件命名规范：
 *  ProgressBar_Fill      库存占用百分比 [0, 1]
 *  TextBlock_ItemType    存储的物品名称
 *  TextBlock_Inventory   "当前数量 / 最大容量"
 */
UCLASS(Blueprintable)
class MASSDSP_API UMassDspStorageWidget : public UMassDspBuildingWidget
{
    GENERATED_BODY()

protected:
    virtual void RefreshWidgets() override;

private:
    const FMassDspStorageFragment* GetFragment() const;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UProgressBar> ProgressBar_Fill;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UTextBlock> TextBlock_ItemType;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UTextBlock> TextBlock_Inventory;
};
