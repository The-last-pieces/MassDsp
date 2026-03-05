#include "Tools/MaterialGeneratorUtils.h"

#if WITH_EDITOR

// 核心依赖
#include "Materials/Material.h"
#include "Factories/MaterialFactoryNew.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/FileHelper.h"
#include "Misc/SecureHash.h"

// 材质节点
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionSine.h"
#include "Materials/MaterialExpressionAbs.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionComment.h"
#include "Materials/MaterialExpressionDDX.h"
#include "Materials/MaterialExpressionDDY.h"
#include "Materials/MaterialExpressionMax.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionClamp.h"
#include "Materials/MaterialExpressionFrac.h"
#include "Materials/MaterialExpressionVertexColor.h"

#include "UI/MassDspMinerWidget.h"
#include "UI/MassDspStorageWidget.h"
#include "UI/MassDspAssemblerWidget.h"
#include "UI/MassDspLogisticsTowerWidget.h"

// UMG Editor
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Kismet2/KismetEditorUtilities.h"

// UMG Runtime Components
#include "WidgetBlueprintFactory.h"

#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Components/Button.h"
#include "Components/Border.h"

// 传送带材质：两侧白边 + 中间倒V形（∧）箭头动画
// 动画速度由 Speed 标量参数控制（UV/s），不再依赖顶点色
void UMaterialGeneratorUtils::CreateConveyorMaterial()
{
    const FString AssetName = TEXT("M_Belt");
    const FString PackageName = TEXT("/Game/Assets/") + AssetName;
    const FString SourceFilePath = FString(TEXT(__FILE__));
    const FString HashPrefix = TEXT("[SOURCE_HASH]:");

    // --- 1. 源码变更检测 (MD5) ---
    FString FileContent;
    FString CurrentHash;

    if (FFileHelper::LoadFileToString(FileContent, *SourceFilePath))
    {
        CurrentHash = HashPrefix + FMD5::HashAnsiString(*FileContent);
    }
    else
    {
        CurrentHash = HashPrefix + FDateTime::Now().ToString();
    }

    if (UMaterial* ExistingMaterial = LoadObject<UMaterial>(nullptr, *PackageName))
    {
        for (UMaterialExpression* Expr : ExistingMaterial->GetExpressions())
        {
            if (UMaterialExpressionComment* CommentNode = Cast<UMaterialExpressionComment>(Expr))
            {
                if (CommentNode->Text.Equals(CurrentHash))
                {
                    return;
                }
            }
        }
    }

    // --- 2. 创建资产 ---
    UPackage* Package = CreatePackage(*PackageName);
    UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
    UMaterial* Material = static_cast<UMaterial*>(Factory->FactoryCreateNew(UMaterial::StaticClass(), Package, *AssetName, RF_Standalone | RF_Public, nullptr, GWarn));

    if (!Material) return;

    Material->bEnableResponsiveAA = false;

    auto CreateNode = [&](const UClass* Class, int32 X, int32 Y) -> UMaterialExpression*
    {
        UMaterialExpression* Node = NewObject<UMaterialExpression>(Material, Class);
        Material->GetExpressionCollection().AddExpression(Node);
        Node->MaterialExpressionEditorX = X;
        Node->MaterialExpressionEditorY = Y;
        return Node;
    };

    // --- 3. 哈希注释节点（源码变更检测用）---
    auto* HashComment = Cast<UMaterialExpressionComment>(CreateNode(UMaterialExpressionComment::StaticClass(), -1800, -500));
    HashComment->Text = CurrentHash;
    HashComment->CommentColor = FLinearColor::Black;
    HashComment->SizeX = 400;
    HashComment->SizeY = 100;

    // --- 4. 动态参数（运行时可通过 DynMat->SetXxxParameterValue 修改）---
    // Speed：全局速度倍率；每条传送带的实际速度已烘焙到顶点色 R 通道
    auto* SpeedParam = Cast<UMaterialExpressionScalarParameter>(CreateNode(UMaterialExpressionScalarParameter::StaticClass(), -1600, 0));
    SpeedParam->ParameterName = TEXT("Speed");
    SpeedParam->DefaultValue = 1.0f;

    auto* ArrowColorParam = Cast<UMaterialExpressionVectorParameter>(CreateNode(UMaterialExpressionVectorParameter::StaticClass(), -1600, 120));
    ArrowColorParam->ParameterName = TEXT("ArrowColor");
    ArrowColorParam->DefaultValue = FLinearColor(1.0f, 0.65f, 0.0f, 1.0f);

    // --- 5. 静态参数（在材质实例 Asset 里配置，不需要运行时修改）---
    auto* ArrowSizeParam = Cast<UMaterialExpressionScalarParameter>(CreateNode(UMaterialExpressionScalarParameter::StaticClass(), -1600, 300));
    ArrowSizeParam->ParameterName = TEXT("ArrowSize");
    ArrowSizeParam->DefaultValue = 0.45f; // 每个箭头条纹的宽度（UV 单位，占 Period 的比例分子）

    auto* ArrowSpaceParam = Cast<UMaterialExpressionScalarParameter>(CreateNode(UMaterialExpressionScalarParameter::StaticClass(), -1600, 420));
    ArrowSpaceParam->ParameterName = TEXT("ArrowSpace");
    ArrowSpaceParam->DefaultValue = 1.0f; // 相邻箭头之间的间隔（UV 单位）

    auto* BorderWidthParam = Cast<UMaterialExpressionScalarParameter>(CreateNode(UMaterialExpressionScalarParameter::StaticClass(), -1600, 540));
    BorderWidthParam->ParameterName = TEXT("BorderWidth");
    BorderWidthParam->DefaultValue = 0.08f; // 两侧白边宽度（0~0.5 的 U 比例）

    auto* BorderColorParam = Cast<UMaterialExpressionVectorParameter>(CreateNode(UMaterialExpressionVectorParameter::StaticClass(), -1600, 660));
    BorderColorParam->ParameterName = TEXT("BorderColor");
    BorderColorParam->DefaultValue = FLinearColor(0.9f, 0.9f, 0.9f, 1.0f);

    auto* CenterColorParam = Cast<UMaterialExpressionVectorParameter>(CreateNode(UMaterialExpressionVectorParameter::StaticClass(), -1600, 780));
    CenterColorParam->ParameterName = TEXT("CenterColor");
    CenterColorParam->DefaultValue = FLinearColor(0.04f, 0.04f, 0.04f, 1.0f);

    // --- 6. 公共常量 ---
    auto* ConstOne = Cast<UMaterialExpressionConstant>(CreateNode(UMaterialExpressionConstant::StaticClass(), -1800, 920));
    ConstOne->R = 1.0f;

    auto* ConstEpsilon = Cast<UMaterialExpressionConstant>(CreateNode(UMaterialExpressionConstant::StaticClass(), -1800, 980));
    ConstEpsilon->R = 0.0001f;

    // 人字形斜率：abs(U-0.5) * Slope 控制箭头 V 形的弯曲深度（固定值，如需调整可改为参数）
    auto* ConstAngleSlope = Cast<UMaterialExpressionConstant>(CreateNode(UMaterialExpressionConstant::StaticClass(), -1800, 1040));
    ConstAngleSlope->R = 2.0f;

    // --- 7. 基础输入节点 ---
    auto* TexCoord = Cast<UMaterialExpressionTextureCoordinate>(CreateNode(UMaterialExpressionTextureCoordinate::StaticClass(), -1350, 200));
    auto* Time = Cast<UMaterialExpressionTime>(CreateNode(UMaterialExpressionTime::StaticClass(), -1350, 320));

    // --- 8. 提取 U / V ---
    auto* MaskU = Cast<UMaterialExpressionComponentMask>(CreateNode(UMaterialExpressionComponentMask::StaticClass(), -1150, 200));
    MaskU->Input.Expression = TexCoord;
    MaskU->R = 1;
    MaskU->G = 0;
    MaskU->B = 0;

    auto* MaskV = Cast<UMaterialExpressionComponentMask>(CreateNode(UMaterialExpressionComponentMask::StaticClass(), -1150, 320));
    MaskV->Input.Expression = TexCoord;
    MaskV->R = 0;
    MaskV->G = 1;
    MaskV->B = 0;

    // --- 9. 动画速度：TimeDelta = Time * Speed（Speed单位：UV/s = BeltSpeed(cm/s)/100）---
    auto* TimeDelta = Cast<UMaterialExpressionMultiply>(CreateNode(UMaterialExpressionMultiply::StaticClass(), -950, 360));
    TimeDelta->A.Expression = Time;
    TimeDelta->B.Expression = SpeedParam;

    // AnimV = V - TimeDelta（沿传送带前进方向滚动）
    auto* AnimV = Cast<UMaterialExpressionSubtract>(CreateNode(UMaterialExpressionSubtract::StaticClass(), -750, 320));
    AnimV->A.Expression = MaskV;
    AnimV->B.Expression = TimeDelta;

    // =============================================
    // A. 箭头/人字形遮罩
    //
    // 核心公式：
    //   ChevronOff = abs(U - 0.5) * Slope
    //   PhaseRaw  = AnimV + ChevronOff
    //     → 中心(U=0.5)相位最低，边缘相位最高
    //     → lit band 的前沿（高V侧）在中心最靠前，形成 ∧ 尖指向运动方向
    //   PhaseNorm = PhaseRaw / Period
    //   PhaseFrac = frac(PhaseNorm)
    //   ArrowFrac  = ArrowSize / Period
    //   ArrowMask  = saturate((ArrowFrac - PhaseFrac) / PixelWidth)
    // =============================================

    // abs(U - 0.5)
    auto* HalfU = Cast<UMaterialExpressionSubtract>(CreateNode(UMaterialExpressionSubtract::StaticClass(), -550, 200));
    HalfU->A.Expression = MaskU;
    HalfU->ConstB = 0.5f;

    auto* AbsHalfU = Cast<UMaterialExpressionAbs>(CreateNode(UMaterialExpressionAbs::StaticClass(), -380, 200));
    AbsHalfU->Input.Expression = HalfU;

    // * AngleSlope => 人字形纵向偏移
    auto* AngleOffset = Cast<UMaterialExpressionMultiply>(CreateNode(UMaterialExpressionMultiply::StaticClass(), -200, 200));
    AngleOffset->A.Expression = AbsHalfU;
    AngleOffset->B.Expression = ConstAngleSlope;

    // PhaseRaw = AnimV + AngleOffset（+ 使中心相位低于边缘 → ∧ 尖朝前）
    auto* PhaseRaw = Cast<UMaterialExpressionAdd>(CreateNode(UMaterialExpressionAdd::StaticClass(), 0, 260));
    PhaseRaw->A.Expression = AnimV;
    PhaseRaw->B.Expression = AngleOffset;

    // Period = ArrowSize + ArrowSpace
    auto* Period = Cast<UMaterialExpressionAdd>(CreateNode(UMaterialExpressionAdd::StaticClass(), -1350, 360));
    Period->A.Expression = ArrowSizeParam;
    Period->B.Expression = ArrowSpaceParam;

    // ArrowFraction = ArrowSize / Period
    auto* ArrowFrac = Cast<UMaterialExpressionDivide>(CreateNode(UMaterialExpressionDivide::StaticClass(), -1150, 360));
    ArrowFrac->A.Expression = ArrowSizeParam;
    ArrowFrac->B.Expression = Period;

    // PhaseNorm = PhaseRaw / Period
    auto* PhaseNorm = Cast<UMaterialExpressionDivide>(CreateNode(UMaterialExpressionDivide::StaticClass(), 50, 260));
    PhaseNorm->A.Expression = PhaseRaw;
    PhaseNorm->B.Expression = Period;

    // PhaseFrac = frac(PhaseNorm)
    auto* PhaseFrac = Cast<UMaterialExpressionFrac>(CreateNode(UMaterialExpressionFrac::StaticClass(), 250, 260));
    PhaseFrac->Input.Expression = PhaseNorm;

    // 屏幕空间导数用于抗锯齿（1px 边缘宽度）
    auto* DdxPhaseNorm = Cast<UMaterialExpressionDDX>(CreateNode(UMaterialExpressionDDX::StaticClass(), 250, 380));
    DdxPhaseNorm->Value.Expression = PhaseNorm;

    auto* DdyPhaseNorm = Cast<UMaterialExpressionDDY>(CreateNode(UMaterialExpressionDDY::StaticClass(), 250, 480));
    DdyPhaseNorm->Value.Expression = PhaseNorm;

    auto* AbsDdxPhaseNorm = Cast<UMaterialExpressionAbs>(CreateNode(UMaterialExpressionAbs::StaticClass(), 420, 380));
    AbsDdxPhaseNorm->Input.Expression = DdxPhaseNorm;

    auto* AbsDdyPhaseNorm = Cast<UMaterialExpressionAbs>(CreateNode(UMaterialExpressionAbs::StaticClass(), 420, 480));
    AbsDdyPhaseNorm->Input.Expression = DdyPhaseNorm;

    auto* MaxDerivativePhaseNorm = Cast<UMaterialExpressionMax>(CreateNode(UMaterialExpressionMax::StaticClass(), 580, 430));
    MaxDerivativePhaseNorm->A.Expression = AbsDdxPhaseNorm;
    MaxDerivativePhaseNorm->B.Expression = AbsDdyPhaseNorm;

    auto* SafeDerivativePhaseNorm = Cast<UMaterialExpressionMax>(CreateNode(UMaterialExpressionMax::StaticClass(), 730, 430));
    SafeDerivativePhaseNorm->A.Expression = MaxDerivativePhaseNorm;
    SafeDerivativePhaseNorm->B.Expression = ConstEpsilon;

    // ArrowMask = saturate((ArrowFrac - PhaseFrac) / SafeDerivativePhaseNorm)
    auto* SubArrowFracPhase = Cast<UMaterialExpressionSubtract>(CreateNode(UMaterialExpressionSubtract::StaticClass(), 580, 260));
    SubArrowFracPhase->A.Expression = ArrowFrac;
    SubArrowFracPhase->B.Expression = PhaseFrac;

    auto* ArrowDivideDerivative = Cast<UMaterialExpressionDivide>(CreateNode(UMaterialExpressionDivide::StaticClass(), 880, 340));
    ArrowDivideDerivative->A.Expression = SubArrowFracPhase;
    ArrowDivideDerivative->B.Expression = SafeDerivativePhaseNorm;

    auto* ArrowMask = Cast<UMaterialExpressionClamp>(CreateNode(UMaterialExpressionClamp::StaticClass(), 1050, 340));
    ArrowMask->Input.Expression = ArrowDivideDerivative;
    ArrowMask->MinDefault = 0.0f;
    ArrowMask->MaxDefault = 1.0f;

    // =============================================
    // B. 两侧边缘遮罩（DDX/DDY AA，始终无锯齿）
    // =============================================

    auto* DdxCoordU = Cast<UMaterialExpressionDDX>(CreateNode(UMaterialExpressionDDX::StaticClass(), -950, 620));
    DdxCoordU->Value.Expression = MaskU;

    auto* DdyCoordU = Cast<UMaterialExpressionDDY>(CreateNode(UMaterialExpressionDDY::StaticClass(), -950, 720));
    DdyCoordU->Value.Expression = MaskU;

    auto* AbsDdxCoordU = Cast<UMaterialExpressionAbs>(CreateNode(UMaterialExpressionAbs::StaticClass(), -790, 620));
    AbsDdxCoordU->Input.Expression = DdxCoordU;

    auto* AbsDdyCoordU = Cast<UMaterialExpressionAbs>(CreateNode(UMaterialExpressionAbs::StaticClass(), -790, 720));
    AbsDdyCoordU->Input.Expression = DdyCoordU;

    auto* MaxDerivativeU = Cast<UMaterialExpressionMax>(CreateNode(UMaterialExpressionMax::StaticClass(), -630, 670));
    MaxDerivativeU->A.Expression = AbsDdxCoordU;
    MaxDerivativeU->B.Expression = AbsDdyCoordU;

    auto* SafeDerivativeU = Cast<UMaterialExpressionMax>(CreateNode(UMaterialExpressionMax::StaticClass(), -470, 670));
    SafeDerivativeU->A.Expression = MaxDerivativeU;
    SafeDerivativeU->B.Expression = ConstEpsilon;

    // 左边缘：saturate((BorderWidth - U) / SafeDerivativeU)
    auto* LeftRaw = Cast<UMaterialExpressionSubtract>(CreateNode(UMaterialExpressionSubtract::StaticClass(), -950, 820));
    LeftRaw->A.Expression = BorderWidthParam;
    LeftRaw->B.Expression = MaskU;

    auto* LeftNorm = Cast<UMaterialExpressionDivide>(CreateNode(UMaterialExpressionDivide::StaticClass(), -750, 820));
    LeftNorm->A.Expression = LeftRaw;
    LeftNorm->B.Expression = SafeDerivativeU;

    auto* LeftEdge = Cast<UMaterialExpressionClamp>(CreateNode(UMaterialExpressionClamp::StaticClass(), -580, 820));
    LeftEdge->Input.Expression = LeftNorm;
    LeftEdge->MinDefault = 0.0f;
    LeftEdge->MaxDefault = 1.0f;

    // 右边缘：saturate((BorderWidth - (1-U)) / SafeDerivativeU)
    auto* OneMinusU = Cast<UMaterialExpressionSubtract>(CreateNode(UMaterialExpressionSubtract::StaticClass(), -950, 940));
    OneMinusU->A.Expression = ConstOne;
    OneMinusU->B.Expression = MaskU;

    auto* RightRaw = Cast<UMaterialExpressionSubtract>(CreateNode(UMaterialExpressionSubtract::StaticClass(), -750, 940));
    RightRaw->A.Expression = BorderWidthParam;
    RightRaw->B.Expression = OneMinusU;

    auto* RightNorm = Cast<UMaterialExpressionDivide>(CreateNode(UMaterialExpressionDivide::StaticClass(), -580, 940));
    RightNorm->A.Expression = RightRaw;
    RightNorm->B.Expression = SafeDerivativeU;

    auto* RightEdge = Cast<UMaterialExpressionClamp>(CreateNode(UMaterialExpressionClamp::StaticClass(), -410, 940));
    RightEdge->Input.Expression = RightNorm;
    RightEdge->MinDefault = 0.0f;
    RightEdge->MaxDefault = 1.0f;

    // BorderMask = max(LeftEdge, RightEdge)
    auto* BorderMask = Cast<UMaterialExpressionMax>(CreateNode(UMaterialExpressionMax::StaticClass(), -240, 880));
    BorderMask->A.Expression = LeftEdge;
    BorderMask->B.Expression = RightEdge;

    // =============================================
    // C. 颜色合成
    //   顶点色 R=1.0：顶/底面，走完整箭头 + 边框流程
    //   顶点色 R=0.0：左/右/封口面，直接输出 BorderColor
    //
    //   CenterArrow    = lerp(CenterColor, ArrowColor, ArrowMask)        — 中心箭头区
    //   TopBottomColor = lerp(CenterArrow, BorderColor, BorderMask)      — 两侧白边叠加
    //   FinalColor     = lerp(BorderColor, TopBottomColor, VertexColor.R) — R=0 侧面→BorderColor
    // =============================================

    auto* VertexColorNode = Cast<UMaterialExpressionVertexColor>(CreateNode(UMaterialExpressionVertexColor::StaticClass(), 1050, 480));

    auto* MaskVertexR = Cast<UMaterialExpressionComponentMask>(CreateNode(UMaterialExpressionComponentMask::StaticClass(), 1200, 480));
    MaskVertexR->Input.Expression = VertexColorNode;
    MaskVertexR->R = 1;
    MaskVertexR->G = 0;
    MaskVertexR->B = 0;

    auto* CenterArrow = Cast<UMaterialExpressionLinearInterpolate>(CreateNode(UMaterialExpressionLinearInterpolate::StaticClass(), 1400, 100));
    CenterArrow->A.Expression = CenterColorParam;
    CenterArrow->B.Expression = ArrowColorParam;
    CenterArrow->Alpha.Expression = ArrowMask;

    auto* TopBottomColor = Cast<UMaterialExpressionLinearInterpolate>(CreateNode(UMaterialExpressionLinearInterpolate::StaticClass(), 1600, 300));
    TopBottomColor->A.Expression = CenterArrow;
    TopBottomColor->B.Expression = BorderColorParam;
    TopBottomColor->Alpha.Expression = BorderMask;

    // R=0 → BorderColor（前后左右侧面），R=1 → 顶底面完整效果
    auto* FinalColor = Cast<UMaterialExpressionLinearInterpolate>(CreateNode(UMaterialExpressionLinearInterpolate::StaticClass(), 1800, 400));
    FinalColor->A.Expression = BorderColorParam;
    FinalColor->B.Expression = TopBottomColor;
    FinalColor->Alpha.Expression = MaskVertexR;

    // --- 10. 输出与保存 ---
    Material->SetShadingModel(MSM_DefaultLit);
    Material->GetEditorOnlyData()->BaseColor.Expression = FinalColor;
    Material->TwoSided = false;

    // 微量 WPO 动画——让 TAA 每帧记录非零 MotionVector，消除静止 Mesh 的残影
    auto* WpoSine = Cast<UMaterialExpressionSine>(CreateNode(UMaterialExpressionSine::StaticClass(), 1550, 550));
    WpoSine->Input.Expression = Time;

    auto* WpoScale = Cast<UMaterialExpressionMultiply>(CreateNode(UMaterialExpressionMultiply::StaticClass(), 1700, 550));
    WpoScale->A.Expression = WpoSine;
    WpoScale->ConstB = 0.1f;

    Material->GetEditorOnlyData()->WorldPositionOffset.Expression = WpoScale;

    Material->PostEditChange();
    auto _ = Material->MarkPackageDirty();
    FAssetRegistryModule::AssetCreated(Material);

    UE_LOG(LogTemp, Log, TEXT("Conveyor Material Updated (Chevron Style). Hash: %s"), *CurrentHash);
}

