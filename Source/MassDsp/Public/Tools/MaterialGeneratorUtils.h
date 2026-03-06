#pragma once

#if WITH_EDITOR

#include "CoreMinimal.h"
#include "MaterialGeneratorUtils.generated.h"

UCLASS()
class MASSDSP_API UMaterialGeneratorUtils : public UObject
{
    GENERATED_BODY()

public:
    static void CreateAllProceduralAssets();
    
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

    /**
     * 程序化生成无人机 WPO 材质 /Game/Assets/Materials/M_Drone。
     *
     * GPU 侧完全代替 CPU UpdateDroneISMInstance：
     *  · TotalFlightTime == 0 → Idle 螺旋盘旋（正弦振荡）
     *  · TotalFlightTime > 0  → 三次贝塞尔飞行 + 飞行切线旋转
     *
     * ISM CustomData 布局（NumCustomDataFloats = 16）：
     *  [0]     TimeAtDispatch    派遣时的游戏时间
     *  [1]     TotalFlightTime   0 = Idle 模式
     *  [2-4]   P0.xyz
     *  [5-7]   P1.xyz
     *  [8-10]  P2.xyz
     *  [11-13] HomeLocation.xyz
     *  [14]    IdlePhaseOffset   黄金角相位，使同塔无人机均匀散布
     *  [15]    padding
     *  P3      实例 WorldPosition（= ObjectPositionWS / GetObjectWorldPosition()）
     *
     * 仅 WITH_EDITOR 可用（编辑器内生成一次后保存为资源，运行时 StaticLoadObject 加载）。
     */
    static void CreateDroneMaterial();
};

#endif
