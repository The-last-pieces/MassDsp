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
#include "Materials/MaterialExpressionPower.h"

#endif

// TODO 先把抗锯齿从TSR改成TAA临时处理下材质残影问题,后续考虑在材质中设置像素速度解决
void UMaterialGeneratorUtils::CreateConveyorMaterial()
{
#if WITH_EDITOR
    const FString AssetName = TEXT("M_Belt");
    const FString PackageName = TEXT("/Game/Assets/") + AssetName;
    const FString SourceFilePath = FString(TEXT(__FILE__));
    const FString HashPrefix = TEXT("[SOURCE_HASH]:"); // 识别前缀

    // --- 1. 源码变更检测 (MD5) ---
    FString FileContent;
    FString CurrentHash;

    if (FFileHelper::LoadFileToString(FileContent, *SourceFilePath))
    {
        // 计算 MD5 并加上前缀
        CurrentHash = HashPrefix + FMD5::HashAnsiString(*FileContent);
    }
    else
    {
        CurrentHash = HashPrefix + FDateTime::Now().ToString();
    }

    // 尝试加载现有材质进行检测
    if (UMaterial* ExistingMaterial = LoadObject<UMaterial>(nullptr, *PackageName))
    {
        // 遍历所有表达式寻找存储哈希的注释节点
        for (UMaterialExpression* Expr : ExistingMaterial->GetExpressions())
        {
            if (UMaterialExpressionComment* CommentNode = Cast<UMaterialExpressionComment>(Expr))
            {
                // 如果找到了哈希注释，且内容一致
                if (CommentNode->Text.Equals(CurrentHash))
                {
                    // 源码未变，跳过生成
                    // UE_LOG(LogTemp, Log, TEXT("Material is up to date."));
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

    // 辅助Lambda
    auto CreateNode = [&](const UClass* Class, int32 X, int32 Y) -> UMaterialExpression*
    {
        UMaterialExpression* Node = NewObject<UMaterialExpression>(Material, Class);
        Material->GetExpressionCollection().AddExpression(Node);
        Node->MaterialExpressionEditorX = X;
        Node->MaterialExpressionEditorY = Y;
        return Node;
    };

    // --- 3. 存储哈希值 (关键步骤) ---
    // 创建一个注释节点来存储哈希
    auto* HashComment = Cast<UMaterialExpressionComment>(CreateNode(UMaterialExpressionComment::StaticClass(), -1200, -400));
    HashComment->Text = CurrentHash;
    HashComment->CommentColor = FLinearColor::Black; // 黑色注释框
    HashComment->SizeX = 400;
    HashComment->SizeY = 100;

    // --- 4. 参数定义 ---
    auto* BaseColor = Cast<UMaterialExpressionVectorParameter>(CreateNode(UMaterialExpressionVectorParameter::StaticClass(), -600, -200));
    BaseColor->ParameterName = "BaseColor";
    BaseColor->DefaultValue = FLinearColor(0.05f, 0.05f, 0.05f, 1.0f);

    auto* LineColor = Cast<UMaterialExpressionVectorParameter>(CreateNode(UMaterialExpressionVectorParameter::StaticClass(), -600, 0));
    LineColor->ParameterName = "LineColor";
    LineColor->DefaultValue = FLinearColor(1.0f, 0.8f, 0.0f, 1.0f);

    auto* Speed = Cast<UMaterialExpressionScalarParameter>(CreateNode(UMaterialExpressionScalarParameter::StaticClass(), -1000, 200));
    Speed->ParameterName = "Speed";
    Speed->DefaultValue = 1.0f;

    auto* Tiling = Cast<UMaterialExpressionScalarParameter>(CreateNode(UMaterialExpressionScalarParameter::StaticClass(), -1000, 400));
    Tiling->ParameterName = "Tiling";
    Tiling->DefaultValue = 5.0f;

    auto* LineWidth = Cast<UMaterialExpressionScalarParameter>(CreateNode(UMaterialExpressionScalarParameter::StaticClass(), -600, 600));
    LineWidth->ParameterName = "LineWidth";
    LineWidth->DefaultValue = 0.15f;

    // --- 5. 逻辑构建 ---
    // 设计：两侧边缘亮条（纯U方向，零V频率，完美mip）+ 中心低频扫光（软脉冲）
    // 彻底规避高频虚线带来的远距离模糊问题

    // 常量
    auto* ConstZero = Cast<UMaterialExpressionConstant>(CreateNode(UMaterialExpressionConstant::StaticClass(), -1400, 500));
    ConstZero->R = 0.0f;
    auto* ConstOne = Cast<UMaterialExpressionConstant>(CreateNode(UMaterialExpressionConstant::StaticClass(), -1400, 560));
    ConstOne->R = 1.0f;
    auto* ConstUEpsilon = Cast<UMaterialExpressionConstant>(CreateNode(UMaterialExpressionConstant::StaticClass(), -1400, 620));
    ConstUEpsilon->R = 0.0001f; // 防止除零
    auto* ConstGlowPow = Cast<UMaterialExpressionConstant>(CreateNode(UMaterialExpressionConstant::StaticClass(), -1400, 680));
    ConstGlowPow->R = 4.0f; // 扫光尖锐程度，间距由 GlowThreshold 控制
    auto* ConstGlowThreshold = Cast<UMaterialExpressionConstant>(CreateNode(UMaterialExpressionConstant::StaticClass(), -1400, 740));
    ConstGlowThreshold->R = 0.78f; // 截断阈值：>0 暗区比例 = Threshold，0.78=78%周期为暗区
    auto* ConstGlowThresholdInv = Cast<UMaterialExpressionConstant>(CreateNode(UMaterialExpressionConstant::StaticClass(), -1400, 800));
    ConstGlowThresholdInv->R = 1.0f / (1.0f - 0.78f); // = 1/(1-Threshold)，将峰顶归一化到[0,1]
    auto* ConstGlowAmt = Cast<UMaterialExpressionConstant>(CreateNode(UMaterialExpressionConstant::StaticClass(), -1400, 860));
    ConstGlowAmt->R = 0.55f; // 扫光最大亮度

    // UV
    auto* TexCoord = Cast<UMaterialExpressionTextureCoordinate>(CreateNode(UMaterialExpressionTextureCoordinate::StaticClass(), -1200, 300));
    auto* Time = Cast<UMaterialExpressionTime>(CreateNode(UMaterialExpressionTime::StaticClass(), -1200, 100));

    // 提取 U、V
    auto* MaskU = Cast<UMaterialExpressionComponentMask>(CreateNode(UMaterialExpressionComponentMask::StaticClass(), -1000, 300));
    MaskU->Input.Expression = TexCoord;
    MaskU->R = 1;
    MaskU->G = 0;

    auto* MaskV = Cast<UMaterialExpressionComponentMask>(CreateNode(UMaterialExpressionComponentMask::StaticClass(), -1000, 400));
    MaskV->Input.Expression = TexCoord;
    MaskV->R = 0;
    MaskV->G = 1;

    // =============================================
    // A. 边缘条纹 (Edge Stripes)
    // 用 U 坐标的屏幕空间导数计算"1 像素 = 多少 UV"
    // 边缘过渡宽度始终等于 1 像素，任何距离都无锯齿无发虚
    // =============================================

    auto* DDX_U = Cast<UMaterialExpressionDDX>(CreateNode(UMaterialExpressionDDX::StaticClass(), -800, 150));
    DDX_U->Value.Expression = MaskU;

    auto* DDY_U = Cast<UMaterialExpressionDDY>(CreateNode(UMaterialExpressionDDY::StaticClass(), -800, 220));
    DDY_U->Value.Expression = MaskU;

    auto* AbsDDX_U = Cast<UMaterialExpressionAbs>(CreateNode(UMaterialExpressionAbs::StaticClass(), -650, 150));
    AbsDDX_U->Input.Expression = DDX_U;

    auto* AbsDDY_U = Cast<UMaterialExpressionAbs>(CreateNode(UMaterialExpressionAbs::StaticClass(), -650, 220));
    AbsDDY_U->Input.Expression = DDY_U;

    // 1像素在UV空间的跨度 = max(|ddx(U)|, |ddy(U)|)
    auto* MaxDeriv_U = Cast<UMaterialExpressionMax>(CreateNode(UMaterialExpressionMax::StaticClass(), -500, 185));
    MaxDeriv_U->A.Expression = AbsDDX_U;
    MaxDeriv_U->B.Expression = AbsDDY_U;

    // 防止除零
    auto* SafeDeriv = Cast<UMaterialExpressionMax>(CreateNode(UMaterialExpressionMax::StaticClass(), -350, 185));
    SafeDeriv->A.Expression = MaxDeriv_U;
    SafeDeriv->B.Expression = ConstUEpsilon;

    // 左边缘: saturate((LineWidth - U) / SafeDeriv)
    auto* LeftRaw = Cast<UMaterialExpressionSubtract>(CreateNode(UMaterialExpressionSubtract::StaticClass(), -800, 260));
    LeftRaw->A.Expression = LineWidth;
    LeftRaw->B.Expression = MaskU;

    auto* LeftNorm = Cast<UMaterialExpressionDivide>(CreateNode(UMaterialExpressionDivide::StaticClass(), -600, 260));
    LeftNorm->A.Expression = LeftRaw;
    LeftNorm->B.Expression = SafeDeriv;

    auto* LeftEdge = Cast<UMaterialExpressionClamp>(CreateNode(UMaterialExpressionClamp::StaticClass(), -400, 260));
    LeftEdge->Input.Expression = LeftNorm;
    LeftEdge->MinDefault = 0.0f;
    LeftEdge->MaxDefault = 1.0f;

    // 右边缘: saturate((LineWidth - (1-U)) / SafeDeriv)
    auto* OneMinusU = Cast<UMaterialExpressionSubtract>(CreateNode(UMaterialExpressionSubtract::StaticClass(), -800, 340));
    OneMinusU->A.Expression = ConstOne;
    OneMinusU->B.Expression = MaskU;

    auto* RightRaw = Cast<UMaterialExpressionSubtract>(CreateNode(UMaterialExpressionSubtract::StaticClass(), -600, 340));
    RightRaw->A.Expression = LineWidth;
    RightRaw->B.Expression = OneMinusU;

    auto* RightNorm = Cast<UMaterialExpressionDivide>(CreateNode(UMaterialExpressionDivide::StaticClass(), -400, 340));
    RightNorm->A.Expression = RightRaw;
    RightNorm->B.Expression = SafeDeriv;

    auto* RightEdge = Cast<UMaterialExpressionClamp>(CreateNode(UMaterialExpressionClamp::StaticClass(), -200, 340));
    RightEdge->Input.Expression = RightNorm;
    RightEdge->MinDefault = 0.0f;
    RightEdge->MaxDefault = 1.0f;

    // 合并两侧边缘
    auto* EdgeMask = Cast<UMaterialExpressionMax>(CreateNode(UMaterialExpressionMax::StaticClass(), 0, 300));
    EdgeMask->A.Expression = LeftEdge;
    EdgeMask->B.Expression = RightEdge;

    // =============================================
    // B. 中心滚动扫光 (Center Glow) — sin^n 平滑脉冲，无不连续点
    // =============================================

    auto* TimeSpeed = Cast<UMaterialExpressionMultiply>(CreateNode(UMaterialExpressionMultiply::StaticClass(), -1000, 150));
    TimeSpeed->A.Expression = Time;
    TimeSpeed->B.Expression = Speed;

    auto* VTiling = Cast<UMaterialExpressionMultiply>(CreateNode(UMaterialExpressionMultiply::StaticClass(), -800, 500));
    VTiling->A.Expression = MaskV;
    VTiling->B.Expression = Tiling;

    auto* MoveSub = Cast<UMaterialExpressionSubtract>(CreateNode(UMaterialExpressionSubtract::StaticClass(), -600, 480));
    MoveSub->A.Expression = VTiling;
    MoveSub->B.Expression = TimeSpeed;

    // sin(phase) → [-1, 1]，天然连续无跳变
    auto* SineGlow = Cast<UMaterialExpressionSine>(CreateNode(UMaterialExpressionSine::StaticClass(), -400, 480));
    SineGlow->Input.Expression = MoveSub;

    // * 0.5 + 0.5 → [0, 1]
    auto* SinePos = Cast<UMaterialExpressionMultiply>(CreateNode(UMaterialExpressionMultiply::StaticClass(), -200, 480));
    SinePos->A.Expression = SineGlow;
    SinePos->ConstB = 0.5f;

    auto* SineRemap = Cast<UMaterialExpressionAdd>(CreateNode(UMaterialExpressionAdd::StaticClass(), 0, 480));
    SineRemap->A.Expression = SinePos;
    SineRemap->ConstB = 0.5f;

    // sin^GlowPow → 收窄为尖锐光带，GlowPow 越大越细
    // 先阈值截断：(SineRemap - Threshold) / (1 - Threshold)，将峰顶归一化并切零底部
    // 这样 Threshold 比例的周期就是完全的暗区，实现独立控制间距
    auto* ThreshSub = Cast<UMaterialExpressionSubtract>(CreateNode(UMaterialExpressionSubtract::StaticClass(), 50, 480));
    ThreshSub->A.Expression = SineRemap;
    ThreshSub->B.Expression = ConstGlowThreshold;

    auto* ThreshNorm = Cast<UMaterialExpressionMultiply>(CreateNode(UMaterialExpressionMultiply::StaticClass(), 200, 480));
    ThreshNorm->A.Expression = ThreshSub;
    ThreshNorm->B.Expression = ConstGlowThresholdInv;

    auto* ThreshClamped = Cast<UMaterialExpressionClamp>(CreateNode(UMaterialExpressionClamp::StaticClass(), 350, 480));
    ThreshClamped->Input.Expression = ThreshNorm;
    ThreshClamped->MinDefault = 0.0f;
    ThreshClamped->MaxDefault = 1.0f;

    auto* CenterGlowRaw = Cast<UMaterialExpressionPower>(CreateNode(UMaterialExpressionPower::StaticClass(), 500, 480));
    CenterGlowRaw->Base.Expression = ThreshClamped;
    CenterGlowRaw->Exponent.Expression = ConstGlowPow;

    // 限制扫光亮度
    auto* CenterGlow = Cast<UMaterialExpressionMultiply>(CreateNode(UMaterialExpressionMultiply::StaticClass(), 400, 480));
    CenterGlow->A.Expression = CenterGlowRaw;
    CenterGlow->B.Expression = ConstGlowAmt;

    // =============================================
    // C. 合并：边缘线优先，扫光叠加在底部
    // =============================================
    auto* FinalMask = Cast<UMaterialExpressionMax>(CreateNode(UMaterialExpressionMax::StaticClass(), 600, 380));
    FinalMask->A.Expression = EdgeMask;
    FinalMask->B.Expression = CenterGlow;

    auto* FinalColor = Cast<UMaterialExpressionLinearInterpolate>(CreateNode(UMaterialExpressionLinearInterpolate::StaticClass(), 1200, 200));
    FinalColor->A.Expression = BaseColor;
    FinalColor->B.Expression = LineColor;
    FinalColor->Alpha.Expression = FinalMask;

    // --- 6. 输出与保存 ---
    Material->SetShadingModel(MSM_DefaultLit);
    Material->GetEditorOnlyData()->BaseColor.Expression = FinalColor;
    Material->TwoSided = false;

    // 微型动态 WorldPositionOffset：sin(Time)*0.0001 单位（肉眼完全不可见）
    // 静态 Mesh 默认 MotionVector=0，TAA 认为像素不动并大量积累历史帧导致残影
    // WPO 动画后光栅化器每帧重算 MotionVector，TAA 可正确跟踪动画，残影彻底消除
    auto* WpoSine = Cast<UMaterialExpressionSine>(CreateNode(UMaterialExpressionSine::StaticClass(), 1200, 450));
    WpoSine->Input.Expression = Time;

    auto* WpoScale = Cast<UMaterialExpressionMultiply>(CreateNode(UMaterialExpressionMultiply::StaticClass(), 1380, 450));
    WpoScale->A.Expression = WpoSine;
    WpoScale->ConstB = 0.1f; // 1mm，肉眼完全不可见，但足够让 float16 velocity buffer 精确记录非零 MotionVector

    Material->GetEditorOnlyData()->WorldPositionOffset.Expression = WpoScale;

    Material->PostEditChange();
    auto _ = Material->MarkPackageDirty();
    FAssetRegistryModule::AssetCreated(Material);

    UE_LOG(LogTemp, Log, TEXT("Conveyor Material Updated. Hash: %s"), *CurrentHash);
#endif
}
