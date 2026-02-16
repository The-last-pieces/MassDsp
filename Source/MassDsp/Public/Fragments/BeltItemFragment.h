#pragma once

#include "CoreMinimal.h"
#include "MassDspBeltTypes.h"
#include "MassEntityTypes.h"
#include "BeltItemFragment.generated.h"

USTRUCT()
struct MASSDSP_API FBeltItemFragment : public FMassFragment
{
    GENERATED_BODY()

    // 物料在当前传送带上的进度 (0.0 到 传送带长度)
    UPROPERTY()
    float DistanceAlongBelt = 0.0f;

    UPROPERTY()
    bool bIsBlocked = false;

    UPROPERTY()
    FBeltHandle BeltHandle = FBeltHandle();
};
