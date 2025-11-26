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
            "AssetRegistry",
            // For image resizing/utilities used by the translator when handling texture payloads
            "ImageCore",
            // For VMD camera animation (Level Sequence)
            "LevelSequence",
            "MovieScene"
        });

        PublicIncludePaths.AddRange(new string[]
        {
            Path.Combine(EngineDirectory, "Plugins/Interchange/Runtime/Source/FactoryNodes/Public"),
            Path.Combine(EngineDirectory, "Plugins/Interchange/Runtime/Source/Nodes/Public"),
            Path.Combine(EngineDirectory, "Plugins/Interchange/Runtime/Source/Import/Public"),
            Path.Combine(EngineDirectory, "Plugins/Interchange/Runtime/Source/Import/Public/Mesh"),
            Path.Combine(EngineDirectory, "Plugins/Interchange/Runtime/Source/Import/Public/Animation"),
            Path.Combine(EngineDirectory, "Plugins/Interchange/Runtime/Source/Pipelines/Public"),
            Path.Combine(EngineDirectory, "Plugins/Interchange/Runtime/Source/Parsers/CommonParser/Public")
        });
        PrivateIncludePaths.AddRange(new string[] {});
    }
}