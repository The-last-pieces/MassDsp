#include "Subsystems/MassDspTechTreeSubsystem.h"

#include "MassDspGameMode.h"
#include "GameFramework/PlayerController.h"
#include "Inventory/MassDspPlayerInventoryComponent.h"

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

void UMassDspTechTreeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    ApplyDefaultUnlocks();
    RebuildCaches();
}

void UMassDspTechTreeSubsystem::Deinitialize()
{
    PlayerTechState = FMassDspPlayerTechState();
    UnlockedBuildings.Reset();
    UnlockedRecipes.Reset();
    UnlockedBelts.Reset();
    Super::Deinitialize();
}

void UMassDspTechTreeSubsystem::RestorePlayerTechState(const FMassDspPlayerTechState& InState)
{
    PlayerTechState = InState;
    RebuildCaches();
    TechTreeChangedEvent.Broadcast();
}

bool UMassDspTechTreeSubsystem::IsNodeUnlocked(ETechNodeId NodeId) const
{
    return NodeId == ETechNodeId::None || PlayerTechState.UnlockedNodes.Contains(NodeId);
}

bool UMassDspTechTreeSubsystem::CanUnlockNode(ETechNodeId NodeId, FText* OutFailureReason) const
{
    if (NodeId == ETechNodeId::None)
    {
        if (OutFailureReason) *OutFailureReason = FText::FromString(TEXT("无效科技节点"));
        return false;
    }

    if (IsNodeUnlocked(NodeId))
    {
        if (OutFailureReason) *OutFailureReason = FText::FromString(TEXT("该科技已解锁"));
        return false;
    }

    const FTechNodeConfig* NodeConfig = GetNodeConfig(NodeId);
    if (!NodeConfig)
    {
        if (OutFailureReason) *OutFailureReason = FText::FromString(TEXT("科技配置缺失"));
        return false;
    }

    for (const ETechNodeId Prerequisite : NodeConfig->Prerequisites)
    {
        if (!IsNodeUnlocked(Prerequisite))
        {
            const FTechNodeConfig* PrerequisiteConfig = GetNodeConfig(Prerequisite);
            if (OutFailureReason)
            {
                *OutFailureReason = PrerequisiteConfig
                                        ? FText::FromString(FString::Printf(TEXT("需要先解锁：%s"), *PrerequisiteConfig->DisplayName.ToString()))
                                        : FText::FromString(TEXT("前置科技未满足"));
            }
            return false;
        }
    }

    UMassDspPlayerInventoryComponent* PlayerInventory = GetPlayerInventory();
    if (!PlayerInventory)
    {
        if (OutFailureReason) *OutFailureReason = FText::FromString(TEXT("玩家背包不可用"));
        return false;
    }

    for (const FRecipeEntry& CostEntry : NodeConfig->ResearchCost)
    {
        if (!CostEntry.IsValid()) continue;
        if (PlayerInventory->GetItemCount(CostEntry.ItemType) < CostEntry.Amount)
        {
            if (OutFailureReason)
            {
                *OutFailureReason = FText::FromString(FString::Printf(TEXT("研究材料不足：%s x%d"),
                                                                      *MassDspEnumText::GetItemType(CostEntry.ItemType).ToString(),
                                                                      CostEntry.Amount));
            }
            return false;
        }
    }

    return true;
}

bool UMassDspTechTreeSubsystem::TryUnlockNode(ETechNodeId NodeId, FText* OutFailureReason)
{
    if (!CanUnlockNode(NodeId, OutFailureReason))
    {
        return false;
    }

    const FTechNodeConfig* NodeConfig = GetNodeConfig(NodeId);
    UMassDspPlayerInventoryComponent* PlayerInventory = GetPlayerInventory();
    if (!NodeConfig || !PlayerInventory)
    {
        if (OutFailureReason) *OutFailureReason = FText::FromString(TEXT("科技解锁失败"));
        return false;
    }

    for (const FRecipeEntry& CostEntry : NodeConfig->ResearchCost)
    {
        if (!CostEntry.IsValid()) continue;
        if (PlayerInventory->RemoveItem(CostEntry.ItemType, CostEntry.Amount) != CostEntry.Amount)
        {
            if (OutFailureReason) *OutFailureReason = FText::FromString(TEXT("扣除研究材料失败"));
            return false;
        }
    }

    PlayerTechState.UnlockedNodes.Add(NodeId);
    PlayerTechState.CurrentResearchNode = ETechNodeId::None;
    PlayerTechState.ResearchProgress = 0.0f;

    RebuildCaches();
    TechTreeChangedEvent.Broadcast();
    return true;
}

