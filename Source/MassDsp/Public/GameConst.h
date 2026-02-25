#pragma once

#include "CoreMinimal.h"
#include "MassEntityConfigAsset.h"
#include "MassRepresentationTypes.h"
#include "MassRepresentationSubsystem.h"

#include "Misc/DataValidation.h"

#include "GameConst.generated.h"

class AMassDspBuilding;

struct FGameConst
{
    static constexpr float MinSpacing = 10.0f;
    static constexpr float HalfLength = 50.0f;
    static constexpr float ZOffset = 20.0f;
    static constexpr int SlotMaxCount = 5;
    static constexpr float ItemSpace = HalfLength * 2 + MinSpacing;
};

// TODO 下面的分文件定义

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
};

// 配方类型枚举
UENUM(BlueprintType)
enum class ERecipeType : uint8
{
    None = 0 UMETA(DisplayName = "无"),

    IronPlate = 1 UMETA(DisplayName = "烧制铁板"),
};

// 配方输入输出项
USTRUCT(BlueprintType)
struct FRecipeEntry
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Recipe")
    EItemType ItemType = EItemType::None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Recipe")
    int32 Amount = 1;

    FRecipeEntry() = default;

    FRecipeEntry(EItemType InType, int32 InAmount)
        : ItemType(InType), Amount(InAmount)
    {
    }

    bool IsValid() const
    {
        return ItemType != EItemType::None && Amount > 0;
    }
};


// 单个物品的配置数据(不是DataAsset)
USTRUCT(BlueprintType)
struct FItemConfigData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
    FText DisplayName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item", meta = (MultiLine = true))
    FText Description;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
    TObjectPtr<UTexture2D> Icon;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
    TObjectPtr<UStaticMesh> Mesh;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
    TObjectPtr<UMaterialInterface> Material;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
    int32 MaxStackSize = 100;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
    TObjectPtr<UMassEntityConfigAsset> EntityConfig;

    FStaticMeshInstanceVisualizationDescHandle GetOrCreateMeshHandle(const UWorld* World) const
    {
        if (!Mesh) return FStaticMeshInstanceVisualizationDescHandle();

        if (UMassRepresentationSubsystem* RepSubsystem = World->GetSubsystem<UMassRepresentationSubsystem>())
        {
            FStaticMeshInstanceVisualizationDesc Desc;

            FMassStaticMeshInstanceVisualizationMeshDesc MeshDesc;
            MeshDesc.Mesh = Mesh;

            if (Material)
            {
                MeshDesc.MaterialOverrides.Add(Material);
            }

            Desc.Meshes.Add(MeshDesc);
            Desc.bUseTransformOffset = true;
            Desc.TransformOffset.SetScale3D(FVector(1.0f, 1.0f, 0.2f));

            // 🔥 注册到系统并获取 Handle
            return RepSubsystem->FindOrAddStaticMeshDesc(Desc);
        }
        return FStaticMeshInstanceVisualizationDescHandle();
    }
};

USTRUCT()
struct FRecipeDataForFragment
{
    GENERATED_BODY()

    UPROPERTY()
    ERecipeType RecipeType = ERecipeType::None;

    UPROPERTY()
    FRecipeEntry Inputs[FGameConst::SlotMaxCount - 1];

    UPROPERTY()
    int InputsCount = 0;

    UPROPERTY()
    FRecipeEntry Outputs[FGameConst::SlotMaxCount - 1];

    UPROPERTY()
    int OutputsCount = 0;

    UPROPERTY()
    float CraftingTime = 1.0f;

    TArrayView<FRecipeEntry> GetInputs()
    {
        return MakeArrayView(Inputs, InputsCount);
    }

    TArrayView<FRecipeEntry> GetOutputs()
    {
        return MakeArrayView(Outputs, OutputsCount);
    }
};

// 单个配方的配置数据
USTRUCT(BlueprintType)
struct FRecipeConfigData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recipe")
    FText DisplayName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recipe")
    TArray<FRecipeEntry> Inputs;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recipe")
    TArray<FRecipeEntry> Outputs;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recipe")
    float CraftingTime = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recipe")
    TSubclassOf<AMassDspBuilding> RequiredBuildingClass;

    FRecipeDataForFragment ToFragment(ERecipeType RecipeType) const
    {
        FRecipeDataForFragment FragmentData;
        FragmentData.RecipeType = RecipeType;
        FragmentData.InputsCount = FMath::Min(Inputs.Num(), FGameConst::SlotMaxCount - 1);
        for (int i = 0; i < FragmentData.InputsCount; ++i)
        {
            FragmentData.Inputs[i] = Inputs[i];
        }
        FragmentData.OutputsCount = FMath::Min(Outputs.Num(), FGameConst::SlotMaxCount - 1);
        for (int i = 0; i < FragmentData.OutputsCount; ++i)
        {
            FragmentData.Outputs[i] = Outputs[i];
        }
        FragmentData.CraftingTime = CraftingTime;
        return FragmentData;
    }
};

