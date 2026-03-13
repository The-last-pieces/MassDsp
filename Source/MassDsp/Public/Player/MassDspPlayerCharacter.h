// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "MassDspPlayerCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UInputMappingContext;
class UInputAction;
class UMassDspPlayerInventoryComponent;
struct FInputActionValue;

/**
 * 主玩家角色  第三人称俯视角 3C
 *
 * 设计约定
 * 
 * 摄像机：SpringArm TargetArmLength=1500，固定 Pitch=-60，
 *          支持鼠标右键拖拽旋转 Yaw，角色朝向随移动方向转动。
 * 移动  ：WASD 地面行走；Shift 持有时速度提升至 RunSpeed。
 * 跳跃  ：Space；飞行时允许三次元方向输入（Z 轴 + WASD）。
 * 飞行  ：V 键切换 MOVE_Flying / MOVE_Walking。
 * 模式  ：Tab 键广播 OnBuildModeToggled 事件，HUD 监听后切换建造平移视角。
 * 
 * 注意：在 BP_MainPlayer 中必须手动赋值以下 UPROPERTY：
 *       DefaultMappingContext、所有 IA_* 引用。
 */
UCLASS()
class MASSDSP_API AMassDspPlayerCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    AMassDspPlayerCharacter();

    virtual void Tick(float DeltaTime) override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

protected:
    virtual void BeginPlay() override;

    //  摄像机组件 
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
    TObjectPtr<USpringArmComponent> CameraBoom;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
    TObjectPtr<UCameraComponent> FollowCamera;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory")
    TObjectPtr<UMassDspPlayerInventoryComponent> InventoryComponent;

    //  Enhanced Input 资产（在 BP_MainPlayer 中赋值）
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
    TObjectPtr<UInputMappingContext> DefaultMappingContext;

    /** WASD 移动（Axis2D） */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
    TObjectPtr<UInputAction> IA_Move;

    /** Space 跳跃（Triggered / Completed） */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
    TObjectPtr<UInputAction> IA_Jump;

    /** V 键切换飞行（Started） */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
    TObjectPtr<UInputAction> IA_ToggleFly;

    /** Shift 奔跑（Started / Completed） */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
    TObjectPtr<UInputAction> IA_Run;

    /** 鼠标右键拖拽摄像机 Yaw（Axis1D） */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
    TObjectPtr<UInputAction> IA_CameraYaw;

    /** Tab 切换建造模式 */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
    TObjectPtr<UInputAction> IA_ToggleBuildMode;

    /** 飞行时 Q/E 控制垂直升降（Axis1D） */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
    TObjectPtr<UInputAction> IA_FlyVertical;

    //  运动参数 
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement")
    float WalkSpeed = 400.f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement")
    float RunSpeed = 800.f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement")
    float FlySpeed = 1200.f;

    //  运行时状态（AnimInstance 读取）
public:
    bool bIsRunning = false;

    DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBuildModeToggled, bool, bBuildMode);
    /** HUD 可以绑定此事件来切换建造平移模式 */
    UPROPERTY(BlueprintAssignable, Category = "Events")
    FOnBuildModeToggled OnBuildModeToggled;

private:
    bool bIsFlyingMode = false;
    bool bIsRightMouseHeld = false;

    //  输入处理 
    void Handle_Move(const FInputActionValue& Value);
    void Handle_Jump();
    void Handle_StopJumping();
    void Handle_ToggleFly();
    void Handle_RunStart();
    void Handle_RunStop();
    void Handle_CameraYawStart();
    void Handle_CameraYawStop();
    void Handle_CameraYaw(const FInputActionValue& Value);
    void Handle_ToggleBuildMode();
    void Handle_FlyVertical(const FInputActionValue& Value);
};
