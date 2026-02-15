// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MassEntityTypes.h"
#include "BeltItemFragment.generated.h"

USTRUCT()
struct MASSDSP_API FBeltItemFragment : public FMassFragment
{
    GENERATED_BODY()

    // 物料在当前传送带上的进度 (0.0 到 传送带长度)
    UPROPERTY()
    float DistanceAlongBelt = 0.0f;

    // 物料在当前传送带上的水平偏移量(以传送带切线方向为准,朝右偏移为正)
    UPROPERTY()
    float CrossOffset = 0.0f;

    // 物体半长（例如：100单位长的箱子，此值为50）
    UPROPERTY()
    float HalfLength = 50.0f;

    UPROPERTY()
    bool bIsBlocked = false;
};