// ─────────────────────────────────────────────────────────────────────────────
//  UMG Widget 蓝图生成器
// ─────────────────────────────────────────────────────────────────────────────


// ─── 颜色常量 ────────────────────────────────────────────────────────────────
namespace WidgetColors
{
    // 背景
    static constexpr FLinearColor Overlay{0.02f, 0.02f, 0.05f, 0.82f};
    static constexpr FLinearColor CardBg{0.06f, 0.06f, 0.10f, 1.00f};
    // static const FLinearColor CardBorder{0.18f, 0.18f, 0.28f, 1.00f};
    static constexpr FLinearColor Divider{0.15f, 0.15f, 0.22f, 1.00f};
    // 文字
    static constexpr FLinearColor TextTitle{0.95f, 0.95f, 1.00f, 1.00f};
    static constexpr FLinearColor TextLabel{0.55f, 0.55f, 0.70f, 1.00f};
    static constexpr FLinearColor TextValue{0.92f, 0.92f, 1.00f, 1.00f};
    // 进度条填充（每种建筑不同色调）
    static constexpr FLinearColor FillMiner{0.22f, 0.56f, 0.90f, 1.00f}; // 蓝
    static constexpr FLinearColor FillStorage{0.20f, 0.78f, 0.42f, 1.00f}; // 绿
    static constexpr FLinearColor FillAssembler{0.92f, 0.64f, 0.18f, 1.00f}; // 琥珀
    static constexpr FLinearColor FillLogistics{0.15f, 0.75f, 0.82f, 1.00f}; // 青（物流塔）
    static constexpr FLinearColor BarBg{0.06f, 0.08f, 0.12f, 1.00f};
    // 按钮
    static constexpr FLinearColor BtnClose{0.40f, 0.08f, 0.08f, 1.00f};
    static constexpr FLinearColor BtnCloseHover{0.75f, 0.15f, 0.15f, 1.00f};
}

