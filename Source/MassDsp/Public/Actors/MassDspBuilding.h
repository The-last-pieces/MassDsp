#pragma once

#include "CoreMinimal.h"
#include "GameConst.h"

#include "GameFramework/Actor.h"
#include "MassDspBuilding.generated.h"

struct FMassEntityManager;
class UArrowComponent;

// 槽口类型枚举
UENUM(BlueprintType)
enum class EBuildingSlotType : uint8
{
    Input = 0 UMETA(DisplayName = "输入"),
    Output = 1 UMETA(DisplayName = "输出")
};

// 建筑槽口定义结构体
USTRUCT(BlueprintType)
struct FBuildingSlotDef
{
    GENERATED_BODY()

    // 槽口相对于建筑中心的本地变换（位置和旋转）
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Building")
    FTransform LocalTransform;

    // 槽口类型（输入或输出）
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Building")
    EBuildingSlotType SlotType = EBuildingSlotType::Input;

    // 槽口类型（输入或输出）
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Building")
    float SlotExtend = 100.f;

    // 可视化调试颜色
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Building")
    FColor DebugColor = FColor::Green;
};

/**
 * 通用建筑基类
 * 
 * 【重要】此类仅用作配置数据容器（CDO），运行时不会实例化Actor！
 * 
 * - 在蓝图编辑器中：设置Building的配置属性（Slots、产出类型等）
 * - 运行时：直接从CDO读取配置，通过MassDspManager创建纯Mass Entity
 * - 渲染：使用MassRepresentation System的ISM批量渲染，无独立Mesh组件
 * 
 * 所有逻辑处理通过 MassEntity 系统和 Processor 完成。
 */
UCLASS(Blueprintable)
class MASSDSP_API AMassDspBuilding : public AActor
{
    GENERATED_BODY()

public:
    AMassDspBuilding();

public:
    virtual TArray<const UScriptStruct*> GetStaticStructs() const;

    // 从CDO初始化Fragment数据（由MassDspManager调用）
    virtual void InitFragmentForEntity(FMassEntityManager& EntityManager, FMassEntityHandle EntityHandle, const FTransform& WorldTransform) const;

protected:
    virtual const UScriptStruct* GetStaticStructForFragment() const;

#if WITH_EDITOR
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
    // 更新槽口可视化组件
    void UpdateSlotVisualization();

    // 清理所有槽口可视化组件
    void ClearSlotVisualization();

public:
    // 【已弃用】运行时使用MassRepresentation System的ISM渲染，不再需要独立Mesh组件
    // 保留此字段仅为兼容已有蓝图引用，编辑器中可用于预览
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MassDsp|Building", meta = (DeprecatedProperty, DeprecationMessage = "Runtime rendering uses Mass ISM system"))
    TObjectPtr<UStaticMeshComponent> MeshComponent;

    // 槽口配置列表（蓝图中设置，运行时从CDO读取）
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Building")
    TArray<FBuildingSlotDef> Slots;

    // 是否在编辑器中显示槽口可视化
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Building|Debug")
    bool bShowSlotVisualization = true;

    // 槽口可视化箭头大小
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Building|Debug", meta = (EditCondition = "bShowSlotVisualization"))
    float SlotVisualizationSize = 50.0f;

    // 槽口可视化箭头粗细
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Building|Debug", meta = (EditCondition = "bShowSlotVisualization"))
    float SlotVisualizationThickness = 3.0f;

#if WITH_EDITORONLY_DATA
    // 槽口可视化箭头组件（仅编辑器）
    UPROPERTY(Transient)
    TArray<TObjectPtr<UArrowComponent>> SlotVisualizationComponents;
#endif
};
