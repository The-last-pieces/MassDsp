#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"

#include "MassDspGameInstance.generated.h"

class UMassDspSaveGame;

UCLASS()
class MASSDSP_API UMassDspGameInstance : public UGameInstance
{
    GENERATED_BODY()

public:
    virtual void Init() override;

    UFUNCTION(BlueprintCallable, Category = "MassDsp|Save")
    bool SaveGameToSlot(const FString& SlotName = TEXT("QuickSave"), int32 UserIndex = 0);

    UFUNCTION(BlueprintCallable, Category = "MassDsp|Save")
    bool LoadGameFromSlot(const FString& SlotName = TEXT("QuickSave"), int32 UserIndex = 0);

    UFUNCTION(BlueprintPure, Category = "MassDsp|Save")
    bool DoesSaveExist(const FString& SlotName = TEXT("QuickSave"), int32 UserIndex = 0) const;

private:
    bool CollectCurrentState(UMassDspSaveGame& OutSaveGame) const;
    bool RestoreCurrentState(const UMassDspSaveGame& InSaveGame);
    bool UpgradeSaveGameToCurrentVersion(UMassDspSaveGame& InOutSaveGame) const;
};
