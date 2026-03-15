#include "MassDspGameInstance.h"

#include "Async/Async.h"
#include "GameFramework/GameUserSettings.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"

#include "Inventory/MassDspPlayerInventoryComponent.h"
#include "Save/MassDspSaveData.h"
#include "Subsystems/MassDspManager.h"
#include "Subsystems/MassDspLogisticsSubsystem.h"
#include "Subsystems/MassDspTechTreeSubsystem.h"

namespace
{
    UMassDspPlayerInventoryComponent* ResolvePlayerInventory(UWorld* World)
    {
        if (!World) return nullptr;
        APlayerController* PC = World->GetFirstPlayerController();
        if (APawn* Pawn = PC ? PC->GetPawn() : nullptr)
        {
            UMassDspPlayerInventoryComponent* InvComp = Pawn->FindComponentByClass<UMassDspPlayerInventoryComponent>();
            if (!InvComp)
            {
                InvComp = NewObject<UMassDspPlayerInventoryComponent>(Pawn);
                InvComp->RegisterComponent();
            }
            return InvComp;
        }
        return nullptr;
    }
}

void UMassDspGameInstance::Init()
{
    Super::Init();

    // 1. 彻底关闭景深 (Depth of Field)，保证远景工厂绝对清晰
    if (IConsoleVariable* CVarDoF = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DepthOfFieldQuality")))
    {
        CVarDoF->Set(0, ECVF_SetByCode);
    }

    // // 2. 注入后期锐化，对抗 TAA/TSR 带来的时序涂抹感 (数值 1.0 ~ 3.0)
    if (IConsoleVariable* CVarSharpen = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Tonemapper.Sharpen")))
    {
        CVarSharpen->Set(2.0f, ECVF_SetByCode);
    }
    //
    // // 3. 限制动态分辨率下限 (保底 75%)，防止 136 万实体同屏时引擎把画面降得太糊
    if (IConsoleVariable* CVarDynResMin = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DynamicRes.MinScreenPercentage")))
    {
        CVarDynResMin->Set(75.0f, ECVF_SetByCode);
    }

    // 4. 开启 16x 各向异性过滤，拯救高空俯视视角的地面贴图模糊 (几乎 0 性能开销)
    if (IConsoleVariable* CVarAnisotropy = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MaxAnisotropy")))
    {
        CVarAnisotropy->Set(16, ECVF_SetByCode);
    }

    // 帧率无上限
    if (IConsoleVariable* CVarMaxFPS = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS")))
    {
        CVarMaxFPS->Set(0, ECVF_SetByCode);
    }
    if (IConsoleVariable* CVarMaxFPS = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync")))
    {
        CVarMaxFPS->Set(0, ECVF_SetByCode);
    }

#if UE_BUILD_SHIPPING
    if (UGameUserSettings* Settings = GEngine->GetGameUserSettings())
    {
        Settings->SetFullscreenMode(EWindowMode::WindowedFullscreen);
        Settings->SetScreenResolution(FIntPoint(0, 0)); // 可不设，关键是下面的 ApplySettings
        Settings->ApplySettings(false);
        Settings->SaveSettings();
    }
    // 在 GameInstance::Init 阶段设置窗口模式，早于地图加载，避免启动时短暂全屏
#endif
}

bool UMassDspGameInstance::SaveGameToSlot(const FString& SlotName, int32 UserIndex)
{
    if (IsSaveLoadRequestInFlight())
    {
        return false;
    }

    UMassDspSaveGame* SaveGameObject = Cast<UMassDspSaveGame>(UGameplayStatics::CreateSaveGameObject(UMassDspSaveGame::StaticClass()));
    if (!SaveGameObject)
    {
        return false;
    }

    if (!CollectCurrentState(*SaveGameObject))
    {
        return false;
    }

    return UGameplayStatics::SaveGameToSlot(SaveGameObject, SlotName, UserIndex);
}

bool UMassDspGameInstance::LoadGameFromSlot(const FString& SlotName, int32 UserIndex)
{
    if (IsSaveLoadRequestInFlight())
    {
        return false;
    }

    USaveGame* RawSaveGame = UGameplayStatics::LoadGameFromSlot(SlotName, UserIndex);
    UMassDspSaveGame* SaveGameObject = Cast<UMassDspSaveGame>(RawSaveGame);
    if (!SaveGameObject)
    {
        return false;
    }

    if (!UpgradeSaveGameToCurrentVersion(*SaveGameObject))
    {
        return false;
    }

    return RestoreCurrentState(*SaveGameObject);
}

bool UMassDspGameInstance::DoesSaveExist(const FString& SlotName, int32 UserIndex) const
{
    return UGameplayStatics::DoesSaveGameExist(SlotName, UserIndex);
}

bool UMassDspGameInstance::SaveGameToSlotAsync(const FString& SlotName, int32 UserIndex)
{
    if (!TryBeginAsyncOperation(EMassDspAsyncSaveLoadOperation::Save, SlotName, UserIndex))
    {
        return false;
    }

    UMassDspSaveGame* SaveGameObject = Cast<UMassDspSaveGame>(UGameplayStatics::CreateSaveGameObject(UMassDspSaveGame::StaticClass()));
    if (!SaveGameObject)
    {
        CompleteAsyncOperation(EMassDspAsyncSaveLoadOperation::Save, SlotName, UserIndex, false);
        return false;
    }

    const double CollectStartSeconds = FPlatformTime::Seconds();
    if (!CollectCurrentState(*SaveGameObject))
    {
        CompleteAsyncOperation(EMassDspAsyncSaveLoadOperation::Save, SlotName, UserIndex, false);
        return false;
    }
    ActiveCollectMs = (FPlatformTime::Seconds() - CollectStartSeconds) * 1000.0;

    SaveGameObject->AddToRoot();

    TWeakObjectPtr<UMassDspGameInstance> WeakThis(this);
    Async(EAsyncExecution::ThreadPool, [WeakThis, SaveGameObject, SlotName, UserIndex]()
    {
        TArray<uint8> SaveDataBuffer;
        const double SerializeStartSeconds = FPlatformTime::Seconds();
        const bool bSerialized = UGameplayStatics::SaveGameToMemory(SaveGameObject, SaveDataBuffer);
        const double SerializeMs = (FPlatformTime::Seconds() - SerializeStartSeconds) * 1000.0;

        const double IoStartSeconds = FPlatformTime::Seconds();
        const bool bSaved = bSerialized && UGameplayStatics::SaveDataToSlot(SaveDataBuffer, SlotName, UserIndex);
        const double IoMs = (FPlatformTime::Seconds() - IoStartSeconds) * 1000.0;

        const int64 DataBytes = SaveDataBuffer.Num();

        AsyncTask(ENamedThreads::GameThread, [WeakThis, SaveGameObject, SlotName, UserIndex, bSaved, SerializeMs, IoMs, DataBytes]()
        {
            SaveGameObject->RemoveFromRoot();

            if (WeakThis.IsValid())
            {
                WeakThis->HandleAsyncSaveFinished(SlotName, UserIndex, bSaved, SerializeMs, IoMs, DataBytes);
            }
        });
    });

    return true;
}

bool UMassDspGameInstance::LoadGameFromSlotAsync(const FString& SlotName, int32 UserIndex)
{
    if (!DoesSaveExist(SlotName, UserIndex))
    {
        return false;
    }

    if (!TryBeginAsyncOperation(EMassDspAsyncSaveLoadOperation::Load, SlotName, UserIndex))
    {
        return false;
    }

    TWeakObjectPtr<UMassDspGameInstance> WeakThis(this);
    Async(EAsyncExecution::ThreadPool, [WeakThis, SlotName, UserIndex]()
    {
        TArray<uint8> SaveDataBuffer;
        const double IoStartSeconds = FPlatformTime::Seconds();
        const bool bSucceeded = UGameplayStatics::LoadDataFromSlot(SaveDataBuffer, SlotName, UserIndex);
        const double IoMs = (FPlatformTime::Seconds() - IoStartSeconds) * 1000.0;
        const int64 DataBytes = SaveDataBuffer.Num();

        UMassDspSaveGame* LoadedSaveGame = nullptr;
        double DeserializeMs = 0.0;
        double UpgradeMs = 0.0;
        bool bPreparedForRestore = false;

        if (bSucceeded)
        {
            const double DeserializeStartSeconds = FPlatformTime::Seconds();
            LoadedSaveGame = Cast<UMassDspSaveGame>(UGameplayStatics::LoadGameFromMemory(SaveDataBuffer));
            DeserializeMs = (FPlatformTime::Seconds() - DeserializeStartSeconds) * 1000.0;

            if (LoadedSaveGame && WeakThis.IsValid())
            {
                const double UpgradeStartSeconds = FPlatformTime::Seconds();
                bPreparedForRestore = WeakThis->UpgradeSaveGameToCurrentVersion(*LoadedSaveGame);
                UpgradeMs = (FPlatformTime::Seconds() - UpgradeStartSeconds) * 1000.0;
            }
        }

        AsyncTask(ENamedThreads::GameThread, [WeakThis, SlotName, UserIndex, bPreparedForRestore, LoadedSaveGame, IoMs, DeserializeMs, UpgradeMs, DataBytes]()
        {
            if (WeakThis.IsValid())
            {
                WeakThis->HandleAsyncLoadFinished(SlotName, UserIndex, bPreparedForRestore, LoadedSaveGame, IoMs, DeserializeMs, UpgradeMs, DataBytes);
            }
        });
    });

    return true;
}

bool UMassDspGameInstance::IsSaveLoadRequestInFlight() const
{
    return ActiveAsyncOperation != EMassDspAsyncSaveLoadOperation::None;
}

bool UMassDspGameInstance::IsAsyncSaving() const
{
    return ActiveAsyncOperation == EMassDspAsyncSaveLoadOperation::Save;
}

bool UMassDspGameInstance::IsAsyncLoading() const
{
    return ActiveAsyncOperation == EMassDspAsyncSaveLoadOperation::Load;
}

FString UMassDspGameInstance::GetActiveSaveLoadOperationName() const
{
    switch (ActiveAsyncOperation)
    {
    case EMassDspAsyncSaveLoadOperation::Save:
        return TEXT("saving");
    case EMassDspAsyncSaveLoadOperation::Load:
        return TEXT("loading");
    default:
        return TEXT("idle");
    }
}

bool UMassDspGameInstance::TryBeginAsyncOperation(EMassDspAsyncSaveLoadOperation Operation, const FString& SlotName, int32 UserIndex)
{
    if (IsSaveLoadRequestInFlight())
    {
        return false;
    }

    ActiveAsyncOperation = Operation;
    ActiveAsyncSlotName = SlotName;
    ActiveAsyncUserIndex = UserIndex;
    ActiveAsyncStartSeconds = FPlatformTime::Seconds();
    return true;
}

FMassDspAsyncSaveLoadResult UMassDspGameInstance::CompleteAsyncOperation(
    EMassDspAsyncSaveLoadOperation Operation,
    const FString& SlotName,
    int32 UserIndex,
    bool bSucceeded,
    const FMassDspAsyncSaveLoadResult* PhaseStats)
{
    FMassDspAsyncSaveLoadResult Result;
    Result.Operation = Operation;
    Result.SlotName = SlotName;
    Result.UserIndex = UserIndex;
    Result.bSucceeded = bSucceeded;
    Result.ElapsedMs = (FPlatformTime::Seconds() - ActiveAsyncStartSeconds) * 1000.0;
    Result.CollectMs = ActiveCollectMs;

    if (PhaseStats)
    {
        Result.SerializeMs = PhaseStats->SerializeMs;
        Result.IoMs = PhaseStats->IoMs;
        Result.DeserializeMs = PhaseStats->DeserializeMs;
        Result.UpgradeMs = PhaseStats->UpgradeMs;
        Result.RestoreMs = PhaseStats->RestoreMs;
        Result.DataBytes = PhaseStats->DataBytes;
    }

    ActiveAsyncOperation = EMassDspAsyncSaveLoadOperation::None;
    ActiveAsyncSlotName.Reset();
    ActiveAsyncUserIndex = 0;
    ActiveAsyncStartSeconds = 0.0;
    ActiveCollectMs = 0.0;
    return Result;
}

void UMassDspGameInstance::LogAsyncSaveLoadBreakdown(const FMassDspAsyncSaveLoadResult& Result) const
{
    const TCHAR* OperationName = Result.Operation == EMassDspAsyncSaveLoadOperation::Save ? TEXT("Save") : TEXT("Load");
    UE_LOG(
        LogTemp,
        Log,
        TEXT("[SaveDebug] %s Breakdown: slot=%s user=%d success=%s total=%.2f ms collect=%.2f ms serialize=%.2f ms io=%.2f ms deserialize=%.2f ms upgrade=%.2f ms restore=%.2f ms bytes=%lld"),
        OperationName,
        *Result.SlotName,
        Result.UserIndex,
        Result.bSucceeded ? TEXT("true") : TEXT("false"),
        Result.ElapsedMs,
        Result.CollectMs,
        Result.SerializeMs,
        Result.IoMs,
        Result.DeserializeMs,
        Result.UpgradeMs,
        Result.RestoreMs,
        Result.DataBytes);
}

void UMassDspGameInstance::HandleAsyncSaveFinished(const FString& SlotName, int32 UserIndex, bool bSucceeded, double SerializeMs, double IoMs, int64 DataBytes)
{
    FMassDspAsyncSaveLoadResult PhaseStats;
    PhaseStats.SerializeMs = SerializeMs;
    PhaseStats.IoMs = IoMs;
    PhaseStats.DataBytes = DataBytes;

    const FMassDspAsyncSaveLoadResult Result = CompleteAsyncOperation(
        EMassDspAsyncSaveLoadOperation::Save,
        SlotName,
        UserIndex,
        bSucceeded,
        &PhaseStats);
    LogAsyncSaveLoadBreakdown(Result);
    AsyncSaveFinishedEvent.Broadcast(Result);
}

void UMassDspGameInstance::HandleAsyncLoadFinished(
    const FString& SlotName,
    int32 UserIndex,
    bool bSucceeded,
    UMassDspSaveGame* LoadedSaveGame,
    double IoMs,
    double DeserializeMs,
    double UpgradeMs,
    int64 DataBytes)
{
    bool bRestoreSucceeded = false;
    FMassDspAsyncSaveLoadResult PhaseStats;
    PhaseStats.IoMs = IoMs;
    PhaseStats.DeserializeMs = DeserializeMs;
    PhaseStats.UpgradeMs = UpgradeMs;
    PhaseStats.DataBytes = DataBytes;

    if (bSucceeded && LoadedSaveGame)
    {
        const double RestoreStartSeconds = FPlatformTime::Seconds();
        bRestoreSucceeded = RestoreCurrentState(*LoadedSaveGame);
        PhaseStats.RestoreMs = (FPlatformTime::Seconds() - RestoreStartSeconds) * 1000.0;
    }

    const FMassDspAsyncSaveLoadResult Result = CompleteAsyncOperation(
        EMassDspAsyncSaveLoadOperation::Load,
        SlotName,
        UserIndex,
        bRestoreSucceeded,
        &PhaseStats);
    LogAsyncSaveLoadBreakdown(Result);
    AsyncLoadFinishedEvent.Broadcast(Result);
}

bool UMassDspGameInstance::CollectCurrentState(UMassDspSaveGame& OutSaveGame) const
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return false;
    }

    UMassDspManager* Manager = World->GetSubsystem<UMassDspManager>();
    UMassDspLogisticsSubsystem* Logistics = World->GetSubsystem<UMassDspLogisticsSubsystem>();
    UMassDspTechTreeSubsystem* TechTree = World->GetSubsystem<UMassDspTechTreeSubsystem>();
    UMassDspPlayerInventoryComponent* PlayerInventory = ResolvePlayerInventory(World);
    if (!Manager || !Logistics || !TechTree || !PlayerInventory)
    {
        return false;
    }

    OutSaveGame.Header.SaveVersion = UMassDspSaveGame::CurrentSaveVersion;
    OutSaveGame.Header.MapName = World->GetMapName();
    OutSaveGame.Header.SavedAtUtc = FDateTime::UtcNow();

    Manager->CollectBuildingSaveData(OutSaveGame.Buildings);
    Manager->CollectBeltSaveData(OutSaveGame.Belts);
    Logistics->CollectSaveData(OutSaveGame.Logistics);

    OutSaveGame.PlayerInventory.MaxInventoryItems = PlayerInventory->GetCapacity();
    OutSaveGame.PlayerInventory.ItemStacks.Reset();

    TArray<FInventoryEntryView> InventoryEntries;
    PlayerInventory->GetActiveEntries(InventoryEntries);
    for (const FInventoryEntryView& Entry : InventoryEntries)
    {
        if (Entry.ItemType == EItemType::None || Entry.Quantity <= 0)
        {
            continue;
        }

        FMassDspItemStackSaveData Stack;
        Stack.ItemType = Entry.ItemType;
        Stack.Quantity = Entry.Quantity;
        OutSaveGame.PlayerInventory.ItemStacks.Add(Stack);
    }

    const FMassDspPlayerTechState& TechState = TechTree->GetPlayerTechState();
    OutSaveGame.TechTree.Version = TechState.Version;
    OutSaveGame.TechTree.CurrentResearchNode = TechState.CurrentResearchNode;
    OutSaveGame.TechTree.ResearchProgress = TechState.ResearchProgress;
    OutSaveGame.TechTree.UnlockedNodes.Reset();
    for (const ETechNodeId NodeId : TechState.UnlockedNodes)
    {
        OutSaveGame.TechTree.UnlockedNodes.Add(NodeId);
    }

    return true;
}

bool UMassDspGameInstance::RestoreCurrentState(const UMassDspSaveGame& InSaveGame)
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return false;
    }

    UMassDspManager* Manager = World->GetSubsystem<UMassDspManager>();
    UMassDspLogisticsSubsystem* Logistics = World->GetSubsystem<UMassDspLogisticsSubsystem>();
    UMassDspTechTreeSubsystem* TechTree = World->GetSubsystem<UMassDspTechTreeSubsystem>();
    UMassDspPlayerInventoryComponent* PlayerInventory = ResolvePlayerInventory(World);
    if (!Manager || !Logistics || !TechTree || !PlayerInventory)
    {
        return false;
    }

    if (!Manager->RestoreBuildingSaveData(InSaveGame.Buildings))
    {
        return false;
    }

    if (!Manager->RestoreBeltSaveData(InSaveGame.Belts))
    {
        return false;
    }

    if (!Logistics->RestoreSaveData(InSaveGame.Logistics))
    {
        return false;
    }

    TArray<FInventoryEntryView> InventoryEntries;
    InventoryEntries.Reserve(InSaveGame.PlayerInventory.ItemStacks.Num());
    for (const FMassDspItemStackSaveData& Stack : InSaveGame.PlayerInventory.ItemStacks)
    {
        FInventoryEntryView Entry;
        Entry.ItemType = Stack.ItemType;
        Entry.Quantity = Stack.Quantity;
        InventoryEntries.Add(Entry);
    }
    PlayerInventory->RestoreInventorySnapshot(InSaveGame.PlayerInventory.MaxInventoryItems, InventoryEntries);

    FMassDspPlayerTechState TechState;
    TechState.Version = InSaveGame.TechTree.Version;
    TechState.CurrentResearchNode = InSaveGame.TechTree.CurrentResearchNode;
    TechState.ResearchProgress = InSaveGame.TechTree.ResearchProgress;
    for (const ETechNodeId NodeId : InSaveGame.TechTree.UnlockedNodes)
    {
        TechState.UnlockedNodes.Add(NodeId);
    }
    TechTree->RestorePlayerTechState(TechState);

    return true;
}

bool UMassDspGameInstance::UpgradeSaveGameToCurrentVersion(UMassDspSaveGame& InOutSaveGame) const
{
    if (InOutSaveGame.Header.SaveVersion > UMassDspSaveGame::CurrentSaveVersion)
    {
        return false;
    }

    while (InOutSaveGame.Header.SaveVersion < UMassDspSaveGame::CurrentSaveVersion)
    {
        switch (InOutSaveGame.Header.SaveVersion)
        {
        case 0:
            InOutSaveGame.Header.SaveVersion = 1;
            break;
        default:
            return false;
        }
    }

    return true;
}
