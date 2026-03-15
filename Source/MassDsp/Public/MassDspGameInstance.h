#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"

#include "MassDspGameInstance.generated.h"

class UMassDspSaveGame;

enum class EMassDspAsyncSaveLoadOperation : uint8
{
    None,
    Save,
    Load,
};

struct FMassDspAsyncSaveLoadResult
{
    EMassDspAsyncSaveLoadOperation Operation = EMassDspAsyncSaveLoadOperation::None;
    FString SlotName;
    int32 UserIndex = 0;
    bool bSucceeded = false;
    double ElapsedMs = 0.0;
    double CollectMs = 0.0;
    double SerializeMs = 0.0;
    double IoMs = 0.0;
    double DeserializeMs = 0.0;
    double UpgradeMs = 0.0;
    double RestoreMs = 0.0;
    int64 DataBytes = 0;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FMassDspAsyncSaveFinished, const FMassDspAsyncSaveLoadResult&);
DECLARE_MULTICAST_DELEGATE_OneParam(FMassDspAsyncLoadFinished, const FMassDspAsyncSaveLoadResult&);

UCLASS()
class MASSDSP_API UMassDspGameInstance : public UGameInstance
{
    GENERATED_BODY()

public:
    static constexpr TCHAR DebugQuickSaveSlotName[] = TEXT("QuickSave_Debug");

    virtual void Init() override;

    UFUNCTION(BlueprintCallable, Category = "MassDsp|Save")
    bool SaveGameToSlot(const FString& SlotName = TEXT("QuickSave"), int32 UserIndex = 0);

    UFUNCTION(BlueprintCallable, Category = "MassDsp|Save")
    bool LoadGameFromSlot(const FString& SlotName = TEXT("QuickSave"), int32 UserIndex = 0);

    UFUNCTION(BlueprintCallable, Category = "MassDsp|Save")
    bool SaveGameToSlotAsync(const FString& SlotName = TEXT("QuickSave"), int32 UserIndex = 0);

    UFUNCTION(BlueprintCallable, Category = "MassDsp|Save")
    bool LoadGameFromSlotAsync(const FString& SlotName = TEXT("QuickSave"), int32 UserIndex = 0);

    UFUNCTION(BlueprintPure, Category = "MassDsp|Save")
    bool DoesSaveExist(const FString& SlotName = TEXT("QuickSave"), int32 UserIndex = 0) const;

    UFUNCTION(BlueprintPure, Category = "MassDsp|Save")
    bool IsSaveLoadRequestInFlight() const;

    UFUNCTION(BlueprintPure, Category = "MassDsp|Save")
    bool IsAsyncSaving() const;

    UFUNCTION(BlueprintPure, Category = "MassDsp|Save")
    bool IsAsyncLoading() const;

    UFUNCTION(BlueprintPure, Category = "MassDsp|Save")
    FString GetActiveSaveLoadOperationName() const;

    FMassDspAsyncSaveFinished& OnAsyncSaveFinished() { return AsyncSaveFinishedEvent; }
    FMassDspAsyncLoadFinished& OnAsyncLoadFinished() { return AsyncLoadFinishedEvent; }

private:
    bool TryBeginAsyncOperation(EMassDspAsyncSaveLoadOperation Operation, const FString& SlotName, int32 UserIndex);
    FMassDspAsyncSaveLoadResult CompleteAsyncOperation(EMassDspAsyncSaveLoadOperation Operation, const FString& SlotName, int32 UserIndex, bool bSucceeded, const FMassDspAsyncSaveLoadResult* PhaseStats = nullptr);
    void LogAsyncSaveLoadBreakdown(const FMassDspAsyncSaveLoadResult& Result) const;
    void HandleAsyncSaveFinished(const FString& SlotName, int32 UserIndex, bool bSucceeded, double SerializeMs, double IoMs, int64 DataBytes);
    void HandleAsyncLoadFinished(const FString& SlotName, int32 UserIndex, bool bSucceeded, UMassDspSaveGame* LoadedSaveGame, double IoMs, double DeserializeMs, double UpgradeMs, int64 DataBytes);

    bool CollectCurrentState(UMassDspSaveGame& OutSaveGame) const;
    bool RestoreCurrentState(const UMassDspSaveGame& InSaveGame);
    bool UpgradeSaveGameToCurrentVersion(UMassDspSaveGame& InOutSaveGame) const;

private:
    EMassDspAsyncSaveLoadOperation ActiveAsyncOperation = EMassDspAsyncSaveLoadOperation::None;
    FString ActiveAsyncSlotName;
    int32 ActiveAsyncUserIndex = 0;
    double ActiveAsyncStartSeconds = 0.0;
    double ActiveCollectMs = 0.0;

    FMassDspAsyncSaveFinished AsyncSaveFinishedEvent;
    FMassDspAsyncLoadFinished AsyncLoadFinishedEvent;
};
