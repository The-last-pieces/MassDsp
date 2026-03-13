#include "UI/MassDspInventoryWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "GameFramework/PlayerController.h"
#include "Inventory/MassDspPlayerInventoryComponent.h"
#include "Player/MassDspPlayerCharacter.h"

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
        TextBlock_Hint->SetText(FText::FromString(TEXT("I 关闭, F 可打开建筑面板做存取")));
    }

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

    UTextBlock* ItemRows[12] = {
        TextBlock_Item_0, TextBlock_Item_1, TextBlock_Item_2, TextBlock_Item_3,
        TextBlock_Item_4, TextBlock_Item_5, TextBlock_Item_6, TextBlock_Item_7,
        TextBlock_Item_8, TextBlock_Item_9, TextBlock_Item_10, TextBlock_Item_11,
    };

    const UEnum* ItemEnum = StaticEnum<EItemType>();
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(ItemRows); ++Index)
    {
        UTextBlock* Row = ItemRows[Index];
        if (!Row) continue;

        if (Entries.IsValidIndex(Index))
        {
            const FInventoryEntryView& Entry = Entries[Index];
            const FText ItemName = ItemEnum
                ? ItemEnum->GetDisplayNameTextByValue(static_cast<int64>(Entry.ItemType))
                : FText::FromString(TEXT("Unknown"));
            Row->SetText(FText::FromString(FString::Printf(TEXT("%s x %d"), *ItemName.ToString(), Entry.Quantity)));
            Row->SetVisibility(ESlateVisibility::HitTestInvisible);
        }
        else
        {
            Row->SetText(Index == 0 && Entries.IsEmpty()
                ? FText::FromString(TEXT("背包为空"))
                : FText::FromString(TEXT("")));
            Row->SetVisibility(ESlateVisibility::HitTestInvisible);
        }
    }
}

void UMassDspInventoryWidget::OnCloseButtonClicked()
{
    CloseWidget();
}