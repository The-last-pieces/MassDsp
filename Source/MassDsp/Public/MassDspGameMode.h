// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MassEntityConfigAsset.h"
#include "MassDspGameMode.generated.h"

UCLASS()
class MASSDSP_API AMassDspGameMode : public AGameModeBase
{
	GENERATED_BODY()


protected:
	void BeginPlay() override;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DSP")
	TObjectPtr<UStaticMesh> ConveyorMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DSP")
	TObjectPtr<UMassEntityConfigAsset> BeltItemConfigAsset;
};

