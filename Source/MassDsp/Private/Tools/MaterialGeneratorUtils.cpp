#include "Tools/MaterialGeneratorUtils.h"

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
#include "Materials/MaterialExpressionIf.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionComment.h"

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

    Material->bEnableResponsiveAA = true;

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

    // 常量
    auto* ConstZero = Cast<UMaterialExpressionConstant>(CreateNode(UMaterialExpressionConstant::StaticClass(), -400, 400));
    ConstZero->R = 0.0f;
    auto* ConstOne = Cast<UMaterialExpressionConstant>(CreateNode(UMaterialExpressionConstant::StaticClass(), -400, 450));
    ConstOne->R = 1.0f;
    auto* ConstHalf = Cast<UMaterialExpressionConstant>(CreateNode(UMaterialExpressionConstant::StaticClass(), -1000, 700));
    ConstHalf->R = 0.5f;

    // UV & Time
    auto* TexCoord = Cast<UMaterialExpressionTextureCoordinate>(CreateNode(UMaterialExpressionTextureCoordinate::StaticClass(), -1200, 300));
    auto* Time = Cast<UMaterialExpressionTime>(CreateNode(UMaterialExpressionTime::StaticClass(), -1200, 100));

    // 逻辑A: 纵向滚动虚线
    auto* TimeSpeed = Cast<UMaterialExpressionMultiply>(CreateNode(UMaterialExpressionMultiply::StaticClass(), -1000, 150));
    TimeSpeed->A.Expression = Time;
    TimeSpeed->B.Expression = Speed;

    auto* MaskV = Cast<UMaterialExpressionComponentMask>(CreateNode(UMaterialExpressionComponentMask::StaticClass(), -1000, 300));
    MaskV->Input.Expression = TexCoord;
    MaskV->R = 0;
    MaskV->G = 1;

    auto* VTiling = Cast<UMaterialExpressionMultiply>(CreateNode(UMaterialExpressionMultiply::StaticClass(), -800, 350));
    VTiling->A.Expression = MaskV;
    VTiling->B.Expression = Tiling;

    auto* MoveSub = Cast<UMaterialExpressionSubtract>(CreateNode(UMaterialExpressionSubtract::StaticClass(), -600, 300));
    MoveSub->A.Expression = VTiling;
    MoveSub->B.Expression = TimeSpeed;

    auto* SineWave = Cast<UMaterialExpressionSine>(CreateNode(UMaterialExpressionSine::StaticClass(), -450, 300));
    SineWave->Input.Expression = MoveSub;

    auto* DashMask = Cast<UMaterialExpressionIf>(CreateNode(UMaterialExpressionIf::StaticClass(), -300, 300));
    DashMask->A.Expression = SineWave;
    DashMask->B.Expression = ConstZero;
    DashMask->AGreaterThanB.Expression = ConstOne;
    DashMask->AEqualsB.Expression = ConstZero;
    DashMask->ALessThanB.Expression = ConstZero;

    // 逻辑B: 横向中心遮罩
    auto* MaskU = Cast<UMaterialExpressionComponentMask>(CreateNode(UMaterialExpressionComponentMask::StaticClass(), -1000, 600));
    MaskU->Input.Expression = TexCoord;
    MaskU->R = 1;
    MaskU->G = 0;

    auto* CenterOffset = Cast<UMaterialExpressionSubtract>(CreateNode(UMaterialExpressionSubtract::StaticClass(), -800, 600));
    CenterOffset->A.Expression = MaskU;
    CenterOffset->B.Expression = ConstHalf;

    auto* AbsDist = Cast<UMaterialExpressionAbs>(CreateNode(UMaterialExpressionAbs::StaticClass(), -600, 600));
    AbsDist->Input.Expression = CenterOffset;

    auto* WidthMask = Cast<UMaterialExpressionIf>(CreateNode(UMaterialExpressionIf::StaticClass(), -300, 600));
    WidthMask->A.Expression = AbsDist;
    WidthMask->B.Expression = LineWidth;
    WidthMask->AGreaterThanB.Expression = ConstZero;
    WidthMask->AEqualsB.Expression = ConstOne;
    WidthMask->ALessThanB.Expression = ConstOne;

    // 逻辑C: 混合
    auto* FinalMask = Cast<UMaterialExpressionMultiply>(CreateNode(UMaterialExpressionMultiply::StaticClass(), -150, 450));
    FinalMask->A.Expression = DashMask;
    FinalMask->B.Expression = WidthMask;

    auto* FinalColor = Cast<UMaterialExpressionLinearInterpolate>(CreateNode(UMaterialExpressionLinearInterpolate::StaticClass(), 0, 0));
    FinalColor->A.Expression = BaseColor;
    FinalColor->B.Expression = LineColor;
    FinalColor->Alpha.Expression = FinalMask;

    // --- 6. 输出与保存 ---
    Material->GetEditorOnlyData()->BaseColor.Expression = FinalColor;
    Material->TwoSided = false;

    Material->PostEditChange();
    auto _ = Material->MarkPackageDirty();
    FAssetRegistryModule::AssetCreated(Material);

    UE_LOG(LogTemp, Log, TEXT("Conveyor Material Updated. Hash: %s"), *CurrentHash);
#endif
}