// 🔥 核心:单个 DataAsset 管理所有配置
UCLASS(BlueprintType)
class MASSDSP_API UGameConfigData : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    // 所有物品配置(Key会自动初始化)
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Items", meta = (ForceInlineRow))
    TMap<EItemType, FItemConfigData> ItemConfigs;

    // 所有配方配置
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recipes", meta = (ForceInlineRow))
    TMap<ERecipeType, FRecipeConfigData> RecipeConfigs;

    // 构造函数:自动初始化所有枚举键
    UGameConfigData()
    {
        InitializeItemKeys();
        InitializeRecipeKeys();
    }

    // 🔥 自动创建所有枚举的 Key
    void InitializeItemKeys()
    {
        // 遍历所有 EItemType 枚举值
        const UEnum* EnumPtr = StaticEnum<EItemType>();
        if (!EnumPtr) return;

        for (int32 i = 0; i < EnumPtr->NumEnums() - 1; ++i) // -1 排除 _MAX
        {
            int64 EnumValue = EnumPtr->GetValueByIndex(i);
            EItemType ItemType = static_cast<EItemType>(EnumValue);

            // 跳过 None
            if (ItemType == EItemType::None) continue;

            // 如果 Map 中还没有这个键,就添加一个空配置
            if (!ItemConfigs.Contains(ItemType))
            {
                FItemConfigData DefaultData;
                // 从枚举元数据自动获取显示名称
                DefaultData.DisplayName = EnumPtr->GetDisplayNameTextByValue(EnumValue);
                ItemConfigs.Add(ItemType, DefaultData);
            }
        }
    }

    void InitializeRecipeKeys()
    {
        const UEnum* EnumPtr = StaticEnum<ERecipeType>();
        if (!EnumPtr) return;

        for (int32 i = 0; i < EnumPtr->NumEnums() - 1; ++i)
        {
            int64 EnumValue = EnumPtr->GetValueByIndex(i);
            ERecipeType RecipeType = static_cast<ERecipeType>(EnumValue);

            if (RecipeType == ERecipeType::None) continue;

            if (!RecipeConfigs.Contains(RecipeType))
            {
                FRecipeConfigData DefaultData;
                DefaultData.DisplayName = EnumPtr->GetDisplayNameTextByValue(EnumValue);
                RecipeConfigs.Add(RecipeType, DefaultData);
            }
        }
    }

    virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override
    {
        // 验证所有物品配置的有效性
        for (const auto& Pair : ItemConfigs)
        {
            const EItemType ItemType = Pair.Key;
            const FItemConfigData& Config = Pair.Value;

            if (ItemType == EItemType::None)
            {
                Context.AddError(FText::AsCultureInvariant(FString::Printf(TEXT("ItemConfigs contains invalid key: %d"), static_cast<int32>(ItemType))));
                return EDataValidationResult::Invalid;
            }

            if (Config.DisplayName.IsEmpty())
            {
                Context.AddError(FText::AsCultureInvariant(FString::Printf(TEXT("ItemConfigs[%d] has empty DisplayName"), static_cast<int32>(ItemType))));
                return EDataValidationResult::Invalid;
            }
        }

        // 验证所有配方配置的有效性
        for (const auto& Pair : RecipeConfigs)
        {
            const ERecipeType RecipeType = Pair.Key;
            const FRecipeConfigData& Config = Pair.Value;

            if (RecipeType == ERecipeType::None)
            {
                Context.AddError(FText::AsCultureInvariant(FString::Printf(TEXT("RecipeConfigs contains invalid key: %d"), static_cast<int32>(RecipeType))));
                return EDataValidationResult::Invalid;
            }

            if (Config.DisplayName.IsEmpty())
            {
                Context.AddError(FText::AsCultureInvariant(FString::Printf(TEXT("RecipeConfigs[%d] has empty DisplayName"), static_cast<int32>(RecipeType))));
                return EDataValidationResult::Invalid;
            }

            if (Config.Inputs.Num() == 0)
            {
                Context.AddError(FText::AsCultureInvariant(FString::Printf(TEXT("RecipeConfigs[%d] has no inputs defined"), static_cast<int32>(RecipeType))));
                return EDataValidationResult::Invalid;
            }

            if (Config.Outputs.Num() == 0)
            {
                Context.AddError(FText::AsCultureInvariant(FString::Printf(TEXT("RecipeConfigs[%d] has no outputs defined"), static_cast<int32>(RecipeType))));
                return EDataValidationResult::Invalid;
            }
        }

        return EDataValidationResult::Valid;
    }

#if WITH_EDITOR
    // 编辑器中修改时自动更新键
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override
    {
        Super::PostEditChangeProperty(PropertyChangedEvent);
        InitializeItemKeys();
        InitializeRecipeKeys();
    }
#endif

    // 查询接口
    const FItemConfigData* GetItemConfig(EItemType ItemType) const
    {
        return ItemConfigs.Find(ItemType);
    }

    const FRecipeConfigData* GetRecipeConfig(ERecipeType RecipeType) const
    {
        return RecipeConfigs.Find(RecipeType);
    }

    virtual FPrimaryAssetId GetPrimaryAssetId() const override
    {
        return FPrimaryAssetId(TEXT("GameConfig"), GetFName());
    }
};

template <typename T>
concept IsDspBuildFragment = requires(T TT, int SlotIdx, EItemType ItemType, float DeltaTime)
{
    { TT.TryProvideItemToSlot(SlotIdx) } -> std::convertible_to<EItemType>;
    { TT.TryConsumeItemFromSlot(ItemType) } -> std::convertible_to<bool>;
    { TT.TickExecute(DeltaTime) } -> std::convertible_to<void>;
};
