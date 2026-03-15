#pragma once

#include "CoreMinimal.h"
#include "GameConst.h"
#include "GameFramework/SaveGame.h"

#include "MassDspSaveData.generated.h"

USTRUCT(BlueprintType)
struct MASSDSP_API FMassDspSaveHeader
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    int32 SaveVersion = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    FString MapName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    FDateTime SavedAtUtc;
};

USTRUCT(BlueprintType)
struct MASSDSP_API FMassDspItemStackSaveData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    EItemType ItemType = EItemType::None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    int32 Quantity = 0;
};

USTRUCT(BlueprintType)
struct MASSDSP_API FMassDspPlayerInventorySaveData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    int32 MaxInventoryItems = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    TArray<FMassDspItemStackSaveData> ItemStacks;
};

USTRUCT(BlueprintType)
struct MASSDSP_API FMassDspTechTreeSaveData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    int32 Version = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    TArray<ETechNodeId> UnlockedNodes;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    ETechNodeId CurrentResearchNode = ETechNodeId::None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    float ResearchProgress = 0.0f;
};

USTRUCT(BlueprintType)
struct MASSDSP_API FMassDspMinerFragmentSaveData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    float NextProductionWorldTime = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    float ProductionInterval = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    int32 InventoryCount = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    int32 MaxInventory = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    EItemType StoredItemType = EItemType::None;
};

USTRUCT(BlueprintType)
struct MASSDSP_API FMassDspStorageFragmentSaveData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    int32 InventoryCount = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    int32 MaxInventory = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    EItemType StoredItemType = EItemType::None;
};

USTRUCT(BlueprintType)
struct MASSDSP_API FMassDspAssemblerFragmentSaveData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    ERecipeType ActiveRecipeType = ERecipeType::None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    float NextCraftWorldTime = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    float CraftingSpeedMultiplier = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    int32 InputBufferCapacity = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    int32 OutputBufferCapacity = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    TArray<FMassDspItemStackSaveData> InputBuffers;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    TArray<FMassDspItemStackSaveData> OutputBuffers;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    bool bInputSatisfied = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    bool bOutputSatisfied = false;
};

USTRUCT(BlueprintType)
struct MASSDSP_API FMassDspLogisticsTowerFragmentSaveData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    ELogisticsTowerMode TowerMode = ELogisticsTowerMode::Storage;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    EItemType ItemType = EItemType::None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    int32 RequestThreshold = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    int32 DroneCargoCount = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    float CoverageRadius = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    float ScanInterval = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    float LastScanTime = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    bool bDirty = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    bool bAcceptsRequests = true;
};

USTRUCT(BlueprintType)
struct MASSDSP_API FMassDspBuildingSaveData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    EBuildingType BuildingType = EBuildingType::None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    FTransform WorldTransform;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    bool bHasMinerFragment = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    FMassDspMinerFragmentSaveData MinerData;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    bool bHasStorageFragment = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    FMassDspStorageFragmentSaveData StorageData;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    bool bHasAssemblerFragment = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    FMassDspAssemblerFragmentSaveData AssemblerData;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    bool bHasLogisticsTowerFragment = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    FMassDspLogisticsTowerFragmentSaveData LogisticsTowerData;
};

USTRUCT(BlueprintType)
struct MASSDSP_API FMassDspBeltSaveChunk
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    int32 Version = 1;
};

USTRUCT(BlueprintType)
struct MASSDSP_API FMassDspLogisticsSaveChunk
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    int32 Version = 1;
};

UCLASS(BlueprintType)
class MASSDSP_API UMassDspSaveGame : public USaveGame
{
    GENERATED_BODY()

public:
    static constexpr int32 CurrentSaveVersion = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    FMassDspSaveHeader Header;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    TArray<FMassDspBuildingSaveData> Buildings;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    FMassDspBeltSaveChunk Belts;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    FMassDspLogisticsSaveChunk Logistics;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    FMassDspPlayerInventorySaveData PlayerInventory;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    FMassDspTechTreeSaveData TechTree;
};