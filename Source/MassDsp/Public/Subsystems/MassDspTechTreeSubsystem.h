#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameConst.h"

#include "MassDspTechTreeSubsystem.generated.h"

class UGameConfigData;
class UMassDspPlayerInventoryComponent;

USTRUCT(BlueprintType)
struct MASSDSP_API FMassDspPlayerTechState
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "MassDsp|Tech")
    TSet<ETechNodeId> UnlockedNodes;

    UPROPERTY(BlueprintReadOnly, Category = "MassDsp|Tech")
    ETechNodeId CurrentResearchNode = ETechNodeId::None;

    UPROPERTY(BlueprintReadOnly, Category = "MassDsp|Tech")
    float ResearchProgress = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "MassDsp|Tech")
    int32 Version = 1;
};

DECLARE_MULTICAST_DELEGATE(FMassDspOnTechTreeChanged);

UCLASS()
class MASSDSP_API UMassDspTechTreeSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    virtual bool ShouldCreateSubsystem(UObject* Outer) const override
    {
        return true;
    }

    const FMassDspPlayerTechState& GetPlayerTechState() const { return PlayerTechState; }

    bool IsNodeUnlocked(ETechNodeId NodeId) const;
    bool CanUnlockNode(ETechNodeId NodeId, FText* OutFailureReason = nullptr) const;
    bool TryUnlockNode(ETechNodeId NodeId, FText* OutFailureReason = nullptr);

    bool IsBuildingUnlocked(EBuildingType BuildingType) const;
    bool IsRecipeUnlocked(ERecipeType RecipeType) const;
    bool IsBeltUnlocked(EBeltType BeltType) const;

    const FTechNodeConfig* GetNodeConfig(ETechNodeId NodeId) const;

    FText GetUnlockRequirementTextForBuilding(EBuildingType BuildingType) const;
    FText GetUnlockRequirementTextForRecipe(ERecipeType RecipeType) const;
    FText GetUnlockRequirementTextForBelt(EBeltType BeltType) const;

    void UnlockAllForDebug();

    FMassDspOnTechTreeChanged& OnTechTreeChanged()
    {
        return TechTreeChangedEvent;
    }

private:
    const UGameConfigData* GetGameConfig() const;
    UMassDspPlayerInventoryComponent* GetPlayerInventory() const;

    void ApplyDefaultUnlocks();
    void RebuildCaches();

    bool HasUnlockRuleForBuilding(EBuildingType BuildingType) const;
    bool HasUnlockRuleForRecipe(ERecipeType RecipeType) const;
    bool HasUnlockRuleForBelt(EBeltType BeltType) const;

    static bool RewardMatchesBuilding(const FTechReward& Reward, EBuildingType BuildingType);
    static bool RewardMatchesRecipe(const FTechReward& Reward, ERecipeType RecipeType);
    static bool RewardMatchesBelt(const FTechReward& Reward, EBeltType BeltType);

    ETechNodeId FindUnlockingNodeForBuilding(EBuildingType BuildingType) const;
    ETechNodeId FindUnlockingNodeForRecipe(ERecipeType RecipeType) const;
    ETechNodeId FindUnlockingNodeForBelt(EBeltType BeltType) const;

    UPROPERTY()
    FMassDspPlayerTechState PlayerTechState;

    TSet<EBuildingType> UnlockedBuildings;
    TSet<ERecipeType> UnlockedRecipes;
    TSet<EBeltType> UnlockedBelts;

    FMassDspOnTechTreeChanged TechTreeChangedEvent;
};