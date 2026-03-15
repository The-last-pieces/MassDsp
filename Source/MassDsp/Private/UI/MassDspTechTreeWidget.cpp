#include "UI/MassDspTechTreeWidget.h"

#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "MassDspGameMode.h"

#include "Blueprint/WidgetTree.h"

#include "Components/Border.h"

#include "Subsystems/MassDspTechTreeSubsystem.h"
#include "UI/MassDspTechNodeButton.h"

namespace
{
    FString GetEnumDisplayName(const UEnum* EnumPtr, int64 Value, const FString& Fallback)
    {
        return EnumPtr ? EnumPtr->GetDisplayNameTextByValue(Value).ToString() : Fallback;
    }
}

void UMassDspTechTreeWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (Button_Close)
    {
        Button_Close->OnClicked.RemoveDynamic(this, &UMassDspTechTreeWidget::OnCloseButtonClicked);
        Button_Close->OnClicked.AddDynamic(this, &UMassDspTechTreeWidget::OnCloseButtonClicked);
    }

    if (TextBlock_Title)
    {
        TextBlock_Title->SetText(FText::FromString(TEXT("科技树 / 成长系统")));
    }

    if (TextBlock_Hint)
    {
        TextBlock_Hint->SetText(FText::FromString(TEXT("T 关闭。当前第一版支持研究节点并解锁建筑、配方与传送带能力。")));
    }

    if (UMassDspTechTreeSubsystem* TechTree = GetTechTreeSubsystem())
    {
        TechTree->OnTechTreeChanged().RemoveAll(this);
        TechTree->OnTechTreeChanged().AddUObject(this, &UMassDspTechTreeWidget::HandleTechTreeChanged);
    }

    RefreshAccum = RefreshInterval;
    LastRefreshStateKey.Reset();
    RefreshTree();
}

void UMassDspTechTreeWidget::NativeDestruct()
{
    if (UMassDspTechTreeSubsystem* TechTree = GetTechTreeSubsystem())
    {
        TechTree->OnTechTreeChanged().RemoveAll(this);
    }

    Super::NativeDestruct();
}

void UMassDspTechTreeWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

    RefreshAccum += InDeltaTime;
    if (RefreshAccum >= RefreshInterval)
    {
        RefreshAccum = 0.0f;
        RefreshTree();
    }
}

void UMassDspTechTreeWidget::CloseWidget()
{
    RemoveFromParent();

    APlayerController* PC = GetOwningPlayer();
    if (!PC) return;

    FInputModeGameOnly InputMode;
    PC->SetInputMode(InputMode);
    PC->bShowMouseCursor = false;
}

void UMassDspTechTreeWidget::RefreshTree()
{
    UMassDspTechTreeSubsystem* TechTree = GetTechTreeSubsystem();
    if (!TechTree)
    {
        if (TextBlock_Summary)
        {
            TextBlock_Summary->SetText(FText::FromString(TEXT("科技树子系统不可用")));
        }
        return;
    }

    if (TextBlock_Summary)
    {
        const FMassDspPlayerTechState& TechState = TechTree->GetPlayerTechState();
        TextBlock_Summary->SetText(FText::FromString(FString::Printf(
            TEXT("已解锁 %d 个节点。当前可先研究“基础熔炼”来解锁合成台与铁板配方。"),
            TechState.UnlockedNodes.Num())));
    }

    const FString RefreshStateKey = BuildRefreshStateKey();
    if (RefreshStateKey == LastRefreshStateKey)
    {
        return;
    }

    LastRefreshStateKey = RefreshStateKey;
    RebuildNodeCards();
}

