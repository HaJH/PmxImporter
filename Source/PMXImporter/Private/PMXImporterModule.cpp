// Copyright (c) 2025 Jeonghyeon Ha. All Rights Reserved. 

#include "Modules/ModuleManager.h"
#include "LogPMXImporter.h"
#include "InterchangeManager.h"
#include "PmxTranslator.h"

#if WITH_EDITOR
#include "PmxPhysicsPostProcessor.h"
#endif

DEFINE_LOG_CATEGORY(LogPMXImporter);

class FPMXImporterModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
#if WITH_EDITOR
		UE_LOG(LogPMXImporter, Display, TEXT("PMX Importer module started"));
		UInterchangeManager& Manager = UInterchangeManager::GetInterchangeManager();
		Manager.RegisterTranslator(UPmxTranslator::StaticClass());
		UE_LOG(LogPMXImporter, Display, TEXT("Registered PMX translator"));

		// Initialize PMX PhysicsAsset post-processor
		PmxPhysicsPostProcessor::GetInstance().Initialize();
#endif
	}

	virtual void ShutdownModule() override
	{
#if WITH_EDITOR
		UE_LOG(LogPMXImporter, Verbose, TEXT("PMX Importer module shutdown"));
		
		// Shutdown PMX PhysicsAsset post-processor
		PmxPhysicsPostProcessor::GetInstance().Shutdown();
#endif
	}

};

IMPLEMENT_MODULE(FPMXImporterModule, PMXImporter);