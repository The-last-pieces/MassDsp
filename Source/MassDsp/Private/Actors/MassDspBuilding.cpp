#include "Actors/MassDspBuilding.h"
#include "Subsystems/MassDspManager.h"
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

    if (auto Manager = GetWorld()->GetSubsystem<UMassDspManager>())
    {
        Manager->RegisterBuildingEntity(this);
    }
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
    // 可视化槽口逻辑可以在这里添加，例如绘制DebugSphere
}
#endif