FString UMassDspTechTreeWidget::BuildRefreshStateKey() const
{
    UMassDspTechTreeSubsystem* TechTree = GetTechTreeSubsystem();
    if (!TechTree)
    {
        return TEXT("NoTechTree");
    }

    const UGameConfigData* GameConfig = nullptr;
    if (const UWorld* World = GetWorld())
    {
        if (const AMassDspGameMode* GameMode = Cast<AMassDspGameMode>(World->GetAuthGameMode()))
        {
            GameConfig = GameMode->GameConfig.Get();
        }
    }
    if (!GameConfig)
    {
        return TEXT("NoGameConfig");
    }

    TArray<ETechNodeId> SortedNodeIds;
    GameConfig->GetTechNodeConfigs().GetKeys(SortedNodeIds);
    SortedNodeIds.Sort([](ETechNodeId A, ETechNodeId B)
    {
        return static_cast<uint8>(A) < static_cast<uint8>(B);
    });

    FString StateKey;
    StateKey.Reserve(SortedNodeIds.Num() * 32);

    for (const ETechNodeId NodeId : SortedNodeIds)
    {
        if (NodeId == ETechNodeId::None)
        {
            continue;
        }

        FText FailureReason;
        const bool bUnlocked = TechTree->IsNodeUnlocked(NodeId);
        const bool bCanUnlock = TechTree->CanUnlockNode(NodeId, &FailureReason);

        StateKey += FString::Printf(TEXT("%d:%d:%d:%s|"),
            static_cast<int32>(NodeId),
            bUnlocked ? 1 : 0,
            bCanUnlock ? 1 : 0,
            *FailureReason.ToString());
    }

    return StateKey;
}

void UMassDspTechTreeWidget::HandleTechTreeChanged()
{
    LastRefreshStateKey.Reset();
    RefreshTree();
}

