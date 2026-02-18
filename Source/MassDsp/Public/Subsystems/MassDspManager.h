#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MassEntityTemplate.h"
#include "MassDspBeltTypes.h"

#include "Containers/Deque.h"
#include "Containers/SparseArray.h"

#include "MassDspManager.generated.h"

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

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
    TObjectPtr<UMassEntityConfigAsset> DefaultItemConfig;

    TMap<FBeltHandle, FBeltEntityArray> BeltEntityRegistry;

    TSparseArray<FBeltTrajectory> BeltTrajectories;

    UPROPERTY()
    AActor* BeltsContainerActor;

private:
    FBeltHandle CreateRuntimeBelt(const TFunction<void(USplineComponent*)>& InitSpline, UStaticMesh* BeltMesh, int32 SegmentsPerSection);

public:
    FBeltHandle CreateAndLinkBeltForSlot(const AMassDspBuilding* SBuilding, int32 StartSlotIndex, const AMassDspBuilding* EBuilding, int32 EndSlotIndex, UStaticMesh* BeltMesh);

    bool ProvideItemToBelt(FMassCommandBuffer& CommandBuffer, FBeltHandle BeltHandle, UMassEntityConfigAsset* ItemConfig);

    bool ConsumeItemFromBelt(FMassCommandBuffer& CommandBuffer, FBeltHandle BeltHandle);

    FMassEntityHandle RegisterBuildingEntity(const AMassDspBuilding* BuildingActor) const;
};