bool UMassDspTechTreeSubsystem::IsBuildingUnlocked(EBuildingType BuildingType) const
{
    if (BuildingType == EBuildingType::None) return true;
    return !HasUnlockRuleForBuilding(BuildingType) || UnlockedBuildings.Contains(BuildingType);
}

bool UMassDspTechTreeSubsystem::IsRecipeUnlocked(ERecipeType RecipeType) const
{
    if (RecipeType == ERecipeType::None) return true;
    return !HasUnlockRuleForRecipe(RecipeType) || UnlockedRecipes.Contains(RecipeType);
}

bool UMassDspTechTreeSubsystem::IsBeltUnlocked(EBeltType BeltType) const
{
    if (BeltType == EBeltType::None) return true;
    return !HasUnlockRuleForBelt(BeltType) || UnlockedBelts.Contains(BeltType);
}

const FTechNodeConfig* UMassDspTechTreeSubsystem::GetNodeConfig(ETechNodeId NodeId) const
{
    const UGameConfigData* GameConfig = GetGameConfig();
    return GameConfig ? GameConfig->GetTechNodeConfig(NodeId) : nullptr;
}

FText UMassDspTechTreeSubsystem::GetUnlockRequirementTextForBuilding(EBuildingType BuildingType) const
{
    const ETechNodeId NodeId = FindUnlockingNodeForBuilding(BuildingType);
    const FTechNodeConfig* NodeConfig = GetNodeConfig(NodeId);
    return NodeConfig ? FText::FromString(FString::Printf(TEXT("需要解锁：%s"), *NodeConfig->DisplayName.ToString())) : FText::GetEmpty();
}

FText UMassDspTechTreeSubsystem::GetUnlockRequirementTextForRecipe(ERecipeType RecipeType) const
{
    const ETechNodeId NodeId = FindUnlockingNodeForRecipe(RecipeType);
    const FTechNodeConfig* NodeConfig = GetNodeConfig(NodeId);
    return NodeConfig ? FText::FromString(FString::Printf(TEXT("需要解锁：%s"), *NodeConfig->DisplayName.ToString())) : FText::GetEmpty();
}

FText UMassDspTechTreeSubsystem::GetUnlockRequirementTextForBelt(EBeltType BeltType) const
{
    const ETechNodeId NodeId = FindUnlockingNodeForBelt(BeltType);
    const FTechNodeConfig* NodeConfig = GetNodeConfig(NodeId);
    return NodeConfig ? FText::FromString(FString::Printf(TEXT("需要解锁：%s"), *NodeConfig->DisplayName.ToString())) : FText::GetEmpty();
}

void UMassDspTechTreeSubsystem::UnlockAllForDebug()
{
    const UGameConfigData* GameConfig = GetGameConfig();
    if (!GameConfig) return;

    for (const TPair<ETechNodeId, FTechNodeConfig>& Pair : GameConfig->GetTechNodeConfigs())
    {
        if (Pair.Key != ETechNodeId::None)
        {
            PlayerTechState.UnlockedNodes.Add(Pair.Key);
        }
    }

    RebuildCaches();
    TechTreeChangedEvent.Broadcast();
}

const UGameConfigData* UMassDspTechTreeSubsystem::GetGameConfig() const
{
    const UWorld* World = GetWorld();
    const AMassDspGameMode* GameMode = World ? Cast<AMassDspGameMode>(World->GetAuthGameMode()) : nullptr;
    return GameMode ? GameMode->GameConfig.Get() : nullptr;
}

UMassDspPlayerInventoryComponent* UMassDspTechTreeSubsystem::GetPlayerInventory() const
{
    return ResolvePlayerInventory(GetWorld());
}

void UMassDspTechTreeSubsystem::ApplyDefaultUnlocks()
{
    PlayerTechState.UnlockedNodes.Reset();

    const UGameConfigData* GameConfig = GetGameConfig();
    if (!GameConfig) return;

    for (const TPair<ETechNodeId, FTechNodeConfig>& Pair : GameConfig->GetTechNodeConfigs())
    {
        if (Pair.Key != ETechNodeId::None && Pair.Value.bUnlockedByDefault)
        {
            PlayerTechState.UnlockedNodes.Add(Pair.Key);
        }
    }
}