void UMassDspTechTreeWidget::RebuildNodeCards()
{
    if (!ScrollBox_Nodes) return;

    ScrollBox_Nodes->ClearChildren();
    NodeButtons.Reset();

    UMassDspTechTreeSubsystem* TechTree = GetTechTreeSubsystem();
    if (!TechTree) return;

    const UGameConfigData* GameConfig = nullptr;
    if (const UWorld* World = GetWorld())
    {
        if (const AMassDspGameMode* GameMode = Cast<AMassDspGameMode>(World->GetAuthGameMode()))
        {
            GameConfig = GameMode->GameConfig.Get();
        }
    }
    if (!GameConfig) return;

    TArray<ETechNodeId> SortedNodeIds;
    GameConfig->GetTechNodeConfigs().GetKeys(SortedNodeIds);
    SortedNodeIds.Sort([](ETechNodeId A, ETechNodeId B)
    {
        return static_cast<uint8>(A) < static_cast<uint8>(B);
    });

    const UEnum* TechEnum = StaticEnum<ETechNodeId>();

    for (const ETechNodeId NodeId : SortedNodeIds)
    {
        if (NodeId == ETechNodeId::None) continue;

        const FTechNodeConfig* NodeConfig = GameConfig->GetTechNodeConfig(NodeId);
        if (!NodeConfig) continue;

        UBorder* CardBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
        CardBorder->SetBrushColor(TechTree->IsNodeUnlocked(NodeId)
            ? FLinearColor(0.08f, 0.20f, 0.12f, 0.94f)
            : FLinearColor(0.10f, 0.11f, 0.15f, 0.94f));
        ScrollBox_Nodes->AddChild(CardBorder);

        UVerticalBox* CardBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
        CardBorder->SetContent(CardBox);

        UHorizontalBox* HeaderRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
        UVerticalBoxSlot* HeaderBoxSlot = CardBox->AddChildToVerticalBox(HeaderRow);
        HeaderBoxSlot->SetPadding(FMargin(14.f, 12.f, 14.f, 6.f));

        UTextBlock* TitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
        TitleText->SetText(NodeConfig->DisplayName.IsEmpty()
            ? FText::FromString(GetEnumDisplayName(TechEnum, static_cast<int64>(NodeId), TEXT("科技节点")))
            : NodeConfig->DisplayName);
        TitleText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
        HeaderRow->AddChildToHorizontalBox(TitleText);

        UTextBlock* StatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
        StatusText->SetText(BuildStatusText(NodeId));
        StatusText->SetColorAndOpacity(FSlateColor(GetStatusColor(NodeId)));
        UHorizontalBoxSlot* StatusSlot = HeaderRow->AddChildToHorizontalBox(StatusText);
        StatusSlot->SetPadding(FMargin(12.f, 0.f, 12.f, 0.f));
        StatusSlot->SetHorizontalAlignment(HAlign_Right);

        UMassDspTechNodeButton* UnlockButton = WidgetTree->ConstructWidget<UMassDspTechNodeButton>(UMassDspTechNodeButton::StaticClass());
        UnlockButton->NodeId = NodeId;
        UnlockButton->BindClickForwarder();
        UnlockButton->OnTechNodeClicked.RemoveDynamic(this, &UMassDspTechTreeWidget::OnNodeButtonClicked);
        UnlockButton->OnTechNodeClicked.AddDynamic(this, &UMassDspTechTreeWidget::OnNodeButtonClicked);
        UnlockButton->SetIsEnabled(!TechTree->IsNodeUnlocked(NodeId) && TechTree->CanUnlockNode(NodeId));
        NodeButtons.Add(UnlockButton);

        UHorizontalBoxSlot* ButtonSlot = HeaderRow->AddChildToHorizontalBox(UnlockButton);
        ButtonSlot->SetHorizontalAlignment(HAlign_Right);

        UTextBlock* ButtonText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
        ButtonText->SetText(TechTree->IsNodeUnlocked(NodeId)
            ? FText::FromString(TEXT("已解锁"))
            : FText::FromString(TEXT("研究")));
        ButtonText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
        UnlockButton->AddChild(ButtonText);

        UTextBlock* DescText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
        DescText->SetText(NodeConfig->Description);
        DescText->SetColorAndOpacity(FSlateColor(FLinearColor(0.88f, 0.90f, 0.95f)));
        UVerticalBoxSlot* DescSlot = CardBox->AddChildToVerticalBox(DescText);
        DescSlot->SetPadding(FMargin(14.f, 0.f, 14.f, 4.f));

        UTextBlock* PrereqText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
        PrereqText->SetText(FText::FromString(BuildPrerequisiteText(*NodeConfig)));
        PrereqText->SetColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.78f, 0.90f)));
        UVerticalBoxSlot* PrereqSlot = CardBox->AddChildToVerticalBox(PrereqText);
        PrereqSlot->SetPadding(FMargin(14.f, 0.f, 14.f, 2.f));

        UTextBlock* CostText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
        CostText->SetText(FText::FromString(BuildCostText(*NodeConfig)));
        CostText->SetColorAndOpacity(FSlateColor(FLinearColor(0.86f, 0.82f, 0.63f)));
        UVerticalBoxSlot* CostSlot = CardBox->AddChildToVerticalBox(CostText);
        CostSlot->SetPadding(FMargin(14.f, 0.f, 14.f, 2.f));

        UTextBlock* RewardText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
        RewardText->SetText(FText::FromString(BuildRewardText(*NodeConfig)));
        RewardText->SetColorAndOpacity(FSlateColor(FLinearColor(0.75f, 0.92f, 0.74f)));
        UVerticalBoxSlot* RewardSlot = CardBox->AddChildToVerticalBox(RewardText);
        RewardSlot->SetPadding(FMargin(14.f, 0.f, 14.f, 4.f));

        UTextBlock* StatusDetailText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
        if (!TechTree->IsNodeUnlocked(NodeId))
        {
            FText FailureReason;
            if (!TechTree->CanUnlockNode(NodeId, &FailureReason) && !FailureReason.IsEmpty())
            {
                StatusDetailText->SetText(FailureReason);
            }
            else
            {
                StatusDetailText->SetText(FText::FromString(TEXT("满足条件后可立即研究。")));
            }
        }
        else
        {
            StatusDetailText->SetText(FText::FromString(TEXT("该节点效果已生效。")));
        }
        StatusDetailText->SetColorAndOpacity(FSlateColor(FLinearColor(0.82f, 0.84f, 0.90f)));
        UVerticalBoxSlot* StatusDetailSlot = CardBox->AddChildToVerticalBox(StatusDetailText);
        StatusDetailSlot->SetPadding(FMargin(14.f, 0.f, 14.f, 12.f));
    }
}

FString UMassDspTechTreeWidget::BuildPrerequisiteText(const FTechNodeConfig& Config) const
{
    if (Config.Prerequisites.IsEmpty())
    {
        return TEXT("前置：无");
    }

    const UEnum* TechEnum = StaticEnum<ETechNodeId>();
    TArray<FString> Names;
    for (const ETechNodeId NodeId : Config.Prerequisites)
    {
        Names.Add(GetEnumDisplayName(TechEnum, static_cast<int64>(NodeId), TEXT("未知前置")));
    }

    return FString::Printf(TEXT("前置：%s"), *FString::Join(Names, TEXT("、")));
}

