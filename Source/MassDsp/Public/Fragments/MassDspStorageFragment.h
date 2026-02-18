#pragma once

#include "CoreMinimal.h"
#include "MassEntityTypes.h"
#include "MassDspStorageFragment.generated.h"

/**
 * 仓库特定的Fragment
 * 包含仓库的存储相关数据
 * 注意：InventoryCount和MaxInventory在FMassDspBuildingFragment中
 * 这个Fragment主要作为标记，表明这是一个仓库实体
 */
USTRUCT()
struct MASSDSP_API FMassDspStorageFragment : public FMassFragment
{
	GENERATED_BODY()

	// 存储容量（初始化值，实际使用BuildingFragment中的MaxInventory）
	UPROPERTY()
	int32 Capacity = 50;
};