// ─── 内部构建辅助（文件作用域）────────────────────────────────────────────────

struct FWidgetBuilder
{
    UWidgetTree* Tree = nullptr;
    UCanvasPanel* Root = nullptr;
    float OX = 0.f; // 卡片左上角 X（相对画布中心）
    float OY = 0.f; // 卡片左上角 Y（相对画布中心）

    // 在画布上放置控件（坐标以卡片左上角为原点）
    UCanvasPanelSlot* Place(UWidget* W, float X, float Y, float W2, float H, FVector2D Align = FVector2D::ZeroVector)
    {
        UCanvasPanelSlot* Slot = Root->AddChildToCanvas(W);
        Slot->SetAnchors(FAnchors(0.5f, 0.5f)); // 相对画布中心点
        Slot->SetAlignment(Align);
        Slot->SetPosition(FVector2D(OX + X, OY + Y));
        Slot->SetSize(FVector2D(W2, H));
        return Slot;
    }

    // 快速创建并放置 TextBlock
    UTextBlock* Text(FName Name, const FString& Content, float X, float Y, float W, float H,
                     FLinearColor Color, int32 Pt = 13, bool bBold = false)
    {
        UTextBlock* TB = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
        TB->SetText(FText::FromString(Content));
        TB->SetColorAndOpacity(FSlateColor(Color));
        FSlateFontInfo F = TB->GetFont();
        F.Size = Pt;
        if (bBold) F.TypefaceFontName = FName("Bold");
        TB->SetFont(F);
        Place(TB, X, Y, W, H);
        return TB;
    }

