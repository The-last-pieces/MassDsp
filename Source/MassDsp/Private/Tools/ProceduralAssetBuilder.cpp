#if WITH_EDITOR
#include "Tools/ProceduralAssetBuilder.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "UObject/SavePackage.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"

UObject* FProceduralAssetBuilder::GenerateAsset(const FString& AssetPath, const FString& Version, FOnBuildAsset BuildFunc)
{
    // 1. 以版本号直接与缓存比较
    TMap<FString, FString> CacheMap = LoadCache();
    bool bCacheMatched = CacheMap.Contains(AssetPath) && CacheMap[AssetPath] == Version;

    // 2. 如果版本匹配且资产存在，直接加载并返回 (极速跳过)
    if (bool bAssetExists = FPackageName::DoesPackageExist(AssetPath); bCacheMatched && bAssetExists)
    {
        UE_LOG(LogTemp, Log, TEXT("ProceduralGen: 版本匹配，跳过生成 -> %s"), *AssetPath);
        // 必须使用完整对象路径（PackagePath.AssetName），否则 StaticLoadObject 找不到对象
        const FString ObjectPath = AssetPath + TEXT(".") + FPackageName::GetLongPackageAssetName(AssetPath);
        return StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPath);
    }

    UE_LOG(LogTemp, Display, TEXT("ProceduralGen: 开始生成资产 -> %s"), *AssetPath);

    // 4. 准备 Package 和 AssetName
    FString AssetName = FPackageName::GetLongPackageAssetName(AssetPath);
    UPackage* Package = CreatePackage(*AssetPath);
    Package->FullyLoad();

    // 5. 执行外部传入的构建逻辑
    UObject* NewAsset = BuildFunc(Package, AssetName);

    if (!NewAsset)
    {
        UE_LOG(LogTemp, Error, TEXT("ProceduralGen: 构建回调返回了 nullptr -> %s"), *AssetPath);
        return nullptr;
    }

    // 6. 注册资产并保存 Package (UE5 标准保存流程)
    FAssetRegistryModule::AssetCreated(NewAsset);
    auto _ = Package->MarkPackageDirty();

    FString PackageFileName = FPackageName::LongPackageNameToFilename(AssetPath, FPackageName::GetAssetPackageExtension());

    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.Error = GError;
    SaveArgs.bForceByteSwapping = false; // 编辑器本机保存无需字节序交换，true 会写出大端数据导致资产损坏
    SaveArgs.bWarnOfLongFilename = true;
    SaveArgs.SaveFlags = SAVE_NoError;

    if (UPackage::SavePackage(Package, NewAsset, *PackageFileName, SaveArgs))
    {
        // 7. 保存成功，更新缓存（以 AssetPath 为 key，Version 为 value）
        CacheMap.Add(AssetPath, Version);
        SaveCache(CacheMap);
        UE_LOG(LogTemp, Display, TEXT("ProceduralGen: 资产生成并保存成功 -> %s"), *AssetPath);
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
