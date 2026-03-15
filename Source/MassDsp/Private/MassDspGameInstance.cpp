#include "MassDspGameInstance.h"

#include "GameFramework/GameUserSettings.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"

#include "Inventory/MassDspPlayerInventoryComponent.h"
#include "Save/MassDspSaveData.h"
#include "Subsystems/MassDspManager.h"
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

bool UMassDspGameInstance::CollectCurrentState(UMassDspSaveGame& OutSaveGame) const
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return false;
    }

    UMassDspManager* Manager = World->GetSubsystem<UMassDspManager>();
    UMassDspTechTreeSubsystem* TechTree = World->GetSubsystem<UMassDspTechTreeSubsystem>();
    UMassDspPlayerInventoryComponent* PlayerInventory = ResolvePlayerInventory(World);
    if (!Manager || !TechTree || !PlayerInventory)
    {
        return false;
    }

    OutSaveGame.Header.SaveVersion = UMassDspSaveGame::CurrentSaveVersion;
    OutSaveGame.Header.MapName = World->GetMapName();
    OutSaveGame.Header.SavedAtUtc = FDateTime::UtcNow();

    Manager->CollectBuildingSaveData(OutSaveGame.Buildings);

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
    UMassDspTechTreeSubsystem* TechTree = World->GetSubsystem<UMassDspTechTreeSubsystem>();
    UMassDspPlayerInventoryComponent* PlayerInventory = ResolvePlayerInventory(World);
    if (!Manager || !TechTree || !PlayerInventory)
    {
        return false;
    }

    if (!Manager->RestoreBuildingSaveData(InSaveGame.Buildings))
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