    // 创建并放置 ProgressBar
    UProgressBar* Bar(FName Name, float X, float Y, float W, float H, FLinearColor FillColor)
    {
        UProgressBar* PB = Tree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), Name);
        PB->SetPercent(0.45f); // 预览值

        FProgressBarStyle Style = PB->GetWidgetStyle();
        FSlateBrush FillBrush;
        FillBrush.TintColor = FSlateColor(FillColor);
        FillBrush.DrawAs = ESlateBrushDrawType::Box;
        Style.FillImage = FillBrush;

        FSlateBrush BgBrush;
        BgBrush.TintColor = FSlateColor(WidgetColors::BarBg);
        BgBrush.DrawAs = ESlateBrushDrawType::Box;
        Style.BackgroundImage = BgBrush;

        PB->SetWidgetStyle(Style);
        Place(PB, X, Y, W, H);
        return PB;
    }

    // 创建并放置背景 Border（纯色填充矩形）
    UBorder* Rect(FName Name, float X, float Y, float W, float H, FLinearColor Color)
    {
        UBorder* B = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), Name);
        FSlateBrush Br;
        Br.TintColor = FSlateColor(Color);
        Br.DrawAs = ESlateBrushDrawType::Box;
        B->SetBrush(Br);
        B->SetPadding(FMargin(0.f));
        Place(B, X, Y, W, H);
        return B;
    }

    // 创建关闭按钮（右上角）
    UButton* CloseButton(float CardW)
    {
        UButton* Btn = Tree->ConstructWidget<UButton>(UButton::StaticClass(), FName("Button_Close"));

        FButtonStyle Style = Btn->GetStyle();
        auto MakeBrush = [](FLinearColor C)
        {
            FSlateBrush Br;
            Br.TintColor = FSlateColor(C);
            Br.DrawAs = ESlateBrushDrawType::Box;
            return Br;
        };
        Style.Normal = MakeBrush(WidgetColors::BtnClose);
        Style.Hovered = MakeBrush(WidgetColors::BtnCloseHover);
        Style.Pressed = MakeBrush(FLinearColor(0.25f, 0.04f, 0.04f, 1.f));
        Style.SetNormalPadding(FMargin(0.f));
        Style.SetPressedPadding(FMargin(0.f));
        Btn->SetStyle(Style);

        UTextBlock* X = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName("TextBlock_CloseX"));
        X->SetText(FText::FromString(TEXT("✕")));
        X->SetColorAndOpacity(FSlateColor(FLinearColor::White));
        FSlateFontInfo F = X->GetFont();
        F.Size = 12;
        X->SetFont(F);
        X->SetJustification(ETextJustify::Center);
        Btn->SetContent(X);

        Place(Btn, CardW - 38.f, 8.f, 30.f, 30.f);
        return Btn;
    }
};

