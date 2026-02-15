#include "Actors/MassDspBuilding.h"
#include "Subsystems/MassDspManager.h"
#include "Kismet/GameplayStatics.h"
#include "Components/StaticMeshComponent.h"

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

    if (bAutoRegisterToMass)
    {
        UMassDspManager* Manager = GetWorld()->GetSubsystem<UMassDspManager>();
        if (Manager)
        {
            Manager->RegisterBuildingEntity(this);
        }
    }
}

#if WITH_EDITOR
void AMassDspBuilding::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    // 可视化槽口逻辑可以在这里添加，例如绘制DebugSphere
}
#endif

TArray<FTransform> AMassDspBuilding::GetSlotTransformsByType(EBuildingSlotType Type) const
{
    TArray<FTransform> Result;
    FTransform ActorTransform = GetActorTransform();

    for (const FBuildingSlotDef& Slot : Slots)
    {
        if (Slot.SlotType == Type)
        {
            // 将本地变换转换为世界变换
            Result.Add(Slot.LocalTransform * ActorTransform);
        }
    }
    return Result;
}
