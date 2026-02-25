#pragma once

#include "CoreMinimal.h"
#include "GameConst.h"
#include "MassDspBeltTypes.h"
#include "MassEntityTypes.h"
#include "BeltItemFragment.generated.h"

USTRUCT()
struct MASSDSP_API FBeltItemFragment : public FMassFragment
{
    GENERATED_BODY()

    UPROPERTY()
    EItemType ItemType = EItemType::None;

    // 物料在当前传送带上的进度 (0.0 到 传送带长度)
    UPROPERTY()
    float DistanceAlongBelt = 0.0f;

    UPROPERTY()
    bool bIsBlocked = false;

    UPROPERTY()
    FBeltHandle BeltHandle = FBeltHandle();
};
