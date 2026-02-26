#pragma once

#include "CoreMinimal.h"
#include "MaterialGeneratorUtils.generated.h"

UCLASS()
class MASSDSP_API UMaterialGeneratorUtils : public UObject
{
    GENERATED_BODY()

public:
    /** 
     * 自动生成传送带材质
     * 包含源码MD5检测，避免重复生成
     */
    static void CreateConveyorMaterial();
};
