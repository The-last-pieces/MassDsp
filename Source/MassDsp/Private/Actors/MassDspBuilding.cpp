#include "Actors/MassDspBuilding.h"
#include "Subsystems/MassDspManager.h"
#include "Components/StaticMeshComponent.h"

#if WITH_EDITOR
#include "Components/ArrowComponent.h"
#endif

AMassDspBuilding::AMassDspBuilding()
{
    PrimaryActorTick.bCanEverTick = false;

    // 创建根组件
    MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
    RootComponent = MeshComponent;
}

void AMassDspBuilding::BeginPlay()
{
    Super::BeginPlay();
}

void AMassDspBuilding::PostActorCreated()
{
    Super::PostActorCreated();

    if (auto DspManager = GetWorld()->GetSubsystem<UMassDspManager>())
    {
        MassHandle = DspManager->RegisterBuildingEntity(this);
    }
}

#if WITH_EDITOR
void AMassDspBuilding::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    UpdateSlotVisualization();
}

void AMassDspBuilding::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);

    // 当属性变化时更新可视化
    if (PropertyChangedEvent.Property)
    {
        const FName PropertyName = PropertyChangedEvent.Property->GetFName();
        if (PropertyName == GET_MEMBER_NAME_CHECKED(AMassDspBuilding, Slots) ||
            PropertyName == GET_MEMBER_NAME_CHECKED(AMassDspBuilding, bShowSlotVisualization) ||
            PropertyName == GET_MEMBER_NAME_CHECKED(AMassDspBuilding, SlotVisualizationSize) ||
            PropertyName == GET_MEMBER_NAME_CHECKED(AMassDspBuilding, SlotVisualizationThickness))
        {
            UpdateSlotVisualization();
        }
    }
}

void AMassDspBuilding::UpdateSlotVisualization()
{
    // 先清理旧的可视化组件
    ClearSlotVisualization();

    if (!bShowSlotVisualization)
    {
        return;
    }

    // 为每个槽口创建箭头组件
    for (int32 i = 0; i < Slots.Num(); ++i)
    {
        const auto& [LocalTransform, SlotType, SlotExtend, DebugColor] = Slots[i];

        // 创建箭头组件
        FString ComponentName = FString::Printf(TEXT("SlotArrow_%d"), i);

        if (UArrowComponent* ArrowComponent = NewObject<UArrowComponent>(this, FName(*ComponentName)))
        {
            // 设置箭头属性
            ArrowComponent->SetupAttachment(RootComponent);
            ArrowComponent->ArrowSize = SlotExtend / 50.0f; // 归一化大小

            ArrowComponent->SetRelativeTransform(LocalTransform);

            ArrowComponent->SetHiddenInGame(true); // 游戏中隐藏
            ArrowComponent->bIsScreenSizeScaled = true;

            // 根据槽口类型设置颜色
            ArrowComponent->ArrowColor = DebugColor;

            // 注册组件
            ArrowComponent->RegisterComponent();

            // 添加到数组以便后续管理
            SlotVisualizationComponents.Add(ArrowComponent);
        }
    }
}

void AMassDspBuilding::ClearSlotVisualization()
{
    // 销毁所有可视化组件
    for (UArrowComponent* Component : SlotVisualizationComponents)
    {
        if (Component && IsValid(Component))
        {
            Component->DestroyComponent();
        }
    }
    SlotVisualizationComponents.Empty();
}
#endif
