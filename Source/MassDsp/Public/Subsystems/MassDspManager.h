#pragma once

#include "CoreMinimal.h"
#include "GameConst.h"

#include "Subsystems/WorldSubsystem.h"
#include "MassEntityTemplate.h"
#include "MassDspBeltTypes.h"

#include "Containers/Deque.h"
#include "Containers/SparseArray.h"

#include "MassDspManager.generated.h"

class AMassDspGameMode;
class AMassDspBuilding;
class UMassEntityConfigAsset;

struct FBeltEntityArray
{
    TDeque<FMassEntityHandle> Entities;
};

UCLASS()
class MASSDSP_API UMassDspManager : public UWorldSubsystem
{
    GENERATED_BODY()

protected:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

public:
    TMap<FBeltHandle, FBeltEntityArray> BeltEntityRegistry;

    TSparseArray<FBeltTrajectory> BeltTrajectories;

    TWeakObjectPtr<AMassDspGameMode> GameMode;

protected:
    UPROPERTY()
    AActor* BeltsContainerActor;

private:
    FBeltHandle CreateRuntimeBelt(const TFunction<void(USplineComponent*)>& InitSpline, UStaticMesh* BeltMesh, int32 SegmentsPerSection);

public:
    FBeltHandle CreateAndLinkBeltForSlot(FMassEntityHandle SBuilding, int32 StartSlotIndex, FMassEntityHandle EBuilding, int32 EndSlotIndex, UStaticMesh* BeltMesh);

    bool ProvideItemToBelt(FMassCommandBuffer& CommandBuffer, FBeltHandle BeltHandle, const TFunction<EItemType()>& GetItemFunc);

    EItemType ConsumeItemFromBelt(FMassCommandBuffer& CommandBuffer, FBeltHandle BeltHandle, const TFunction<bool(EItemType)>& ValidateItemFunc);

    FMassEntityHandle RegisterBuildingEntity(const AMassDspBuilding* BuildingActor) const;
};
