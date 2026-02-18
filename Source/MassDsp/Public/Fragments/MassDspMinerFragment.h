#pragma once

#include "CoreMinimal.h"
#include "MassEntityTypes.h"
#include "MassDspMinerFragment.generated.h"

/**
 * 矿机特定的Fragment
 * 包含矿机的生产逻辑相关数据
 * 注意：InventoryCount和MaxInventory在FMassDspBuildingFragment中，这里只包含矿机特有的字段
 */
USTRUCT()
struct MASSDSP_API FMassDspMinerFragment : public FMassFragment
{
	GENERATED_BODY()

	// 当前生产进度 (0.0 - 1.0)
	UPROPERTY()
	float ProductionProgress = 0.0f;

	// 生产间隔（秒）
	UPROPERTY()
	float ProductionInterval = 2.0f;
};
