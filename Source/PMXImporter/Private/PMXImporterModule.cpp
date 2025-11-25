// Copyright (c) 2025 Jeonghyeon Ha. All Rights Reserved.

#include "Modules/ModuleManager.h"
#include "LogPMXImporter.h"
#include "InterchangeManager.h"
#include "InterchangeProjectSettings.h"
#include "PmxTranslator.h"

DEFINE_LOG_CATEGORY(LogPMXImporter);

class FPMXImporterModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		UE_LOG(LogPMXImporter, Display, TEXT("PMX Importer module started"));

		// Register translator immediately (required for file extension recognition)
		UInterchangeManager& InterchangeManager = UInterchangeManager::GetInterchangeManager();
		InterchangeManager.RegisterTranslator(UPmxTranslator::StaticClass());
		UE_LOG(LogPMXImporter, Display, TEXT("Registered PMX translator"));

		// Since LoadingPhase is PostEngineInit, we can register pipelines directly here
		// (OnPostEngineInit has already fired by the time this module loads)
		RegisterPipelines();
	}

	virtual void ShutdownModule() override
	{
		// Clear static caches
		UPmxTranslator::MeshPayloadCache.Empty();
		UPmxTranslator::PhysicsPayloadCache.Empty();

		UE_LOG(LogPMXImporter, Verbose, TEXT("PMX Importer module shutdown"));
	}

private:
	void RegisterPipelines()
	{
#if WITH_EDITOR
		UE_LOG(LogPMXImporter, Display, TEXT("RegisterPipelines called - Starting pipeline registration"));

		// Create translator-to-pipeline mapping
		TSoftClassPtr<UInterchangeTranslatorBase> TranslatorClassPath = TSoftClassPtr<UInterchangeTranslatorBase>(
			UPmxTranslator::StaticClass()
		);

		UE_LOG(LogPMXImporter, Display, TEXT("TranslatorClassPath: %s"), *TranslatorClassPath.ToString());

		FInterchangeTranslatorPipelines TranslatorPipelines;
		TranslatorPipelines.Translator = TranslatorClassPath;
		// Reference the Blueprint pipeline asset in the plugin's Content folder
		// This asset must be created in UE Editor: Right-click → Miscellaneous → Data Asset → PmxPipeline
		FSoftObjectPath PipelinePath(TEXT("/PMXImporter/Pipelines/DefaultPmxPipeline.DefaultPmxPipeline"));
		TranslatorPipelines.Pipelines.Add(PipelinePath);

		UE_LOG(LogPMXImporter, Display, TEXT("Pipeline path: %s"), *PipelinePath.ToString());

		// Verify the pipeline asset can be loaded
		if (UObject* LoadedPipeline = PipelinePath.TryLoad())
		{
			UE_LOG(LogPMXImporter, Display, TEXT("Pipeline asset loaded successfully: %s"), *LoadedPipeline->GetClass()->GetName());
		}
		else
		{
			UE_LOG(LogPMXImporter, Warning, TEXT("Failed to load pipeline asset at: %s"), *PipelinePath.ToString());
		}

		// Get project settings
		UInterchangeProjectSettings* ProjectSettings = GetMutableDefault<UInterchangeProjectSettings>();
		if (!ProjectSettings)
		{
			UE_LOG(LogPMXImporter, Warning, TEXT("Failed to get InterchangeProjectSettings"));
			return;
		}

		// Register pipeline for Asset import (PMX is a Meshes type)
		FInterchangeContentImportSettings& AssetImportSettings = ProjectSettings->ContentImportSettings;

		// Log available pipeline stacks
		UE_LOG(LogPMXImporter, Display, TEXT("Available pipeline stacks:"));
		for (const auto& StackPair : AssetImportSettings.PipelineStacks)
		{
			UE_LOG(LogPMXImporter, Display, TEXT("  - Stack: %s (Pipelines: %d, PerTranslator: %d)"),
				*StackPair.Key.ToString(),
				StackPair.Value.Pipelines.Num(),
				StackPair.Value.PerTranslatorPipelines.Num());
		}

		// Add to "Assets" pipeline stack
		if (FInterchangePipelineStack* AssetStack = AssetImportSettings.PipelineStacks.Find(TEXT("Assets")))
		{
			AssetStack->PerTranslatorPipelines.Add(TranslatorPipelines);
			UE_LOG(LogPMXImporter, Display, TEXT("Registered PMX pipeline to 'Assets' stack (PerTranslatorPipelines count: %d)"),
				AssetStack->PerTranslatorPipelines.Num());
		}
		else
		{
			UE_LOG(LogPMXImporter, Warning, TEXT("'Assets' pipeline stack not found"));
		}

		// Also register for Scene import settings
		FInterchangeImportSettings& SceneImportSettings = ProjectSettings->SceneImportSettings;
		if (FInterchangePipelineStack* SceneStack = SceneImportSettings.PipelineStacks.Find(TEXT("Scene")))
		{
			SceneStack->PerTranslatorPipelines.Add(TranslatorPipelines);
			UE_LOG(LogPMXImporter, Display, TEXT("Registered PMX pipeline to 'Scene' stack"));
		}

		// Set import dialog to show for PMX files
		{
			FInterchangeDialogOverride& DialogOverrides = AssetImportSettings.ShowImportDialogOverride.FindOrAdd(
				EInterchangeTranslatorAssetType::Meshes
			);

			FInterchangePerTranslatorDialogOverride ImportDialogOverride;
			ImportDialogOverride.Translator = TranslatorClassPath;
			ImportDialogOverride.bShowImportDialog = true;
			ImportDialogOverride.bShowReimportDialog = true;
			DialogOverrides.PerTranslatorImportDialogOverride.Add(ImportDialogOverride);

			UE_LOG(LogPMXImporter, Display, TEXT("Configured PMX import dialog override"));
		}

		UE_LOG(LogPMXImporter, Display, TEXT("PMX Pipeline registration complete"));
#endif
	}
};

IMPLEMENT_MODULE(FPMXImporterModule, PMXImporter);
