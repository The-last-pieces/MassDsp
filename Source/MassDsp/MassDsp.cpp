// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassDsp.h"
#include "Modules/ModuleManager.h"

#if WITH_EDITOR
#include "ToolMenus.h"
#include "Tools/MaterialGeneratorUtils.h"
#endif


void FMassDspModule::StartupModule()
{
#if WITH_EDITOR
    // 注册菜单扩展
    UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FMassDspModule::RegisterMenus));
#endif
}

void FMassDspModule::ShutdownModule()
{
}

#if WITH_EDITOR
void FMassDspModule::RegisterMenus()
{
    FToolMenuOwnerScoped OwnerScoped(this);

    static const FName NewMenuName = "LevelEditor.MainMenu.Tools";
    UToolMenu* NewMenu = UToolMenus::Get()->ExtendMenu(NewMenuName);

    FToolMenuSection& NewSection = NewMenu->FindOrAddSection("MassDsp", FText::FromString("MassDsp"));

    NewSection.AddMenuEntry(
        "CreateAllProceduralAssets",
        FText::FromString(TEXT("生成传送带/无人机材质+建筑UI蓝图")),
        FText::FromString(TEXT("生成传送带/无人机材质+建筑UI蓝图")),
        FSlateIcon(),
        FUIAction(FExecuteAction::CreateStatic(&FUMaterialGeneratorUtils::CreateAllProceduralAssets))
    );

    UE_LOG(LogTemp, Log, TEXT("Menus Registered."));
}
#endif

IMPLEMENT_PRIMARY_GAME_MODULE(FMassDspModule, MassDsp, "MassDsp");
