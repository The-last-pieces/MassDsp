#pragma once

#include "CoreMinimal.h"
#include "MassEntityTypes.h"
#include "MassDspStorageFragment.generated.h"

/**
 * 仓库特定的Fragment
 * 包含仓库的存储相关数据
 */
USTRUCT()
struct MASSDSP_API FMassDspStorageFragment : public FMassFragment
{
	GENERATED_BODY()

	// 当前存储数量
	UPROPERTY()
	int32 InventoryCount = 0;

	// 存储容量
	UPROPERTY()
	int32 MaxInventory = 50;
};