void UMassDspTechTreeSubsystem::RebuildCaches()
{
    UnlockedBuildings.Reset();
    UnlockedRecipes.Reset();
    UnlockedBelts.Reset();

    const UGameConfigData* GameConfig = GetGameConfig();
    if (!GameConfig) return;

    for (const ETechNodeId NodeId : PlayerTechState.UnlockedNodes)
    {
        const FTechNodeConfig* NodeConfig = GameConfig->GetTechNodeConfig(NodeId);
        if (!NodeConfig) continue;

        for (const FTechReward& Reward : NodeConfig->Rewards)
        {
            switch (Reward.RewardType)
            {
            case ETechRewardType::UnlockBuilding:
                if (Reward.BuildingType != EBuildingType::None)
                {
                    UnlockedBuildings.Add(Reward.BuildingType);
                }
                break;
            case ETechRewardType::UnlockRecipe:
                if (Reward.RecipeType != ERecipeType::None)
                {
                    UnlockedRecipes.Add(Reward.RecipeType);
                }
                break;
            case ETechRewardType::UnlockBelt:
                if (Reward.BeltType != EBeltType::None)
                {
                    UnlockedBelts.Add(Reward.BeltType);
                }
                break;
            default:
                break;
            }
        }
    }
}

bool UMassDspTechTreeSubsystem::HasUnlockRuleForBuilding(EBuildingType BuildingType) const
{
    return FindUnlockingNodeForBuilding(BuildingType) != ETechNodeId::None;
}

bool UMassDspTechTreeSubsystem::HasUnlockRuleForRecipe(ERecipeType RecipeType) const
{
    return FindUnlockingNodeForRecipe(RecipeType) != ETechNodeId::None;
}

bool UMassDspTechTreeSubsystem::HasUnlockRuleForBelt(EBeltType BeltType) const
{
    return FindUnlockingNodeForBelt(BeltType) != ETechNodeId::None;
}

bool UMassDspTechTreeSubsystem::RewardMatchesBuilding(const FTechReward& Reward, EBuildingType BuildingType)
{
    return Reward.RewardType == ETechRewardType::UnlockBuilding && Reward.BuildingType == BuildingType;
}

bool UMassDspTechTreeSubsystem::RewardMatchesRecipe(const FTechReward& Reward, ERecipeType RecipeType)
{
    return Reward.RewardType == ETechRewardType::UnlockRecipe && Reward.RecipeType == RecipeType;
}

bool UMassDspTechTreeSubsystem::RewardMatchesBelt(const FTechReward& Reward, EBeltType BeltType)
{
    return Reward.RewardType == ETechRewardType::UnlockBelt && Reward.BeltType == BeltType;
}

ETechNodeId UMassDspTechTreeSubsystem::FindUnlockingNodeForBuilding(EBuildingType BuildingType) const
{
    const UGameConfigData* GameConfig = GetGameConfig();
    if (!GameConfig) return ETechNodeId::None;

    for (const TPair<ETechNodeId, FTechNodeConfig>& Pair : GameConfig->GetTechNodeConfigs())
    {
        for (const FTechReward& Reward : Pair.Value.Rewards)
        {
            if (RewardMatchesBuilding(Reward, BuildingType))
            {
                return Pair.Key;
            }
        }
    }

    return ETechNodeId::None;
}

ETechNodeId UMassDspTechTreeSubsystem::FindUnlockingNodeForRecipe(ERecipeType RecipeType) const
{
    const UGameConfigData* GameConfig = GetGameConfig();
    if (!GameConfig) return ETechNodeId::None;

    for (const TPair<ETechNodeId, FTechNodeConfig>& Pair : GameConfig->GetTechNodeConfigs())
    {
        for (const FTechReward& Reward : Pair.Value.Rewards)
        {
            if (RewardMatchesRecipe(Reward, RecipeType))
            {
                return Pair.Key;
            }
        }
    }

    return ETechNodeId::None;
}

ETechNodeId UMassDspTechTreeSubsystem::FindUnlockingNodeForBelt(EBeltType BeltType) const
{
    const UGameConfigData* GameConfig = GetGameConfig();
    if (!GameConfig) return ETechNodeId::None;

    for (const TPair<ETechNodeId, FTechNodeConfig>& Pair : GameConfig->GetTechNodeConfigs())
    {
        for (const FTechReward& Reward : Pair.Value.Rewards)
        {
            if (RewardMatchesBelt(Reward, BeltType))
            {
                return Pair.Key;
            }
        }
    }

    return ETechNodeId::None;
}
