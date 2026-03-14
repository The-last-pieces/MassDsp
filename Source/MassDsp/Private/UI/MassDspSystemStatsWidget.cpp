#include "UI/MassDspSystemStatsWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "GameFramework/PlayerController.h"
#include "MassDspHUD.h"
#include "Subsystems/MassDspDebugStatsSubsystem.h"

void UMassDspSystemStatsWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (Button_Close && !Button_Close->OnClicked.IsBound())
    {
        Button_Close->OnClicked.AddDynamic(this, &UMassDspSystemStatsWidget::OnCloseButtonClicked);
    }

    if (Button_Refresh && !Button_Refresh->OnClicked.IsBound())
    {
        Button_Refresh->OnClicked.AddDynamic(this, &UMassDspSystemStatsWidget::OnRefreshButtonClicked);
    }

    if (TextBlock_Title)
    {
        TextBlock_Title->SetText(FText::FromString(TEXT("系统统计 / 调试面板")));
    }

    RefreshAccum = RefreshInterval;
}

void UMassDspSystemStatsWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

    RefreshAccum += InDeltaTime;
    if (RefreshAccum >= RefreshInterval)
    {
        RefreshAccum = 0.f;
        RefreshStats();
    }
}

void UMassDspSystemStatsWidget::CloseWidget()
{
    RemoveFromParent();

    APlayerController* PC = GetOwningPlayer();
    if (!PC) return;

    FInputModeGameOnly InputMode;
    PC->SetInputMode(InputMode);
    PC->bShowMouseCursor = false;
}

void UMassDspSystemStatsWidget::RefreshStats()
{
    const UWorld* World = GetWorld();
    AMassDspHUD* HUD = GetOwningPlayer() ? Cast<AMassDspHUD>(GetOwningPlayer()->GetHUD()) : nullptr;
    UMassDspDebugStatsSubsystem* Stats = World ? World->GetSubsystem<UMassDspDebugStatsSubsystem>() : nullptr;
    if (!HUD || !Stats) return;

    const FMassDspDebugStatsSnapshot& Snapshot = Stats->GetSnapshot();

    if (TextBlock_Performance)
    {
        TextBlock_Performance->SetText(FText::FromString(FString::Printf(
            TEXT("FPS\n当前 %.1f | 平均 %.1f | 1%% Low %.1f | 采样 %.2fs"),
            World && World->GetDeltaSeconds() > 0.f ? 1.f / World->GetDeltaSeconds() : 0.f,
            HUD->AverageFPS,
            HUD->OnePercentLowFPS,
            Snapshot.SampleIntervalSeconds)));
    }

    if (TextBlock_WorldScale)
    {
        TextBlock_WorldScale->SetText(FText::FromString(FString::Printf(
            TEXT("世界规模\n建筑 %d  [矿机 %d / 合成 %d / 仓库 %d / 物流塔 %d]\n传送带 %d | 带上物品 %d | 理论吞吐 %.1f 物品/秒"),
            Snapshot.TotalBuildings,
            Snapshot.MinerCount,
            Snapshot.AssemblerCount,
            Snapshot.StorageCount,
            Snapshot.LogisticsTowerCount,
            Snapshot.TotalBelts,
            Snapshot.TotalBeltItems,
            Snapshot.EstimatedBeltThroughputPerSecond)));
    }

    if (TextBlock_Logistics)
    {
        TextBlock_Logistics->SetText(FText::FromString(FString::Printf(
            TEXT("物流态势\n无人机 %d | 空闲 %d | 活跃任务 %d | 待处理请求 %d\n阻塞带 %d | 缺料合成台 %d | 满载节点 %d"),
            Snapshot.TotalDrones,
            Snapshot.IdleDrones,
            Snapshot.ActiveTasks,
            Snapshot.PendingRequests,
            Snapshot.BlockedBelts,
            Snapshot.StarvedAssemblers,
            Snapshot.FullMinerNodes + Snapshot.FullStorageNodes + Snapshot.FullLogisticsTowers)));
    }

    if (TextBlock_Bottleneck)
    {
        TextBlock_Bottleneck->SetText(FText::FromString(FString::Printf(TEXT("瓶颈摘要\n%s"), *Snapshot.BottleneckSummary)));
    }

    if (TextBlock_BusiestTower)
    {
        TextBlock_BusiestTower->SetText(FText::FromString(FString::Printf(TEXT("最忙物流塔\n%s"), *Snapshot.BusiestTowerSummary)));
    }

    UTextBlock* DeltaBlocks[4] = {
        TextBlock_ItemDelta_0,
        TextBlock_ItemDelta_1,
        TextBlock_ItemDelta_2,
        TextBlock_ItemDelta_3,
    };
    const UEnum* ItemEnum = StaticEnum<EItemType>();
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(DeltaBlocks); ++Index)
    {
        UTextBlock* TextBlock = DeltaBlocks[Index];
        if (!TextBlock) continue;

        if (Snapshot.TopItemDeltas.IsValidIndex(Index))
        {
            const FMassDspItemDeltaStat& Delta = Snapshot.TopItemDeltas[Index];
            const FString ItemName = ItemEnum
                ? ItemEnum->GetDisplayNameTextByValue(static_cast<int64>(Delta.ItemType)).ToString()
                : TEXT("Unknown");
            TextBlock->SetText(FText::FromString(FString::Printf(
                TEXT("%s  %+.1f /s  [现存 %d]"),
                *ItemName,
                Delta.DeltaPerSecond,
                Delta.CurrentCount)));
        }
        else
        {
            TextBlock->SetText(FText::FromString(Index == 0 ? TEXT("暂无显著物品变化") : TEXT("")));
        }
    }
}

void UMassDspSystemStatsWidget::OnCloseButtonClicked()
{
    CloseWidget();
}

void UMassDspSystemStatsWidget::OnRefreshButtonClicked()
{
    if (UWorld* World = GetWorld())
    {
        if (UMassDspDebugStatsSubsystem* Stats = World->GetSubsystem<UMassDspDebugStatsSubsystem>())
        {
            Stats->ForceRefresh();
        }
    }
    RefreshStats();
}