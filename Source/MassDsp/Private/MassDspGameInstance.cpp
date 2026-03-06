#include "MassDspGameInstance.h"

#include "GameFramework/GameUserSettings.h"
#include "Engine/Engine.h"

void UMassDspGameInstance::Init()
{
    Super::Init();

    // 1. 彻底关闭景深 (Depth of Field)，保证远景工厂绝对清晰
    if (IConsoleVariable* CVarDoF = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DepthOfFieldQuality")))
    {
        CVarDoF->Set(0, ECVF_SetByCode);
    }

    // // 2. 注入后期锐化，对抗 TAA/TSR 带来的时序涂抹感 (数值 1.0 ~ 3.0)
    if (IConsoleVariable* CVarSharpen = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Tonemapper.Sharpen")))
    {
        CVarSharpen->Set(2.0f, ECVF_SetByCode);
    }
    //
    // // 3. 限制动态分辨率下限 (保底 75%)，防止 136 万实体同屏时引擎把画面降得太糊
    if (IConsoleVariable* CVarDynResMin = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DynamicRes.MinScreenPercentage")))
    {
        CVarDynResMin->Set(75.0f, ECVF_SetByCode);
    }

    // 4. 开启 16x 各向异性过滤，拯救高空俯视视角的地面贴图模糊 (几乎 0 性能开销)
    if (IConsoleVariable* CVarAnisotropy = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MaxAnisotropy")))
    {
        CVarAnisotropy->Set(16, ECVF_SetByCode);
    }

#if UE_BUILD_SHIPPING
    // 在 GameInstance::Init 阶段设置窗口模式，早于地图加载，避免启动时短暂全屏
    if (UGameUserSettings* UserSettings = GEngine->GetGameUserSettings())
    {
        UserSettings->SetFullscreenMode(EWindowMode::Fullscreen);
        UserSettings->ApplySettings(true);
    }
#endif
}
