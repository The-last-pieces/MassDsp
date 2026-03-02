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

    /**
     * 程序化生成三种建筑交互 UMG Widget 蓝图资源：
     *   /Game/Assets/UI/BP_Miner    (UMassDspMinerWidget)
     *   /Game/Assets/UI/BP_Storage  (UMassDspStorageWidget)
     *   /Game/Assets/UI/BP_Assembler(UMassDspAssemblerWidget)
     * 资源已存在时跳过，删除资源后重新运行可重新生成。
     */
    static void CreateBuildingWidgets();
};
