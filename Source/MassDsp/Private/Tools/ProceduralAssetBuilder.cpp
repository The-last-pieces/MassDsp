#if WITH_EDITOR
#include "Tools/ProceduralAssetBuilder.h"

#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "Engine/Blueprint.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "UObject/SavePackage.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"

namespace
{
    FString BuildAssetObjectPath(const FString& AssetPath)
    {
        return AssetPath + TEXT(".") + FPackageName::GetLongPackageAssetName(AssetPath);
    }

    bool ReleaseExistingAssetObjects(const FString& AssetPath, const FString& AssetName)
    {
        UPackage* ExistingPackage = FindPackage(nullptr, *AssetPath);
        UObject* ExistingAsset = StaticFindObject(UObject::StaticClass(), nullptr, *BuildAssetObjectPath(AssetPath));
        if (!ExistingAsset)
        {
            return true;
        }

        if (ExistingPackage)
        {
            ExistingPackage->FullyLoad();
            ResetLoaders(ExistingPackage);
        }

        TArray<UObject*> ObjectsToRelocate;
        if (ExistingPackage)
        {
            ForEachObjectWithPackage(ExistingPackage, [&ObjectsToRelocate](UObject* Object)
            {
                if (Object && Object != GetTransientPackage() && !Object->IsPackageExternal())
                {
                    ObjectsToRelocate.Add(Object);
                }
                return true;
            }, true);
        }
        else
        {
            ObjectsToRelocate.Add(ExistingAsset);
        }

        for (UObject* Object : ObjectsToRelocate)
        {
            if (!Object || Object->IsPackageExternal())
            {
                continue;
            }

            Object->ClearFlags(RF_Public | RF_Standalone);
            const FName TempName = MakeUniqueObjectName(GetTransientPackage(), Object->GetClass(), *FString::Printf(TEXT("%s_Stale"), *Object->GetName()));
            Object->Rename(*TempName.ToString(), GetTransientPackage(), REN_DontCreateRedirectors | REN_ForceNoResetLoaders | REN_NonTransactional);
            Object->MarkAsGarbage();
        }

        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

        if (!ExistingPackage)
        {
            return true;
        }

        return FindObject<UBlueprint>(ExistingPackage, *AssetName) == nullptr
            && StaticFindObjectFast(UObject::StaticClass(), ExistingPackage, *AssetName) == nullptr;
    }

    UObject* ReloadGeneratedAsset(UPackage* Package, const FString& AssetPath)
    {
        if (!Package)
        {
            return nullptr;
        }

        ReloadPackage(Package, static_cast<uint32>(EPackageReloadPhase::OnPackageFixup));
        return StaticLoadObject(UObject::StaticClass(), nullptr, *BuildAssetObjectPath(AssetPath));
    }
}

UObject* FProceduralAssetBuilder::GenerateAsset(const FString& AssetPath, const FString& Version, FOnBuildAsset BuildFunc)
{
    const FString AssetName = FPackageName::GetLongPackageAssetName(AssetPath);
    const FString ObjectPath = BuildAssetObjectPath(AssetPath);

    TMap<FString, FString> CacheMap = LoadCache();
    const bool bCacheMatched = CacheMap.Contains(AssetPath) && CacheMap[AssetPath] == Version;
    UObject* ExistingAsset = StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPath);
    const bool bAssetExists = FPackageName::DoesPackageExist(AssetPath);
    if (bCacheMatched && bAssetExists && ExistingAsset)
    {
        UE_LOG(LogTemp, Log, TEXT("ProceduralGen: 版本匹配，跳过生成 -> %s"), *AssetPath);
        return ExistingAsset;
    }

    if (ExistingAsset && !ReleaseExistingAssetObjects(AssetPath, AssetName))
    {
        UE_LOG(LogTemp, Error, TEXT("ProceduralGen: 无法释放旧资产的内存对象 -> %s"), *AssetPath);
        return nullptr;
    }

    if (bAssetExists)
    {
        const FString ExistingPackageFileName = FPackageName::LongPackageNameToFilename(AssetPath, FPackageName::GetAssetPackageExtension());
        if (!IFileManager::Get().Delete(*ExistingPackageFileName))
        {
            UE_LOG(LogTemp, Warning, TEXT("ProceduralGen: 未删除旧资产包文件，将尝试直接覆盖保存 -> %s"), *AssetPath);
        }
    }

    UPackage* Package = CreatePackage(*AssetPath);
    Package->FullyLoad();
    ResetLoaders(Package);

    UE_LOG(LogTemp, Display, TEXT("ProceduralGen: 开始生成资产 -> %s"), *AssetPath);

    UObject* NewAsset = BuildFunc(Package, AssetName);
    if (!NewAsset)
    {
        UE_LOG(LogTemp, Error, TEXT("ProceduralGen: 构建回调返回了 nullptr -> %s"), *AssetPath);
        return nullptr;
    }

    FAssetRegistryModule::AssetCreated(NewAsset);
    auto _ = Package->MarkPackageDirty();

    const FString PackageFileName = FPackageName::LongPackageNameToFilename(AssetPath, FPackageName::GetAssetPackageExtension());

    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.Error = GError;
    SaveArgs.bForceByteSwapping = false;
    SaveArgs.bWarnOfLongFilename = true;
    SaveArgs.SaveFlags = SAVE_NoError;

    if (UPackage::SavePackage(Package, NewAsset, *PackageFileName, SaveArgs))
    {
        CacheMap.Add(AssetPath, Version);
        SaveCache(CacheMap);
        UE_LOG(LogTemp, Display, TEXT("ProceduralGen: 资产生成并保存成功 -> %s"), *AssetPath);
        if (UObject* ReloadedAsset = ReloadGeneratedAsset(Package, AssetPath))
        {
            return ReloadedAsset;
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("ProceduralGen: 资产保存失败 -> %s"), *AssetPath);
    }

    return NewAsset;
}

FString FProceduralAssetBuilder::GetCacheFilePath()
{
    return FPaths::ProjectSavedDir() / TEXT("ProceduralGenCache.json");
}

TMap<FString, FString> FProceduralAssetBuilder::LoadCache()
{
    TMap<FString, FString> Result;
    FString JsonString;
    if (FFileHelper::LoadFileToString(JsonString, *GetCacheFilePath()))
    {
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
        TSharedPtr<FJsonObject> JsonObj;
        if (FJsonSerializer::Deserialize(Reader, JsonObj) && JsonObj.IsValid())
        {
            for (auto& Pair : JsonObj->Values)
            {
                Result.Add(Pair.Key, Pair.Value->AsString());
            }
        }
    }
    return Result;
}

void FProceduralAssetBuilder::SaveCache(const TMap<FString, FString>& CacheMap)
{
    TSharedPtr<FJsonObject> JsonObj = MakeShareable(new FJsonObject);
    for (const auto& Pair : CacheMap)
    {
        JsonObj->SetStringField(Pair.Key, Pair.Value);
    }

    FString JsonString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
    FJsonSerializer::Serialize(JsonObj.ToSharedRef(), Writer);

    FFileHelper::SaveStringToFile(JsonString, *GetCacheFilePath());
}
#endif
