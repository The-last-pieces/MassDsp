#include "Player/MassDspPlayerCharacter.h"

#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Inventory/MassDspPlayerInventoryComponent.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"

// 
//  构造函数
// 

AMassDspPlayerCharacter::AMassDspPlayerCharacter()
{
    PrimaryActorTick.bCanEverTick = false;

    //  胶囊体 & 碰撞 
    GetCapsuleComponent()->InitCapsuleSize(35.f, 90.f);

    //  角色不跟随控制器 Yaw 旋转，移动时自动转向 
    bUseControllerRotationPitch = false;
    bUseControllerRotationYaw = false;
    bUseControllerRotationRoll = false;

    GetCharacterMovement()->bOrientRotationToMovement = true;
    GetCharacterMovement()->RotationRate = FRotator(0.f, 540.f, 0.f);
    GetCharacterMovement()->JumpZVelocity = 600.f;
    GetCharacterMovement()->AirControl = 0.35f;
    GetCharacterMovement()->MaxWalkSpeed = 400.f;
    GetCharacterMovement()->MaxFlySpeed = 1200.f;

    //  SpringArm（固定俯视 -60，可旋转 Yaw）
    CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
    CameraBoom->SetupAttachment(RootComponent);
    CameraBoom->TargetArmLength = 1500.f;
    CameraBoom->SetRelativeRotation(FRotator(-60.f, 0.f, 0.f));
    CameraBoom->bUsePawnControlRotation = true; // 跟随 ControlRotation 的 Yaw
    CameraBoom->bInheritPitch = false; // 固定 Pitch，不跟随控制器
    CameraBoom->bInheritRoll = false;
    CameraBoom->bInheritYaw = true;
    CameraBoom->bDoCollisionTest = true;

    //  Camera 
    FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
    FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
    FollowCamera->bUsePawnControlRotation = false;

    InventoryComponent = CreateDefaultSubobject<UMassDspPlayerInventoryComponent>(TEXT("InventoryComponent"));
}

// 
//  BeginPlay  注册 Enhanced Input 映射上下文
// 

void AMassDspPlayerCharacter::BeginPlay()
{
    Super::BeginPlay();

    if (const APlayerController* PC = Cast<APlayerController>(Controller))
    {
        if (UEnhancedInputLocalPlayerSubsystem* Sub =
            ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
        {
            if (DefaultMappingContext)
                Sub->AddMappingContext(DefaultMappingContext, 0);
        }
    }
}

// 
//  Tick
// 

void AMassDspPlayerCharacter::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
}

// 
//  Enhanced Input 绑定
// 

void AMassDspPlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    UEnhancedInputComponent* EIC = CastChecked<UEnhancedInputComponent>(PlayerInputComponent);

    if (IA_Move)
    {
        EIC->BindAction(IA_Move, ETriggerEvent::Triggered, this, &AMassDspPlayerCharacter::Handle_Move);
    }
    if (IA_Jump)
    {
        EIC->BindAction(IA_Jump, ETriggerEvent::Started, this, &AMassDspPlayerCharacter::Handle_Jump);
        EIC->BindAction(IA_Jump, ETriggerEvent::Completed, this, &AMassDspPlayerCharacter::Handle_StopJumping);
    }
    if (IA_ToggleFly)
    {
        EIC->BindAction(IA_ToggleFly, ETriggerEvent::Started, this, &AMassDspPlayerCharacter::Handle_ToggleFly);
    }
    if (IA_Run)
    {
        EIC->BindAction(IA_Run, ETriggerEvent::Started, this, &AMassDspPlayerCharacter::Handle_RunStart);
        EIC->BindAction(IA_Run, ETriggerEvent::Completed, this, &AMassDspPlayerCharacter::Handle_RunStop);
    }
    if (IA_CameraYaw)
    {
        EIC->BindAction(IA_CameraYaw, ETriggerEvent::Started, this, &AMassDspPlayerCharacter::Handle_CameraYawStart);
        EIC->BindAction(IA_CameraYaw, ETriggerEvent::Completed, this, &AMassDspPlayerCharacter::Handle_CameraYawStop);
        EIC->BindAction(IA_CameraYaw, ETriggerEvent::Triggered, this, &AMassDspPlayerCharacter::Handle_CameraYaw);
    }
    if (IA_ToggleBuildMode)
    {
        EIC->BindAction(IA_ToggleBuildMode, ETriggerEvent::Started, this, &AMassDspPlayerCharacter::Handle_ToggleBuildMode);
    }
    if (IA_FlyVertical)
    {
        EIC->BindAction(IA_FlyVertical, ETriggerEvent::Triggered, this, &AMassDspPlayerCharacter::Handle_FlyVertical);
    }
}

