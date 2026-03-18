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
    bool bUseRelativeProductionDelay = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    float RemainingProductionDelaySeconds = 0.0f;

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
struct MASSDSP_API FMassDspWarehouseFragmentSaveData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    int32 MaxInventoryItems = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    TArray<FMassDspItemStackSaveData> ItemStacks;
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
    bool bUseRelativeCraftDelay = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    float RemainingCraftDelaySeconds = 0.0f;

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
    bool bHasWarehouseFragment = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    FMassDspWarehouseFragmentSaveData WarehouseData;

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
struct MASSDSP_API FMassDspBeltItemSaveData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    float Offset = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    EItemType ItemType = EItemType::None;
};

USTRUCT(BlueprintType)
struct MASSDSP_API FMassDspDubinsPathSaveData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    uint8 WordType = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    float SegLen0 = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    float SegLen1 = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    float SegLen2 = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    float TotalLength = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    float TurningRadius = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    FVector2D StartPos = FVector2D::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    float StartHeading = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    FVector2D EndPos = FVector2D::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    float EndHeading = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    float StartZ = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    float EndZ = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    bool bHasStartExtend = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    FVector StartExtendPos = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    bool bHasEndExtend = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    FVector EndExtendPos = FVector::ZeroVector;
};

USTRUCT(BlueprintType)
struct MASSDSP_API FMassDspHermiteRebuildSaveData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    FVector A = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    FVector B = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    FVector C = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    FVector D = FVector::ZeroVector;
};

USTRUCT(BlueprintType)
struct MASSDSP_API FMassDspBeltEntrySaveData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    int32 StartBuildingIndex = INDEX_NONE;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    int32 StartSlotIndex = INDEX_NONE;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    int32 EndBuildingIndex = INDEX_NONE;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    int32 EndSlotIndex = INDEX_NONE;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    EBeltType BeltType = EBeltType::None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    uint8 RebuildType = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    FMassDspDubinsPathSaveData DubinsData;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    FMassDspHermiteRebuildSaveData HermiteData;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    float BeltLength = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    float BeltSpeed = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    float TotalMove = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    int32 BlockedCount = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    float GroupFrontOffset = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    int32 ItemCacheStartIndex = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    int32 ItemCacheCount = 0;
};

USTRUCT(BlueprintType)
struct MASSDSP_API FMassDspBeltSaveChunk
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    int32 Version = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    TArray<FMassDspBeltEntrySaveData> Belts;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    TArray<FMassDspBeltItemSaveData> FlatItemCache;
};

USTRUCT(BlueprintType)
struct MASSDSP_API FMassDspLogisticsRequestSaveData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    uint8 Type = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    int32 SourceBuildingIndex = INDEX_NONE;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    EItemType ItemType = EItemType::None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    int32 Quantity = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    uint8 Priority = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    int32 PreferredTowerBuildingIndex = INDEX_NONE;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    float RequestTime = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    float ExpiryDuration = 0.0f;
};

USTRUCT(BlueprintType)
struct MASSDSP_API FMassDspLogisticsTaskSaveData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    int32 SupplyRequestIndex = INDEX_NONE;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    int32 DemandRequestIndex = INDEX_NONE;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    uint8 DeviceType = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    int32 DeviceIndex = INDEX_NONE;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    uint8 State = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    FVector PickupLocation = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    FVector DeliveryLocation = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    int32 PickupBuildingIndex = INDEX_NONE;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    int32 DeliveryBuildingIndex = INDEX_NONE;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    int32 TransferQuantity = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    float CreatedTime = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    int32 SupplyTowerBuildingIndex = INDEX_NONE;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    int32 DemandTowerBuildingIndex = INDEX_NONE;
};

USTRUCT(BlueprintType)
struct MASSDSP_API FMassDspDroneSaveData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    int32 Generation = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    uint8 State = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    int32 CurrentTaskIndex = INDEX_NONE;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    FVector P0 = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    FVector P1 = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    FVector P2 = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    FVector P3 = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    float TotalFlightTime = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    float ElapsedTime = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    float FlightSpeed = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    float CooldownDuration = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    float CooldownRemaining = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    int32 PickupBuildingIndex = INDEX_NONE;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    int32 DeliveryBuildingIndex = INDEX_NONE;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    FVector PickupLocation = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    FVector DeliveryLocation = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    EItemType CarriedItemType = EItemType::None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    int32 CarriedQuantity = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    int32 CarryCapacity = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    int32 AffiliatedTowerBuildingIndex = INDEX_NONE;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    FVector HomeLocation = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    float IdlePhaseOffset = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    float DispatchTime = 0.0f;
};

USTRUCT(BlueprintType)
struct MASSDSP_API FMassDspLogisticsTowerRuntimeSaveData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    int32 TowerBuildingIndex = INDEX_NONE;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    TArray<int32> PendingRequestIndices;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    TArray<int32> ActiveTaskIndices;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    TArray<int32> AffiliatedDroneIndices;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    int32 CachedRequestIndex = INDEX_NONE;
};

USTRUCT(BlueprintType)
struct MASSDSP_API FMassDspLogisticsSaveChunk
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    int32 Version = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    TArray<FMassDspLogisticsRequestSaveData> Requests;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    TArray<FMassDspLogisticsTaskSaveData> Tasks;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    TArray<FMassDspDroneSaveData> Drones;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Save")
    TArray<FMassDspLogisticsTowerRuntimeSaveData> Towers;
};

UCLASS(BlueprintType)
class MASSDSP_API UMassDspSaveGame : public USaveGame
{
    GENERATED_BODY()

public:
    static constexpr int32 CurrentSaveVersion = 2;

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