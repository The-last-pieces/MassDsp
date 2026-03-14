#include "UI/MassDspInventoryWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "GameFramework/PlayerController.h"
#include "Inventory/MassDspPlayerInventoryComponent.h"
#include "MassDspGameMode.h"
#include "UI/MassDspItemGridUtils.h"

void UMassDspInventoryWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (Button_Close && !Button_Close->OnClicked.IsBound())
    {
        Button_Close->OnClicked.AddDynamic(this, &UMassDspInventoryWidget::OnCloseButtonClicked);
    }

    if (TextBlock_Title)
    {
        TextBlock_Title->SetText(FText::FromString(TEXT("玩家背包")));
    }

    if (TextBlock_Hint)
    {
        TextBlock_Hint->SetText(FText::FromString(TEXT("I 关闭。建筑面板内会同时显示背包网格并支持点击转移")));
    }

    MassDspItemGridUtils::CollectGridSlots(this, TEXT("InventorySlot"), GridSlotCount, TEXT("Inventory"), GridSlots);

    RefreshAccum = RefreshInterval;
}

void UMassDspInventoryWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

    RefreshAccum += InDeltaTime;
    if (RefreshAccum >= RefreshInterval)
    {
        RefreshAccum = 0.f;
        RefreshInventory();
    }
}

void UMassDspInventoryWidget::CloseWidget()
{
    RemoveFromParent();

    APlayerController* PC = GetOwningPlayer();
    if (!PC) return;

    FInputModeGameOnly InputMode;
    PC->SetInputMode(InputMode);
    PC->bShowMouseCursor = false;
}

UMassDspPlayerInventoryComponent* UMassDspInventoryWidget::GetInventoryComponent() const
{
    const APlayerController* PC = GetOwningPlayer();
    const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
    return Pawn ? Pawn->FindComponentByClass<UMassDspPlayerInventoryComponent>() : nullptr;
}

void UMassDspInventoryWidget::RefreshInventory()
{
    UMassDspPlayerInventoryComponent* InventoryComponent = GetInventoryComponent();
    if (!InventoryComponent) return;

    if (TextBlock_Capacity)
    {
        TextBlock_Capacity->SetText(FText::FromString(FString::Printf(
            TEXT("容量: %d / %d"),
            InventoryComponent->GetTotalItemCount(),
            InventoryComponent->GetCapacity())));
    }

    TArray<FInventoryEntryView> Entries;
    InventoryComponent->GetActiveEntries(Entries);

    const AMassDspGameMode* GameMode = GetWorld() ? Cast<AMassDspGameMode>(GetWorld()->GetAuthGameMode()) : nullptr;
    const UGameConfigData* GameConfig = GameMode ? GameMode->GameConfig.Get() : nullptr;
    MassDspItemGridUtils::ApplyGridEntries(GameConfig, GridSlots, Entries, false, FText::FromString(TEXT("背包为空")));
}

void UMassDspInventoryWidget::OnCloseButtonClicked()
{
    CloseWidget();
}