// 
//  输入处理实现
// 

void AMassDspPlayerCharacter::Handle_Move(const FInputActionValue& Value)
{
    const FVector2D MoveDir = Value.Get<FVector2D>();
    if (MoveDir.IsNearlyZero()) return;

    // 以控制器的 Yaw 为朝向基准，在水平面内投影移动方向
    const FRotator YawRot(0.f, GetControlRotation().Yaw, 0.f);
    const FVector FwdDir = FRotationMatrix(YawRot).GetUnitAxis(EAxis::X);
    const FVector RightDir = FRotationMatrix(YawRot).GetUnitAxis(EAxis::Y);

    AddMovementInput(FwdDir, MoveDir.Y);
    AddMovementInput(RightDir, MoveDir.X);
}

void AMassDspPlayerCharacter::Handle_Jump()
{
    if (!bIsFlyingMode)
        Jump();
}

void AMassDspPlayerCharacter::Handle_StopJumping()
{
    StopJumping();
}

void AMassDspPlayerCharacter::Handle_ToggleFly()
{
    bIsFlyingMode = !bIsFlyingMode;
    UCharacterMovementComponent* MoveComp = GetCharacterMovement();
    if (bIsFlyingMode)
    {
        MoveComp->SetMovementMode(MOVE_Flying);
        MoveComp->MaxFlySpeed = FlySpeed;
    }
    else
    {
        MoveComp->SetMovementMode(MOVE_Walking);
    }
}

void AMassDspPlayerCharacter::Handle_RunStart()
{
    bIsRunning = true;
    GetCharacterMovement()->MaxWalkSpeed = RunSpeed;
    if (bIsFlyingMode)
        GetCharacterMovement()->MaxFlySpeed = FlySpeed * 1.5f;
}

void AMassDspPlayerCharacter::Handle_RunStop()
{
    bIsRunning = false;
    GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
    if (bIsFlyingMode)
        GetCharacterMovement()->MaxFlySpeed = FlySpeed;
}

void AMassDspPlayerCharacter::Handle_CameraYawStart()
{
    bIsRightMouseHeld = true;
    // 显示/隐藏鼠标指针由蓝图或 PlayerController 控制
}

void AMassDspPlayerCharacter::Handle_CameraYawStop()
{
    bIsRightMouseHeld = false;
}

void AMassDspPlayerCharacter::Handle_CameraYaw(const FInputActionValue& Value)
{
    if (!bIsRightMouseHeld) return;
    const float Delta = Value.Get<float>();
    // 只旋转 Yaw，Pitch 由 SpringArm 的固定偏移保持不变
    AddControllerYawInput(Delta);
}

void AMassDspPlayerCharacter::Handle_ToggleBuildMode()
{
    // 将事件广播给 HUD
    static bool bBuildMode = false;
    bBuildMode = !bBuildMode;
    OnBuildModeToggled.Broadcast(bBuildMode);
}

void AMassDspPlayerCharacter::Handle_FlyVertical(const FInputActionValue& Value)
{
    if (!bIsFlyingMode) return;
    const float Axis = Value.Get<float>();
    AddMovementInput(FVector::UpVector, Axis);
}
