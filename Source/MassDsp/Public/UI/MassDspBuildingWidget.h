#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MassEntityTypes.h"
#include "GameConst.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "MassDspBuildingWidget.generated.h"

class UMassDspManager;
class UGameConfigData;
class UMassDspPlayerInventoryComponent;

/**
 * 建筑交互 UI 基类
 *
 * 架构说明：
 *   - 蓝图只负责按命名规范放置 UI 节点（ProgressBar / TextBlock / Button）。
 *   - C++ 通过 meta=(BindWidget/BindWidgetOptional) 按名称绑定控件。
 *   - NativeTick 以 RefreshInterval 频率驱动 RefreshWidgets()，子类重写此函数
 *     直接操控绑定控件，无需蓝图事件。
 *
 * 蓝图命名约定（必须严格一致，否则 BindWidget 报错）：
 *   Button_Close    关闭按钮（可选）
 *   TextBlock_Title  建筑名称标题（可选）
 */
UCLASS(Abstract, Blueprintable)
class MASSDSP_API UMassDspBuildingWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    /** HUD 调用：设置目标实体，InitWidget 在 AddToViewport 前调用 */
    void InitWidget(FMassEntityHandle InEntity, EBuildingType InBuildingType);

    /** 蓝图关闭按钮或 HUD F 键再按一次时调用，统一处理 RemoveFromParent + 恢复输入 */
    void CloseWidget();

    FMassEntityHandle GetTargetEntity() const { return TargetEntity; }

    EBuildingType GetTargetBuildingType() const { return BuildingType; }

    int32 TryStoreItemsFromPlayer(EItemType ItemType, int32 Quantity);

    int32 TryTakeItemsForPlayer(EItemType ItemType, int32 Quantity);

protected:
    virtual void NativeConstruct() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

    /**
     * 子类重写此函数，从 Fragment 读取数据并直接写入绑定控件。
     * 由基类 NativeTick 按 RefreshInterval 调用，无需蓝图参与。
     */
    virtual void RefreshWidgets()
    {
    }

    //  工具函数供子类使用 

    /** 将 EItemType 枚举值转为显示文本 */
    static FText GetItemTypeDisplayName(EItemType ItemType);

    /** 将 ERecipeType 枚举值转为显示文本 */
    static FText GetRecipeTypeDisplayName(ERecipeType RecipeType);

    UMassDspManager* GetDspManager() const;

    UGameConfigData* GetGameConfig() const;

    UMassDspPlayerInventoryComponent* GetPlayerInventory() const;

    virtual EItemType GetSuggestedTransferItemType() const { return EItemType::None; }

    //  数据 

    FMassEntityHandle TargetEntity;

    UPROPERTY(BlueprintReadOnly, Category = "MassDsp|UI")
    EBuildingType BuildingType = EBuildingType::None;

    /** 刷新频率（秒），默认 0.1 = 10Hz */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|UI", meta = (ClampMin = "0.016"))
    float RefreshInterval = 1 / 60.0f;

    //  公共 BindWidget（可选，蓝图中可不放） 

    /** 蓝图中放置名为 Button_Close 的按钮即可自动绑定关闭逻辑 */
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UButton> Button_Close;

    /** 蓝图中放置名为 TextBlock_Title 的文本块，InitWidget 会写入建筑类型名 */
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> TextBlock_Title;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> TextBlock_TransferItem;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> TextBlock_TransferStatus;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UButton> Button_PrevTransferItem;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UButton> Button_NextTransferItem;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UButton> Button_StoreOne;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UButton> Button_TakeOne;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UButton> Button_StoreAll;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UButton> Button_TakeAll;

private:
    float RefreshAccum = 0.f;
    EItemType SelectedTransferItem = EItemType::None;
    FText LastTransferStatus;

    void RefreshTransferWidgets();
    void ChangeTransferItem(int32 Direction);
    void ExecuteStore(bool bStoreAll);
    void ExecuteTake(bool bTakeAll);
    void EnsureTransferItemSelected();
    void BuildTransferSelectableItems(TArray<EItemType>& OutItems) const;
    void AppendTransferCandidate(TArray<EItemType>& OutItems, EItemType ItemType) const;

    UFUNCTION()
    void OnCloseButtonClicked();

    UFUNCTION()
    void OnPrevTransferItemClicked();

    UFUNCTION()
    void OnNextTransferItemClicked();

    UFUNCTION()
    void OnStoreOneClicked();

    UFUNCTION()
    void OnTakeOneClicked();

    UFUNCTION()
    void OnStoreAllClicked();

    UFUNCTION()
    void OnTakeAllClicked();
};
