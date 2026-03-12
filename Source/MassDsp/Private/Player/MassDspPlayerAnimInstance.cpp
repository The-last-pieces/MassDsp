#include "Player/MassDspPlayerAnimInstance.h"
#include "Player/MassDspPlayerCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"

void UMassDspPlayerAnimInstance::NativeInitializeAnimation()
{
    Super::NativeInitializeAnimation();
    OwnerCharacter = Cast<AMassDspPlayerCharacter>(GetOwningActor());
}

void UMassDspPlayerAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);

    if (!OwnerCharacter) return;

    const UCharacterMovementComponent* MoveComp = OwnerCharacter->GetCharacterMovement();
    if (!MoveComp) return;

    Speed = OwnerCharacter->GetVelocity().Size2D();
    bIsInAir = MoveComp->IsFalling();
    bIsFlying = (MoveComp->MovementMode == MOVE_Flying);
    bIsRunning = OwnerCharacter->bIsRunning;
}
