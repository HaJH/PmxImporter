// Copyright (c) 2025 Jeonghyeon Ha. All Rights Reserved. 

using UnrealBuildTool;
using System.IO;

public class PMXImporter : ModuleRules
{
    public PMXImporter(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "DeveloperSettings",
            "InterchangeCore",
            "InterchangeFactoryNodes",
            "InterchangeCommonParser",
            "PhysicsCore",
            "MeshDescription"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "UnrealEd",
            "RenderCore",
            "InterchangeCore",
            "InterchangeNodes",
            "InterchangeEngine",
            "InterchangeEditor",
            "InterchangeImport",
            "InterchangePipelines",
            "StaticMeshDescription",
            "SkeletalMeshDescription",
            "AnimationCore",
            "AssetRegistry"
        });

        PublicIncludePaths.AddRange(new string[]
        {
            Path.Combine(EngineDirectory, "Plugins/Interchange/Runtime/Source/FactoryNodes/Public"),
            Path.Combine(EngineDirectory, "Plugins/Interchange/Runtime/Source/Nodes/Public"),
            Path.Combine(EngineDirectory, "Plugins/Interchange/Runtime/Source/Import/Public"),
            Path.Combine(EngineDirectory, "Plugins/Interchange/Runtime/Source/Import/Public/Mesh")
        });
        PrivateIncludePaths.AddRange(new string[] {});
    }
}