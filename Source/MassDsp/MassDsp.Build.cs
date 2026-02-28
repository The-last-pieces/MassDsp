// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MassDsp : ModuleRules
{
    public MassDsp(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange([
            "Core",
            "CoreUObject",
            "Engine",
            "InputCore",
            "EnhancedInput",
            "MassEntity",
            "MassRepresentation",
            "MassCommon",
            "MassLOD",
            "MassSignals",
            "MassNavigation",
            "MassMovement",
            "ZoneGraph",
            "MassZoneGraphNavigation",
            "HeadMountedDisplay",
            "MassActors",
            "MassSmartObjects",
            "MassSpawner",
        ]);

        PrivateDependencyModuleNames.AddRange([
            "MassMovement", "ProceduralMeshComponent", // "CableComponent",
        ]);

        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.AddRange([
                "MaterialEditor",
                "UnrealEd",
                "ToolMenus",
                "Slate",
                "SlateCore",
                "EditorStyle",
            ]);
        }

        // Uncomment if you are using Slate UI
        // PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

        // Uncomment if you are using online features
        // PrivateDependencyModuleNames.Add("OnlineSubsystem");

        // To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
    }
}