// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "MassDspPlayerAnimInstance.generated.h"

class AMassDspPlayerCharacter;

/**
 * 主玩家动画实例
 * 导出运动状态属性，供 ABP_MainPlayer 蓝图状态机驱动动画过渡。
 *
 * 推荐状态机结构：
 *   (OutputPose)
 *      Locomotion  : Idle -> Walk  (Speed >= 10)
 *      InAir       : bIsInAir == true  (跳跃/下落)
 *      Flying      : bIsFlying == true (飞行模式，优先级最高)
 *
 * 动画资产对应：
 *   Idle    <- AS_Idle
 *   Walk    <- AS_Walk  (bIsRunning=true 时播放速率 x 2)
 *   Default <- AS_Default  (任何过渡用)
 *   Flying  <- AS_Flying
 */
UCLASS()
class MASSDSP_API UMassDspPlayerAnimInstance : public UAnimInstance
{
    GENERATED_BODY()

public:
    virtual void NativeInitializeAnimation() override;
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;

    /** 水平速度 (cm/s)，Idle -> Walk 过渡阈值建议 10.f */
    UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement")
    float Speed = 0.f;

    /** 是否处于跳跃/下落中 */
    UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement")
    bool bIsInAir = false;

    /** 飞行模式激活（优先级高于 InAir） */
    UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement")
    bool bIsFlying = false;

    /** Shift 奔跑中（可用于调整 Walk 动画播放速率） */
    UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement")
    bool bIsRunning = false;

private:
    UPROPERTY()
    TObjectPtr<AMassDspPlayerCharacter> OwnerCharacter;
};
