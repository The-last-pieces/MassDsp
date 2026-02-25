#pragma once

#include "CoreMinimal.h"
#include "Actors/MassDspBuilding.h"
#include "GameConst.h"
#include "MassDspAssembler.generated.h"

/**
 * 合成台建筑
 * 支持3个输入槽和1个输出槽，根据配方合成物品
 */
UCLASS()
class MASSDSP_API AMassDspAssembler : public AMassDspBuilding
{
    GENERATED_BODY()

public:
    AMassDspAssembler();

protected:
    virtual const UScriptStruct* GetStaticStructForFragment() const override;

    virtual void InitFragmentForEntity(FMassEntityManager& EntityManager, FMassEntityHandle EntityHandle, const FTransform& WorldTransform) const override;

public:
    // 当前配方
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Assembler")
    ERecipeType RecipeType;

    // 合成速度倍率
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Assembler", meta = (ClampMin = "0.1", ClampMax = "10.0"))
    float CraftingSpeedMultiplier = 1.0f;

    // 输入缓冲区容量（每个槽位）
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Assembler", meta = (ClampMin = "1", ClampMax = "100"))
    int32 InputBufferCapacity = 10;

    // 输出缓冲区容量
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Assembler", meta = (ClampMin = "1", ClampMax = "100"))
    int32 OutputBufferCapacity = 10;
};
