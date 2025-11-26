// Copyright (c) 2025 Jeonghyeon Ha. All Rights Reserved.

#include "Modules/ModuleManager.h"
#include "LogPMXImporter.h"
#include "InterchangeManager.h"
#include "InterchangeProjectSettings.h"
#include "PmxTranslator.h"
#include "PmxPipeline.h"
#include "VmdTranslator.h"
#include "VmdPipeline.h"

DEFINE_LOG_CATEGORY(LogPMXImporter);

class FPMXImporterModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		UE_LOG(LogPMXImporter, Display, TEXT("PMX Importer module started"));

		// Register translators immediately (required for file extension recognition)
		UInterchangeManager& InterchangeManager = UInterchangeManager::GetInterchangeManager();
		InterchangeManager.RegisterTranslator(UPmxTranslator::StaticClass());
		UE_LOG(LogPMXImporter, Display, TEXT("Registered PMX translator"));

		InterchangeManager.RegisterTranslator(UVmdTranslator::StaticClass());
		UE_LOG(LogPMXImporter, Display, TEXT("Registered VMD translator"));

		// Since LoadingPhase is PostEngineInit, we can register pipelines directly here
		// (OnPostEngineInit has already fired by the time this module loads)
		RegisterPipelines();
	}

	virtual void ShutdownModule() override
	{
		// Note: DefaultPipelineInstance cleanup is skipped during engine shutdown
		// AddToRoot() objects are automatically cleaned up by GC during exit

		// Clear static caches
		UPmxTranslator::MeshPayloadCache.Empty();
		UPmxTranslator::PhysicsPayloadCache.Empty();
		UVmdTranslator::VmdPayloadCache.Empty();

		UE_LOG(LogPMXImporter, Verbose, TEXT("PMX Importer module shutdown"));
	}

private:
	TObjectPtr<UPmxPipeline> DefaultPmxPipelineInstance;
	TObjectPtr<UVmdPipeline> DefaultVmdPipelineInstance;

	void RegisterPipelines()
	{
#if WITH_EDITOR
		UE_LOG(LogPMXImporter, Display, TEXT("RegisterPipelines called - Starting pipeline registration"));

		// Create translator-to-pipeline mapping
		TSoftClassPtr<UInterchangeTranslatorBase> TranslatorClassPath = TSoftClassPtr<UInterchangeTranslatorBase>(
			UPmxTranslator::StaticClass()
		);

		UE_LOG(LogPMXImporter, Display, TEXT("TranslatorClassPath: %s"), *TranslatorClassPath.ToString());

		// Dynamically create PMX pipeline instance at runtime (no Blueprint asset dependency)
		const FString PmxPackagePath = TEXT("/PMXImporter/Pipelines/DefaultPmxPipeline");
		UPackage* PmxPipelinePackage = CreatePackage(*PmxPackagePath);
		PmxPipelinePackage->SetPackageFlags(PKG_CompiledIn);

		DefaultPmxPipelineInstance = NewObject<UPmxPipeline>(
			PmxPipelinePackage,
			UPmxPipeline::StaticClass(),
			TEXT("DefaultPmxPipeline"),
			RF_Public | RF_Standalone
		);
		DefaultPmxPipelineInstance->AddToRoot();

		FInterchangeTranslatorPipelines TranslatorPipelines;
		TranslatorPipelines.Translator = TranslatorClassPath;
		FSoftObjectPath PipelinePath(DefaultPmxPipelineInstance);
		TranslatorPipelines.Pipelines.Add(PipelinePath);

		UE_LOG(LogPMXImporter, Display, TEXT("PMX Pipeline created dynamically: %s"), *PipelinePath.ToString());

		// Create VMD pipeline
		TSoftClassPtr<UInterchangeTranslatorBase> VmdTranslatorClassPath = TSoftClassPtr<UInterchangeTranslatorBase>(
			UVmdTranslator::StaticClass()
		);

		const FString VmdPackagePath = TEXT("/PMXImporter/Pipelines/DefaultVmdPipeline");
		UPackage* VmdPipelinePackage = CreatePackage(*VmdPackagePath);
		VmdPipelinePackage->SetPackageFlags(PKG_CompiledIn);

		DefaultVmdPipelineInstance = NewObject<UVmdPipeline>(
			VmdPipelinePackage,
			UVmdPipeline::StaticClass(),
			TEXT("DefaultVmdPipeline"),
			RF_Public | RF_Standalone
		);
		DefaultVmdPipelineInstance->AddToRoot();

		FInterchangeTranslatorPipelines VmdTranslatorPipelines;
		VmdTranslatorPipelines.Translator = VmdTranslatorClassPath;
		FSoftObjectPath VmdPipelinePath(DefaultVmdPipelineInstance);
		VmdTranslatorPipelines.Pipelines.Add(VmdPipelinePath);

		UE_LOG(LogPMXImporter, Display, TEXT("VMD Pipeline created dynamically: %s"), *VmdPipelinePath.ToString());

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
			AssetStack->PerTranslatorPipelines.Add(VmdTranslatorPipelines);
			UE_LOG(LogPMXImporter, Display, TEXT("Registered PMX and VMD pipelines to 'Assets' stack (PerTranslatorPipelines count: %d)"),
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

		// Set import dialog to show for VMD files (Animations type)
		{
			FInterchangeDialogOverride& VmdDialogOverrides = AssetImportSettings.ShowImportDialogOverride.FindOrAdd(
				EInterchangeTranslatorAssetType::Animations
			);

			FInterchangePerTranslatorDialogOverride VmdImportDialogOverride;
			VmdImportDialogOverride.Translator = VmdTranslatorClassPath;
			VmdImportDialogOverride.bShowImportDialog = true;
			VmdImportDialogOverride.bShowReimportDialog = true;
			VmdDialogOverrides.PerTranslatorImportDialogOverride.Add(VmdImportDialogOverride);

			UE_LOG(LogPMXImporter, Display, TEXT("Configured VMD import dialog override"));
		}

		UE_LOG(LogPMXImporter, Display, TEXT("PMX Pipeline registration complete"));
#endif
	}
};

IMPLEMENT_MODULE(FPMXImporterModule, PMXImporter);
