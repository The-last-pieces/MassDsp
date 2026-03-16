#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "MassDspSystemStatsRowData.generated.h"

UCLASS(BlueprintType)
class MASSDSP_API UMassDspSystemStatsRowData : public UObject
{
    GENERATED_BODY()

public:
    UPROPERTY(BlueprintReadOnly, Category = "MassDsp|Stats")
    FText Title;

    UPROPERTY(BlueprintReadOnly, Category = "MassDsp|Stats")
    FText Value;

    UPROPERTY(BlueprintReadOnly, Category = "MassDsp|Stats")
    FText Details;
};