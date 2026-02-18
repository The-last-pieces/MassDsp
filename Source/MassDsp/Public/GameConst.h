#pragma once

#include "CoreMinimal.h"
#include "GameConst.generated.h"

// 物品类型枚举
UENUM(BlueprintType)
enum class EItemType : uint8
{
    None = 0 UMETA(DisplayName = "无"),
    
    // 原材料
    IronOre = 1 UMETA(DisplayName = "铁矿石"),
    CopperOre = 2 UMETA(DisplayName = "铜矿石"),
    Stone = 3 UMETA(DisplayName = "石头"),
    Coal = 4 UMETA(DisplayName = "煤炭"),
    
    // 基础材料
    IronPlate = 10 UMETA(DisplayName = "铁板"),
    CopperPlate = 11 UMETA(DisplayName = "铜板"),
    SteelPlate = 12 UMETA(DisplayName = "钢板"),
    
    // 中级材料
    IronGear = 20 UMETA(DisplayName = "铁齿轮"),
    CopperWire = 21 UMETA(DisplayName = "铜线"),
    Circuit = 22 UMETA(DisplayName = "电路板"),
    
    // 高级材料
    AdvancedCircuit = 30 UMETA(DisplayName = "高级电路板"),
    ProcessingUnit = 31 UMETA(DisplayName = "处理器"),
    
    // 预留扩展空间
    MAX = 255
};

// 配方输入项
USTRUCT(BlueprintType)
struct FRecipeInput
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Recipe")
    EItemType ItemType = EItemType::None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Recipe")
    int32 Amount = 1;

    FRecipeInput() = default;
    
    FRecipeInput(EItemType InType, int32 InAmount)
        : ItemType(InType), Amount(InAmount)
    {}

    bool IsValid() const
    {
        return ItemType != EItemType::None && Amount > 0;
    }
};

// 配方输出项
USTRUCT(BlueprintType)
struct FRecipeOutput
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Recipe")
    EItemType ItemType = EItemType::None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Recipe")
    int32 Amount = 1;

    FRecipeOutput() = default;
    
    FRecipeOutput(EItemType InType, int32 InAmount)
        : ItemType(InType), Amount(InAmount)
    {}

    bool IsValid() const
    {
        return ItemType != EItemType::None && Amount > 0;
    }
};

// 配方定义
USTRUCT(BlueprintType)
struct FRecipeData
{
    GENERATED_BODY()

    // 配方名称
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Recipe")
    FString RecipeName;

    // 输入项（最多3个）
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Recipe")
    TArray<FRecipeInput> Inputs;

    // 输出项
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Recipe")
    FRecipeOutput Output;

    // 合成时间（秒）
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Recipe")
    float CraftingTime = 1.0f;

    FRecipeData() = default;

    // 验证配方是否有效
    bool IsValid() const
    {
        if (Inputs.Num() == 0 || Inputs.Num() > 3)
            return false;
        
        for (const FRecipeInput& Input : Inputs)
        {
            if (!Input.IsValid())
                return false;
        }
        
        return Output.IsValid() && CraftingTime > 0.0f;
    }
};

struct FGameConst
{
    static constexpr float MinSpacing = 10.0f;
    static constexpr float HalfLength = 50.0f;
    static constexpr float ZOffset = 20.0f;
    static constexpr int SlotMaxCount = 5;
};