// ─── 单个蓝图生成 ─────────────────────────────────────────────────────────────

static UWidgetBlueprint* CreateWidgetBP(
    const FString& PackagePath, const FString& AssetName, UClass* ParentClass)
{
    const FString FullPath = PackagePath + TEXT("/") + AssetName;

    // 直接获取或创建包（与材质生成逻辑相同，不删除旧资产）
    UPackage* Package = CreatePackage(*FullPath);
    if (!Package)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create package: %s"), *FullPath);
        return nullptr;
    }

    // 如果已有同名对象，先将其 Rename 避免工厂创建时 check 失败
    if (UObject* Existing = StaticFindObjectFast(nullptr, Package, *AssetName))
    {
        Existing->Rename(
            *FString::Printf(TEXT("%s_OLD"), *AssetName),
            GetTransientPackage(),
            REN_DontCreateRedirectors | REN_ForceNoResetLoaders | REN_NonTransactional
        );
    }

    UWidgetBlueprintFactory* Factory = NewObject<UWidgetBlueprintFactory>();
    Factory->ParentClass = ParentClass;

    UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(
        Factory->FactoryCreateNew(UWidgetBlueprint::StaticClass(),
                                  Package, *AssetName, RF_Public | RF_Standalone, nullptr, GWarn));

    if (!WBP)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create Widget Blueprint: %s"), *FullPath);
        return nullptr;
    }

    return WBP;
}