FString UMassDspTechTreeWidget::BuildCostText(const FTechNodeConfig& Config) const
{
    if (Config.ResearchCost.IsEmpty())
    {
        return TEXT("材料：无");
    }

    const UEnum* ItemEnum = StaticEnum<EItemType>();
    TArray<FString> CostEntries;
    for (const FRecipeEntry& Entry : Config.ResearchCost)
    {
        if (!Entry.IsValid()) continue;

        CostEntries.Add(FString::Printf(TEXT("%s x%d"),
            *GetEnumDisplayName(ItemEnum, static_cast<int64>(Entry.ItemType), TEXT("未知物品")),
            Entry.Amount));
    }

    return FString::Printf(TEXT("材料：%s"), CostEntries.IsEmpty() ? TEXT("无") : *FString::Join(CostEntries, TEXT("，")));
}

FString UMassDspTechTreeWidget::BuildRewardText(const FTechNodeConfig& Config) const
{
    const UEnum* BuildingEnum = StaticEnum<EBuildingType>();
    const UEnum* RecipeEnum = StaticEnum<ERecipeType>();
    const UEnum* BeltEnum = StaticEnum<EBeltType>();

    TArray<FString> RewardEntries;
    for (const FTechReward& Reward : Config.Rewards)
    {
        switch (Reward.RewardType)
        {
        case ETechRewardType::UnlockBuilding:
            RewardEntries.Add(FString::Printf(TEXT("建筑：%s"), *GetEnumDisplayName(BuildingEnum, static_cast<int64>(Reward.BuildingType), TEXT("未知建筑"))));
            break;
        case ETechRewardType::UnlockRecipe:
            RewardEntries.Add(FString::Printf(TEXT("配方：%s"), *GetEnumDisplayName(RecipeEnum, static_cast<int64>(Reward.RecipeType), TEXT("未知配方"))));
            break;
        case ETechRewardType::UnlockBelt:
            RewardEntries.Add(FString::Printf(TEXT("传送带：%s"), *GetEnumDisplayName(BeltEnum, static_cast<int64>(Reward.BeltType), TEXT("未知传送带"))));
            break;
        default:
            break;
        }
    }

    return FString::Printf(TEXT("奖励：%s"), RewardEntries.IsEmpty() ? TEXT("无") : *FString::Join(RewardEntries, TEXT("，")));
}

FText UMassDspTechTreeWidget::BuildStatusText(ETechNodeId NodeId) const
{
    UMassDspTechTreeSubsystem* TechTree = GetTechTreeSubsystem();
    if (!TechTree)
    {
        return FText::FromString(TEXT("不可用"));
    }

    if (TechTree->IsNodeUnlocked(NodeId))
    {
        return FText::FromString(TEXT("已解锁"));
    }

    return TechTree->CanUnlockNode(NodeId)
        ? FText::FromString(TEXT("可研究"))
        : FText::FromString(TEXT("未满足"));
}

FLinearColor UMassDspTechTreeWidget::GetStatusColor(ETechNodeId NodeId) const
{
    UMassDspTechTreeSubsystem* TechTree = GetTechTreeSubsystem();
    if (!TechTree)
    {
        return FLinearColor(0.82f, 0.55f, 0.55f);
    }

    if (TechTree->IsNodeUnlocked(NodeId))
    {
        return FLinearColor(0.56f, 1.0f, 0.68f);
    }

    return TechTree->CanUnlockNode(NodeId)
        ? FLinearColor(0.98f, 0.89f, 0.50f)
        : FLinearColor(0.85f, 0.72f, 0.72f);
}

UMassDspTechTreeSubsystem* UMassDspTechTreeWidget::GetTechTreeSubsystem() const
{
    return GetWorld() ? GetWorld()->GetSubsystem<UMassDspTechTreeSubsystem>() : nullptr;
}

void UMassDspTechTreeWidget::OnCloseButtonClicked()
{
    CloseWidget();
}

void UMassDspTechTreeWidget::OnNodeButtonClicked(UMassDspTechNodeButton* ClickedButton)
{
    UMassDspTechTreeSubsystem* TechTree = GetTechTreeSubsystem();
    if (!TechTree || !ClickedButton)
    {
        return;
    }

    FText FailureReason;
    if (!TechTree->TryUnlockNode(ClickedButton->NodeId, &FailureReason) && !FailureReason.IsEmpty() && GEngine)
    {
        GEngine->AddOnScreenDebugMessage(INDEX_NONE, 2.5f, FColor::Yellow, FailureReason.ToString());
    }

    LastRefreshStateKey.Reset();
    RefreshTree();
}