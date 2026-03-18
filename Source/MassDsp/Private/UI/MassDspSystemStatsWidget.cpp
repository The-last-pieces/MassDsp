#include "UI/MassDspSystemStatsWidget.h"

#include "Components/Button.h"
#include "Components/ListView.h"
#include "Components/TextBlock.h"
#include "GameFramework/PlayerController.h"
#include "MassDspHUD.h"
#include "Subsystems/MassDspDebugStatsSubsystem.h"
#include "UI/MassDspSystemStatsRowData.h"
#include "UI/MassDspSystemStatsRowWidget.h"

void UMassDspSystemStatsWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (UWorld* World = GetWorld())
    {
        if (UMassDspDebugStatsSubsystem* Stats = World->GetSubsystem<UMassDspDebugStatsSubsystem>())
        {
            Stats->RegisterStatsConsumer();
            Stats->ForceRefresh();
        }
    }

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

    if (ListView_Stats)
    {
        // TODO 蓝图里手动绑一下 EntryWidgetClass = BP_SystemStatsRow
        ListView_Stats->ClearListItems();
    }

    RefreshAccum = RefreshInterval;
}

void UMassDspSystemStatsWidget::NativeDestruct()
{
    if (UWorld* World = GetWorld())
    {
        if (UMassDspDebugStatsSubsystem* Stats = World->GetSubsystem<UMassDspDebugStatsSubsystem>())
        {
            Stats->UnregisterStatsConsumer();
        }
    }

    Super::NativeDestruct();
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
    ResetListItems();

    AddStatRow(
        FText::FromString(TEXT("性能概览")),
        FText::FromString(FString::Printf(
            TEXT("当前 %.1f FPS | 平均 %.1f FPS | 1%% Low %.1f FPS"),
            World && World->GetDeltaSeconds() > 0.f ? 1.f / World->GetDeltaSeconds() : 0.f,
            HUD->AverageFPS,
            HUD->OnePercentLowFPS)),
        FText::FromString(FString::Printf(TEXT("采样窗口 %.2fs"), Snapshot.SampleIntervalSeconds)));

    AddStatRow(
        FText::FromString(TEXT("世界规模")),
        FText::FromString(FString::Printf(TEXT("建筑 %d | 传送带 %d | 带上物品 %d"), Snapshot.TotalBuildings, Snapshot.TotalBelts, Snapshot.TotalBeltItems)),
        FText::FromString(FString::Printf(
            TEXT("矿机 %d | 合成台 %d | 仓库 %d | 物流塔 %d | 理论吞吐 %.1f 物品/秒"),
            Snapshot.MinerCount,
            Snapshot.AssemblerCount,
            Snapshot.StorageCount,
            Snapshot.LogisticsTowerCount,
            Snapshot.EstimatedBeltThroughputPerSecond)));

    AddStatRow(
        FText::FromString(TEXT("物流态势")),
        FText::FromString(FString::Printf(TEXT("无人机 %d | 空闲 %d | 活跃任务 %d | 待处理请求 %d"), Snapshot.TotalDrones, Snapshot.IdleDrones, Snapshot.ActiveTasks, Snapshot.PendingRequests)),
        FText::FromString(FString::Printf(
            TEXT("阻塞带 %d | 缺料合成台 %d | 满载节点 %d"),
            Snapshot.BlockedBelts,
            Snapshot.StarvedAssemblers,
            Snapshot.FullMinerNodes + Snapshot.FullStorageNodes + Snapshot.FullLogisticsTowers)));

    AddStatRow(
        FText::FromString(TEXT("瓶颈摘要")),
        FText::FromString(Snapshot.BottleneckSummary),
        FText::FromString(TEXT("优先关注上方物流和库存态势指标")));

    for (const FMassDspModuleProfileStat& ModuleStat : Snapshot.ModuleProfileStats)
    {
        AddStatRow(
            FText::FromString(FString::Printf(TEXT("模块 Profile · %s"), *ModuleStat.ModuleName)),
            FText::FromString(FString::Printf(
                TEXT("窗口 %.2f ms | 帧占比 %.1f%%"),
                ModuleStat.TotalMilliseconds,
                ModuleStat.FrameSharePercent)),
            FText::FromString(FString::Printf(
                TEXT("平均 %.3f ms / 次 | 调用 %d 次"),
                ModuleStat.AverageMilliseconds,
                ModuleStat.SampleCount)));
    }

    if (Snapshot.ModuleProfileStats.IsEmpty())
    {
        AddStatRow(
            FText::FromString(TEXT("模块 Profile")),
            FText::FromString(TEXT("当前采样窗口暂无模块耗时数据")),
            FText::FromString(TEXT("保持面板开启一小段时间后会显示主要模块的耗时与帧占比")));
    }

    for (const FMassDspItemDeltaStat& Delta : Snapshot.ItemStats)
    {
        const FString ItemName = MassDspEnumText::GetItemType(Delta.ItemType).ToString();
        AddStatRow(
            FText::FromString(FString::Printf(TEXT("物品指标 · %s"), *ItemName)),
            FText::FromString(FString::Printf(
                TEXT("净增长 %+.2f /s | 现存 %d"),
                Delta.NetGrowthPerSecond,
                Delta.CurrentCount)),
            FText::FromString(FString::Printf(
                TEXT("生产 %.2f /s | 消耗 %.2f /s"),
                Delta.ProductionPerSecond,
                Delta.ConsumptionPerSecond)));
    }

    if (Snapshot.ItemStats.IsEmpty())
    {
        AddStatRow(
            FText::FromString(TEXT("物品指标")),
            FText::FromString(TEXT("暂无有效物品变化")),
            FText::FromString(TEXT("等待采样窗口积累更多生产/消耗数据")));
    }
}

void UMassDspSystemStatsWidget::ResetListItems()
{
    RowItems.Reset();
    if (ListView_Stats)
    {
        ListView_Stats->ClearListItems();
    }
}

void UMassDspSystemStatsWidget::AddStatRow(const FText& Title, const FText& Value, const FText& Details)
{
    if (!ListView_Stats)
    {
        return;
    }

    UMassDspSystemStatsRowData* RowData = NewObject<UMassDspSystemStatsRowData>(this);
    RowData->Title = Title;
    RowData->Value = Value;
    RowData->Details = Details;
    RowItems.Add(RowData);
    ListView_Stats->AddItem(RowData);
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
