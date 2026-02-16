// Fill out your copyright notice in the Description page of Project Settings.

#include "MassDspGameMode.h"
#include "Subsystems/MassDspManager.h"
#include "Actors/MassDspMiner.h"
#include "Actors/MassDspStorage.h"
#include "Fragments/MassDspBuildingFragment.h"
#include "ZoneGraphSubsystem.h"
#include "ZoneGraphData.h"
#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "Misc/CoreDelegates.h"

void AMassDspGameMode::BeginPlay()
{
	Super::BeginPlay();

	UWorld* World = GetWorld();
	UMassDspManager* DspManager = World->GetSubsystem<UMassDspManager>();
	if (!DspManager) return;

	// 设置 Manager 的 DefaultItemConfig，以便 Miner 可以生产物品
	if (BeltItemConfigAsset)
	{
		DspManager->DefaultItemConfig = BeltItemConfigAsset;
	}

	if (!MinerClass || !StorageClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("MinerClass or StorageClass not set in GameMode!"));
		return;
	}

	// 1. 创建并注册矿机
	FVector MinerLocation(0, 0, 0);
	AMassDspMiner* MinerActor = World->SpawnActor<AMassDspMiner>(MinerClass, MinerLocation, FRotator::ZeroRotator);
	FMassEntityHandle MinerEntity = DspManager->RegisterBuildingEntity(MinerActor);

	// 2. 创建并注册仓库 (在 X 轴正方向 2000 单位处)
	FVector StorageLocation(2000, 0, 0);
	AMassDspStorage* StorageActor = World->SpawnActor<AMassDspStorage>(StorageClass, StorageLocation, FRotator::ZeroRotator);
	FMassEntityHandle StorageEntity = DspManager->RegisterBuildingEntity(StorageActor);

	// 3. 连接逻辑
	if (MinerActor && StorageActor)
	{
		TArray<FTransform> MinerOutputs = MinerActor->GetSlotTransformsByType(EBuildingSlotType::Output);
		TArray<FTransform> StorageInputs = StorageActor->GetSlotTransformsByType(EBuildingSlotType::Input);

		if (MinerOutputs.Num() > 0 && StorageInputs.Num() > 0)
		{
			FVector StartPoint = MinerOutputs[0].GetLocation();
			FVector EndPoint = StorageInputs[0].GetLocation();

			TArray<FVector> BeltPoints;
			BeltPoints.Add(StartPoint);
			BeltPoints.Add(EndPoint);

			// 创建运行时传送带
			FZoneGraphDataHandle BeltHandle = DspManager->CreateRuntimeBelt(BeltPoints, ConveyorMesh);

			if (BeltHandle.IsValid())
			{
				FZoneGraphLaneHandle LaneHandle(0, BeltHandle);

				// 获取 Lane 长度用于计算输入槽连接点
				float LaneLength = 0.0f;
				UZoneGraphSubsystem* ZoneGraphSubsystem = World->GetSubsystem<UZoneGraphSubsystem>();
				if (ZoneGraphSubsystem)
				{
					const FZoneGraphStorage* ZoneStorage = ZoneGraphSubsystem->GetZoneGraphStorage(BeltHandle);
					if (ZoneStorage && ZoneStorage->Lanes.Num() > 0)
					{
						// LaneLength = ZoneStorage->Lanes[0].LaneLength;
						// TODO 获取CacheLane再拿长度,直接拿不到
						//auto CacheLane = ZoneGraphSubsystem->GetCac(LaneHandle);
					}
				}
				LaneLength = 2000;

				FMassEntityManager& EntityManager = World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();

				// 连接 Miner 的输出槽到传送带起点
				if (FMassDspBuildingSlotsFragment* MinerSlots = EntityManager.GetFragmentDataPtr<FMassDspBuildingSlotsFragment>(MinerEntity))
				{
					for (int32 i = 0; i < MinerSlots->SlotCount; ++i)
					{
						if (MinerSlots->Slots[i].Type == EBuildingSlotType::Output)
						{
							MinerSlots->Slots[i].ConnectedLaneHandle = LaneHandle;
							MinerSlots->Slots[i].LaneConnectionDistance = 0.0f;
							MinerSlots->Slots[i].bConnected = true;
							break;
						}
					}
				}

				// 连接 Storage 的输入槽到传送带终点
				if (FMassDspBuildingSlotsFragment* StorageSlots = EntityManager.GetFragmentDataPtr<FMassDspBuildingSlotsFragment>(StorageEntity))
				{
					for (int32 i = 0; i < StorageSlots->SlotCount; ++i)
					{
						if (StorageSlots->Slots[i].Type == EBuildingSlotType::Input)
						{
							StorageSlots->Slots[i].ConnectedLaneHandle = LaneHandle;
							StorageSlots->Slots[i].LaneConnectionDistance = LaneLength;
							StorageSlots->Slots[i].bConnected = true;
							break;
						}
					}
				}

				// 4. (测试用) 在传送带上生成一些初始物品
				if (BeltItemConfigAsset)
				{
					DspManager->SpawnItemsOnBelt(BeltHandle, BeltItemConfigAsset);
				}
			}
		}
	}
}
