#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameConst.h"
#include "UI/MassDspBuildingWidget.h"
#include "MassDspHotbarWidget.generated.h"

class UButton;
class UTextBlock;
class UBorder;
struct FMassEntityHandle;

//  每个热键格执行的动作类型 
UENUM()
enum class EHotbarAction : uint8
{
    None,
    PlaceBuilding,
    PlaceBelt,
    TestScene,
};

//  单个热键格的数据定义 
struct FHotbarSlotDef
{
    FString ShortName;
    FString FullName;
    EHotbarAction Action = EHotbarAction::None;
    EBuildingType BuildingType = EBuildingType::None;
    EBeltType BeltType = EBeltType::None;
    int32 TestCaseIdx = 0;
};

UCLASS(Blueprintable)
class MASSDSP_API UMassDspHotbarWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    //  数字键转发（备用）
    void OnHotbarKeyPressed(int32 SlotIndex);

    //  鼠标 / 功能键（HUD InputComponent 直接绑定）
    void OnMouseLeftClick();
    void OnMouseRightClick();
    void OnScrollUp();
    void OnScrollDown();
    void OnInteractKey();

    //  建造交互运行状态（HUD Canvas 读取此处状态来绘制）
    FVector CachedHitLocation = FVector::ZeroVector;
    FRotator BuildingRotation = FRotator::ZeroRotator;
    bool bBeltHoverSnapped = false;
    FVector BeltHoverSnapLocation = FVector::ZeroVector;
    FQuat BeltHoverSnapRotation = FQuat::Identity;
    float BeltHoverSnapExtend = 0.f;

    /** 当前已打开的建筑交互 Widget（同时只存一个） */
    UPROPERTY()
    TObjectPtr<UMassDspBuildingWidget> CurrentBuildingWidget;

    /** 视锥可见检测（HUD DrawInteractionHint 调用） */
    bool IsBuildingInViewCone(const FVector& WorldLoc) const;

    // 9 个无参回调：作为 UButton::OnClicked 目标，同时 public 供 HUD BindKey 直接绑定
    UFUNCTION()
    void OnSlot0Clicked();
    UFUNCTION()
    void OnSlot1Clicked();
    UFUNCTION()
    void OnSlot2Clicked();
    UFUNCTION()
    void OnSlot3Clicked();
    UFUNCTION()
    void OnSlot4Clicked();
    UFUNCTION()
    void OnSlot5Clicked();
    UFUNCTION()
    void OnSlot6Clicked();
    UFUNCTION()
    void OnSlot7Clicked();
    UFUNCTION()
    void OnSlot8Clicked();

    static constexpr float BuildingInteractRadius = 4000.f;
    static constexpr float BuildingRotationStep = 15.f;

protected:
    virtual void NativeConstruct() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

private:
    void InitSlotDefs();
    void BindSlotWidgets();
    void ExecuteSlot(int32 SlotIndex);
    void RefreshSlotAvailability();
    void UpdateBuildPreview();
    bool GetWorldHitLocation(FVector& OutLoc) const;
    bool IsSlotUnlocked(const FHotbarSlotDef& Def) const;
    FText GetSlotLockedReason(const FHotbarSlotDef& Def) const;
    void ShowLockedMessage(const FText& Message) const;
    /** F 键触发：为指定建筑创建交互 Widget，若已有则先关闭 */
    void CreateBuildingWidgets(const FMassEntityHandle& Entity, EBuildingType Type);
    void SyncHighlight();
    int32 GetCurrentActiveSlot() const;
    void SetSlotHighlight(int32 SlotIndex, bool bActive);

    TArray<FHotbarSlotDef> SlotDefs;

    UPROPERTY()
    TArray<TObjectPtr<UButton>> SlotButtons;

    UPROPERTY()
    TArray<TObjectPtr<UBorder>> SlotBorders;

    TArray<bool> SlotUnlockedStates;

    int32 LastHighlightedSlot = -1;

public:
    static constexpr float SlotSize = 64.f;
    static constexpr float SlotGap = 6.f;
    static constexpr float BottomMargin = 30.f;
    static constexpr int32 TotalSlots = 9;
};
