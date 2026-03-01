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

#endif

// 传送带材质：两侧白边 + 中间倒V形（∧）箭头动画
// 动画速度由 Speed 标量参数控制（UV/s），不再依赖顶点色
void UMaterialGeneratorUtils::CreateConveyorMaterial()
{
#if WITH_EDITOR
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
#endif
}
