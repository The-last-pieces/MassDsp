#pragma once

#include "CoreMinimal.h"
#include "MassEntityHandle.h"

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
 * 负责在场景中放置、配置槽口和基础属性。
 * 所有的逻辑处理将通过 MassEntity 系统进行。
 */
UCLASS(Blueprintable)
class MASSDSP_API AMassDspBuilding : public AActor
{
    GENERATED_BODY()

public:
    AMassDspBuilding();

public:
    TArray<const UScriptStruct*> GetStaticStructs() const;

    virtual void InitFragmentForEntity(FMassEntityManager& EntityManager, FMassEntityHandle EntityHandle) const;

protected:
    virtual const UScriptStruct* GetStaticStructForFragment() const;

protected:
    virtual void BeginPlay() override;
    virtual void PostActorCreated() override;

#if WITH_EDITOR
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override; // 新增

#endif

private:
    // 更新槽口可视化组件
    void UpdateSlotVisualization();

    // 清理所有槽口可视化组件
    void ClearSlotVisualization();

public:
    // 建筑主网格
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MassDsp|Building")
    TObjectPtr<UStaticMeshComponent> MeshComponent;

    // 槽口配置列表
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

    // 注册到 Mass 后的实体句柄，方便后续查询和调试
    UPROPERTY()
    FMassEntityHandle MassHandle;

#if WITH_EDITORONLY_DATA
    // 槽口可视化箭头组件（仅编辑器）
    UPROPERTY(Transient)
    TArray<TObjectPtr<UArrowComponent>> SlotVisualizationComponents;
#endif
};