static void FinalizeWidgetBP(UWidgetBlueprint* WBP)
{
    if (!WBP) return;

    // 编译蓝图（仅更新 WidgetTree，无需完整 Kismet 图编译）
    FKismetEditorUtilities::CompileBlueprint(WBP,
                                             EBlueprintCompileOptions::SkipGarbageCollection |
                                             EBlueprintCompileOptions::BatchCompile);

    WBP->PostEditChange();
    auto _ = WBP->MarkPackageDirty();
    FAssetRegistryModule::AssetCreated(WBP);
    UE_LOG(LogTemp, Log, TEXT("Widget BP generated: %s"), *WBP->GetPathName());
}

// ─── 共同区段：标题栏 + 分割线 ──────────────────────────────────────────────

static void BuildCommonHeader(FWidgetBuilder& B, const FString& Title, float CardW)
{
    // 标题文字
    B.Text(FName("TextBlock_Title"), Title,
           16.f, 13.f, CardW - 58.f, 26.f,
           WidgetColors::TextTitle, 15, /*bBold*/ true);

    // 关闭按钮
    B.CloseButton(CardW);

    // 分割线
    B.Rect(FName("Border_Sep"), 0.f, 47.f, CardW, 1.f, WidgetColors::Divider);
}

// 灰色小标签 + 白色值 两行结构
static void BuildLabelValue(FWidgetBuilder& B, FName LabelName, FName ValueName,
                            const FString& Label, const FString& DefaultValue,
                            float X, float Y, float W, float LH = 18.f, float VH = 22.f)
{
    B.Text(LabelName, Label, X, Y, W, LH, WidgetColors::TextLabel, 11);
    B.Text(ValueName, DefaultValue, X, Y + LH + 2.f, W, VH, WidgetColors::TextValue, 14);
}

// ─────────────────────────────────────────────────────────────────────────────
//  BP_Miner
// ─────────────────────────────────────────────────────────────────────────────

static void BuildMinerLayout(UWidgetBlueprint* WBP)
{
    constexpr float CW = 440.f, CH = 278.f;

    FWidgetBuilder B;
    B.Tree = WBP->WidgetTree;
    B.Root = B.Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("CanvasPanel_0"));
    B.Tree->RootWidget = B.Root;
    B.OX = -CW * 0.5f;
    B.OY = -CH * 0.5f;

    // ── 全屏半透明背景
    {
        UBorder* Overlay = B.Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Border_Overlay"));
        FSlateBrush Br;
        Br.TintColor = FSlateColor(WidgetColors::Overlay);
        Br.DrawAs = ESlateBrushDrawType::Box;
        Overlay->SetBrush(Br);
        UCanvasPanelSlot* S = B.Root->AddChildToCanvas(Overlay);
        S->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
        S->SetOffsets(FMargin(0.f));
    }

    // ── 卡片背景
    B.Rect(FName("Border_Card"), 0.f, 0.f, CW, CH, WidgetColors::CardBg);

    // ── 标题栏 + 分割线
    BuildCommonHeader(B, TEXT("矿机"), CW);

    // ── 内容（X=20 左边距，Y 从 60 开始）
    constexpr float IX = 20.f, IW = CW - 40.f;

    BuildLabelValue(B,
                    FName("Label_ItemType"), FName("TextBlock_ItemType"),
                    TEXT("资源类型"), TEXT("—"),
                    IX, 60.f, IW);

    BuildLabelValue(B,
                    FName("Label_Inventory"), FName("TextBlock_Inventory"),
                    TEXT("缓存库存"), TEXT("0 / 50"),
                    IX, 108.f, IW / 2.f);

    B.Text(FName("TextBlock_Interval"), TEXT("每 2 秒产出 1 个"),
           IX + IW / 2.f, 108.f + 18.f + 2.f, IW / 2.f, 22.f,
           WidgetColors::TextLabel, 12);

    // 进度条区段
    B.Text(FName("Label_Progress"), TEXT("生产进度"),
           IX, 188.f, IW, 18.f, WidgetColors::TextLabel, 11);
    B.Bar(FName("ProgressBar_Production"),
          IX, 210.f, IW, 20.f, WidgetColors::FillMiner);
}

