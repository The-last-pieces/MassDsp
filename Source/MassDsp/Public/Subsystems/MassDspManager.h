#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ZoneGraphTypes.h"
#include "MassEntityTemplate.h"

#include "Containers/Deque.h"

#include "MassDspManager.generated.h"

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
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
    TObjectPtr<UMassEntityConfigAsset> DefaultItemConfig;

    // 存储车道实体的注册表
    TMap<FZoneGraphLaneHandle, FBeltEntityArray> LaneRegistry;

public:
    FZoneGraphDataHandle CreateRuntimeBelt(const TArray<FVector>& ControlPoints, UStaticMesh* BeltMesh, int32 SegmentsPerSection = 1) const;

    bool ProvideItemToBelt(FMassCommandBuffer& CommandBuffer, FZoneGraphLaneHandle LaneHandle, UMassEntityConfigAsset* ItemConfig);

    bool ConsumeItemFromBelt(FMassCommandBuffer& CommandBuffer, FZoneGraphLaneHandle LaneHandle);

    FMassEntityHandle RegisterBuildingEntity(class AMassDspBuilding* BuildingActor) const;
};
