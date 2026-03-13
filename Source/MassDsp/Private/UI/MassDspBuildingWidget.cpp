#include "UI/MassDspBuildingWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "GameFramework/PlayerController.h"
#include "MassDspGameMode.h"
#include "Subsystems/MassDspManager.h"

// 
//  生命周期
// 

void UMassDspBuildingWidget::NativeConstruct()
{
    Super::NativeConstruct();

    // 自动绑定关闭按钮（若蓝图中存在名为 Button_Close 的按钮）
    if (Button_Close && !Button_Close->OnClicked.IsBound())
    {
        Button_Close->OnClicked.AddDynamic(this, &UMassDspBuildingWidget::OnCloseButtonClicked);
    }
}

void UMassDspBuildingWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

    RefreshAccum += InDeltaTime;
    if (RefreshAccum >= RefreshInterval)
    {
        RefreshAccum = 0.f;
        RefreshWidgets();
    }
}

// 
//  公共接口
// 

void UMassDspBuildingWidget::InitWidget(FMassEntityHandle InEntity, EBuildingType InBuildingType)
{
    TargetEntity  = InEntity;
    BuildingType  = InBuildingType;

    // 写入标题（若蓝图放置了 TextBlock_Title）
    if (TextBlock_Title)
    {
        const UEnum* Enum = StaticEnum<EBuildingType>();
        FText Title = Enum
            ? Enum->GetDisplayNameTextByValue(static_cast<int64>(BuildingType))
            : FText::FromString(TEXT("Building"));
        TextBlock_Title->SetText(Title);
    }

    // 立即刷新一次，避免第一帧空白
    RefreshAccum = RefreshInterval;
}

void UMassDspBuildingWidget::CloseWidget()
{
    RemoveFromParent();

    APlayerController* PC = GetOwningPlayer();
    if (!PC) return;

    FInputModeGameOnly GameOnly;
    PC->SetInputMode(GameOnly);
    PC->bShowMouseCursor = false;
}

int32 UMassDspBuildingWidget::TryStoreItemsFromPlayer(EItemType ItemType, int32 Quantity)
{
    UMassDspManager* Manager = GetDspManager();
    return Manager ? Manager->TryStoreItemsFromPlayer(TargetEntity, ItemType, Quantity) : 0;
}

int32 UMassDspBuildingWidget::TryTakeItemsForPlayer(EItemType ItemType, int32 Quantity)
{
    UMassDspManager* Manager = GetDspManager();
    return Manager ? Manager->TryTakeItemsForPlayer(TargetEntity, ItemType, Quantity) : 0;
}

// 
//  工具函数
// 

FText UMassDspBuildingWidget::GetItemTypeDisplayName(EItemType ItemType)
{
    const UEnum* Enum = StaticEnum<EItemType>();
    return Enum
        ? Enum->GetDisplayNameTextByValue(static_cast<int64>(ItemType))
        : FText::FromString(TEXT("Unknown"));
}

FText UMassDspBuildingWidget::GetRecipeTypeDisplayName(ERecipeType RecipeType)
{
    const UEnum* Enum = StaticEnum<ERecipeType>();
    return Enum
        ? Enum->GetDisplayNameTextByValue(static_cast<int64>(RecipeType))
        : FText::FromString(TEXT("None"));
}

UMassDspManager* UMassDspBuildingWidget::GetDspManager() const
{
    return GetWorld() ? GetWorld()->GetSubsystem<UMassDspManager>() : nullptr;
}

UGameConfigData* UMassDspBuildingWidget::GetGameConfig() const
{
    const UWorld* World = GetWorld();
    const AMassDspGameMode* GM = World ? Cast<AMassDspGameMode>(World->GetAuthGameMode()) : nullptr;
    return GM ? GM->GameConfig.Get() : nullptr;
}

// 
//  私有
// 

void UMassDspBuildingWidget::OnCloseButtonClicked()
{
    CloseWidget();
}