// ─────────────────────────────────────────────────────────────────────────────
//  BP_Maker (Storage 仓库)
// ─────────────────────────────────────────────────────────────────────────────

static void BuildStorageLayout(UWidgetBlueprint* WBP)
{
    constexpr float CW = 440.f, CH = 248.f;

    FWidgetBuilder B;
    B.Tree = WBP->WidgetTree;
    B.Root = B.Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("CanvasPanel_0"));
    B.Tree->RootWidget = B.Root;
    B.OX = -CW * 0.5f;
    B.OY = -CH * 0.5f;

    // 背景
    {
        UBorder* Overlay = B.Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Border_Overlay"));
        FSlateBrush Br;
        Br.TintColor = FSlateColor(WidgetColors::Overlay);
        Br.DrawAs = ESlateBrushDrawType::Box;
        Overlay->SetBrush(Br);
        UCanvasPanelSlot* S = B.Root->AddChildToCanvas(Overlay);
        S->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
        S->SetOffsets(FMargin(0.f));
    }

    B.Rect(FName("Border_Card"), 0.f, 0.f, CW, CH, WidgetColors::CardBg);
    BuildCommonHeader(B, TEXT("仓库"), CW);

    constexpr float IX = 20.f, IW = CW - 40.f;

    BuildLabelValue(B,
                    FName("Label_ItemType"), FName("TextBlock_ItemType"),
                    TEXT("存储物品"), TEXT("—"),
                    IX, 60.f, IW);

    BuildLabelValue(B,
                    FName("Label_Inventory"), FName("TextBlock_Inventory"),
                    TEXT("库存数量"), TEXT("0 / 50"),
                    IX, 108.f, IW);

    B.Text(FName("Label_Fill"), TEXT("占用率"),
           IX, 160.f, IW, 18.f, WidgetColors::TextLabel, 11);
    B.Bar(FName("ProgressBar_Fill"),
          IX, 182.f, IW, 20.f, WidgetColors::FillStorage);
}

// ─────────────────────────────────────────────────────────────────────────────
//  BP_Assembler (Assembler 合成台)
// ─────────────────────────────────────────────────────────────────────────────

