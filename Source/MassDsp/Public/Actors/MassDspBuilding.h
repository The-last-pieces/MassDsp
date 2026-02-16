#pragma once

#include "CoreMinimal.h"
#include "MassEntityHandle.h"

#include "GameFramework/Actor.h"
#include "MassDspBuilding.generated.h"

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

    // 可视化调试颜色
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Building")
    FColor DebugColor = FColor::Green;
};

/**
 * 通用建筑基类
 * 负责在场景中放置、配置槽口和基础属性。
 * 所有的逻辑处理将通过 MassEntity 系统进行。
 */
UCLASS()
class MASSDSP_API AMassDspBuilding : public AActor
{
    GENERATED_BODY()

public:
    AMassDspBuilding();

protected:
    virtual void BeginPlay() override;

    virtual void PostActorCreated() override;

#if WITH_EDITOR
    virtual void OnConstruction(const FTransform& Transform) override;
#endif

public:
    // 建筑主网格
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MassDsp|Building")
    TObjectPtr<UStaticMeshComponent> MeshComponent;

    // 槽口配置列表
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassDsp|Building")
    TArray<FBuildingSlotDef> Slots;

    // 注册到 Mass 后的实体句柄，方便后续查询和调试
    UPROPERTY()
    FMassEntityHandle MassHandle;
};
