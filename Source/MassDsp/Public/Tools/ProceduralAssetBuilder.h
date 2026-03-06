#pragma once

#if WITH_EDITOR

#include "CoreMinimal.h"
#include "Templates/Function.h"

/**
 * 构建回调委托
 * @param InPackage 资产所在的包 (UPackage)
 * @param InAssetName 资产的短名称 (例如 "WBP_MyUI")
 * @return 返回构建好的 UObject 资产
 */
using FOnBuildAsset = TFunction<UObject*(UPackage* InPackage, const FString& InAssetName)>;

class MASSDSP_API FProceduralAssetBuilder
{
public:
    /**
     * 通用程序化生成入口
     * @param AssetPath 资产路径 (例如 "/Game/UI/WBP_MyUI")
     * @param Version   版本号字符串，变更时递增即可触发重新生成 (例如 "v2")
     * @param BuildFunc 具体的构建逻辑回调
     * @return 生成或加载的资产
     */
    static UObject* GenerateAsset(const FString& AssetPath, const FString& Version, FOnBuildAsset BuildFunc);

private:
    // 获取缓存文件路径 (保存在 Saved 目录下)
    static FString GetCacheFilePath();

    // 读取缓存字典
    static TMap<FString, FString> LoadCache();

    // 保存缓存字典
    static void SaveCache(const TMap<FString, FString>& CacheMap);
};

#endif