static void BuildAssemblerLayout(UWidgetBlueprint* WBP)
{
    constexpr float CW = 440.f, CH = 468.f;

    FWidgetBuilder B;
    B.Tree = WBP->WidgetTree;
    B.Root = B.Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("CanvasPanel_0"));
    B.Tree->RootWidget = B.Root;
    B.OX = -CW * 0.5f;
    B.OY = -CH * 0.5f;

    {
        UBorder* Overlay = B.Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Border_Overlay"));
        FSlateBrush Br;
        Br.TintColor = FSlateColor(WidgetColors::Overlay);
        Br.DrawAs = ESlateBrushDrawType::Box;
        Overlay->SetBrush(Br);
        UCanvasPanelSlot* S = B.Root->AddChildToCanvas(Overlay);
        S->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
        S->SetOffsets(FMargin(0.f));
    }

    B.Rect(FName("Border_Card"), 0.f, 0.f, CW, CH, WidgetColors::CardBg);
    BuildCommonHeader(B, TEXT("合成台"), CW);

    constexpr float IX = 20.f, IW = CW - 40.f;

    // 配方 + 进度
    BuildLabelValue(B,
                    FName("Label_Recipe"), FName("TextBlock_RecipeType"),
                    TEXT("当前配方"), TEXT("—"),
                    IX, 60.f, IW * 0.6f);

    B.Text(FName("Label_Speed"), TEXT("速度倍率"),
           IX + IW * 0.6f, 60.f, IW * 0.4f, 18.f, WidgetColors::TextLabel, 11);
    B.Text(FName("TextBlock_Speed"), TEXT("×1.0"),
           IX + IW * 0.6f, 80.f, IW * 0.4f, 22.f, WidgetColors::TextValue, 14);

    B.Text(FName("Label_Crafting"), TEXT("合成进度"),
           IX, 116.f, IW, 18.f, WidgetColors::TextLabel, 11);
    B.Bar(FName("ProgressBar_Crafting"),
          IX, 138.f, IW, 20.f, WidgetColors::FillAssembler);

    // ── 输入区 ─────────────────────────────────────────────────────────────
    B.Rect(FName("Border_InputSep"), 0.f, 172.f, CW, 1.f, WidgetColors::Divider);
    B.Text(FName("Label_Input"), TEXT("输入材料"),
           IX, 180.f, IW, 18.f, WidgetColors::TextLabel, 11, true);

    const FName InputNames[4] = {
        FName("TextBlock_Input_0"), FName("TextBlock_Input_1"),
        FName("TextBlock_Input_2"), FName("TextBlock_Input_3")
    };
    for (int32 i = 0; i < 4; ++i)
    {
        const float Col = (i % 2) * (IW * 0.5f);
        const float Row = (i / 2) * 40.f;
        B.Text(InputNames[i], TEXT("—"), IX + Col, 204.f + Row, IW * 0.5f - 8.f, 30.f,
               WidgetColors::TextValue, 12);
    }

    // ── 输出区 ─────────────────────────────────────────────────────────────
    B.Rect(FName("Border_OutputSep"), 0.f, 288.f, CW, 1.f, WidgetColors::Divider);
    B.Text(FName("Label_Output"), TEXT("输出产物"),
           IX, 296.f, IW, 18.f, WidgetColors::TextLabel, 11, true);

    const FName OutputNames[4] = {
        FName("TextBlock_Output_0"), FName("TextBlock_Output_1"),
        FName("TextBlock_Output_2"), FName("TextBlock_Output_3")
    };
    for (int32 i = 0; i < 4; ++i)
    {
        const float Col = (i % 2) * (IW * 0.5f);
        const float Row = (i / 2) * 40.f;
        B.Text(OutputNames[i], TEXT("—"), IX + Col, 320.f + Row, IW * 0.5f - 8.f, 30.f,
               WidgetColors::TextValue, 12);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  BP_LogisticsTower（物流塔）
// ─────────────────────────────────────────────────────────────────────────────

static void BuildLogisticsTowerLayout(UWidgetBlueprint* WBP)
{
    constexpr float CW = 440.f, CH = 396.f;

    FWidgetBuilder B;
    B.Tree = WBP->WidgetTree;
    B.Root = B.Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("CanvasPanel_0"));
    B.Tree->RootWidget = B.Root;
    B.OX = -CW * 0.5f;
    B.OY = -CH * 0.5f;

    // ── 全屏半透明背景 ──────────────────────────────────────────────────────
    {
        UBorder* Overlay = B.Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Border_Overlay"));
        FSlateBrush Br;
        Br.TintColor = FSlateColor(WidgetColors::Overlay);
        Br.DrawAs = ESlateBrushDrawType::Box;
        Overlay->SetBrush(Br);
        UCanvasPanelSlot* S = B.Root->AddChildToCanvas(Overlay);
        S->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
        S->SetOffsets(FMargin(0.f));
    }

    // ── 卡片背景 ────────────────────────────────────────────────────────────
    B.Rect(FName("Border_Card"), 0.f, 0.f, CW, CH, WidgetColors::CardBg);

    // ── 标题栏 + 分割线 ─────────────────────────────────────────────────────
    BuildCommonHeader(B, TEXT("物流塔"), CW);

    constexpr float IX = 20.f, IW = CW - 40.f;

    // ── 物品类型 ─────────────────────────────────────────────────────────────
    BuildLabelValue(B,
                    FName("Label_ItemType"), FName("TextBlock_ItemType"),
                    TEXT("物品类型"), TEXT("—"),
                    IX, 60.f, IW);

    // ── 库存数量 ─────────────────────────────────────────────────────────────
    BuildLabelValue(B,
                    FName("Label_Inventory"), FName("TextBlock_Inventory"),
                    TEXT("库存数量"), TEXT("0 / 50"),
                    IX, 108.f, IW);

    // ── 库存进度条 ───────────────────────────────────────────────────────────
    B.Text(FName("Label_Fill"), TEXT("库存占用"),
           IX, 160.f, IW, 18.f, WidgetColors::TextLabel, 11);
    B.Bar(FName("ProgressBar_Storage"),
          IX, 182.f, IW, 20.f, WidgetColors::FillLogistics);

    // ── 分割线 ───────────────────────────────────────────────────────────────
    B.Rect(FName("Border_InfoSep"), 0.f, 216.f, CW, 1.f, WidgetColors::Divider);

    // ── 运行模式 ───────────────────────────────────────────────────────────
    BuildLabelValue(B,
                    FName("Label_Mode"), FName("TextBlock_Mode"),
                    TEXT("运行模式"), TEXT("仓储"),
                    IX, 224.f, IW * 0.5f);

    // ── 请求阈值 + 单架运量 ─────────────────────────────────────────────
    BuildLabelValue(B,
                    FName("Label_Threshold"), FName("TextBlock_Threshold"),
                    TEXT("请求阈值"), TEXT("—"),
                    IX, 272.f, IW * 0.5f);

    BuildLabelValue(B,
                    FName("Label_DroneCount"), FName("TextBlock_DroneCount"),
                    TEXT("单次运量"), TEXT("—"),
                    IX + IW * 0.5f, 272.f, IW * 0.5f);

    // ── 归属无人机状态 + 来航数 ─────────────────────────────────
    BuildLabelValue(B,
                    FName("Label_OwnedDrones"), FName("TextBlock_OwnedDrones"),
                    TEXT("归属无人机"), TEXT("—"),
                    IX, 320.f, IW * 0.5f);

    BuildLabelValue(B,
                    FName("Label_IncomingDrones"), FName("TextBlock_IncomingDrones"),
                    TEXT("来航"), TEXT("—"),
                    IX + IW * 0.5f, 320.f, IW * 0.5f);
}

// ─────────────────────────────────────────────────────────────────────────────
//  公共入口
// ─────────────────────────────────────────────────────────────────────────────

void UMaterialGeneratorUtils::CreateBuildingWidgets()
{
    static const FString UIRoot = TEXT("/Game/Assets/UI");

    // 在全部创建前强制清理一次
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

    struct FEntry
    {
        FString Name;
        UClass* Parent;
        void (*Build)(UWidgetBlueprint*);
    };
    const FEntry Entries[] = {
        {TEXT("BP_Miner"), UMassDspMinerWidget::StaticClass(), &BuildMinerLayout},
        {TEXT("BP_Maker"), UMassDspAssemblerWidget::StaticClass(), &BuildAssemblerLayout},
        {TEXT("BP_Storage"), UMassDspStorageWidget::StaticClass(), &BuildStorageLayout},
        {TEXT("BP_LogisticsTower"), UMassDspLogisticsTowerWidget::StaticClass(), &BuildLogisticsTowerLayout},
    };

    for (const FEntry& E : Entries)
    {
        UWidgetBlueprint* WBP = CreateWidgetBP(UIRoot, E.Name, E.Parent);
        if (!WBP)
        {
            UE_LOG(LogTemp, Warning, TEXT("Failed to create: %s"), *E.Name);
            continue;
        }
        E.Build(WBP);
        FinalizeWidgetBP(WBP);
    }
}

#endif
