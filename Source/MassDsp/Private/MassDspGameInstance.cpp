#include "MassDspGameInstance.h"

#include "GameFramework/GameUserSettings.h"
#include "Engine/Engine.h"

void UMassDspGameInstance::Init()
{
    Super::Init();

#if UE_BUILD_SHIPPING
    // 在 GameInstance::Init 阶段设置窗口模式，早于地图加载，避免启动时短暂全屏
    if (UGameUserSettings* UserSettings = GEngine->GetGameUserSettings())
    {
        UserSettings->SetFullscreenMode(EWindowMode::Windowed);
        UserSettings->ApplySettings(true);
    }
#endif
}
