// Copyright (c) 2025 Jeonghyeon Ha. All Rights Reserved. 

#include "Modules/ModuleManager.h"
#include "LogPMXImporter.h"
#include "InterchangeManager.h"
#include "PmxTranslator.h"


DEFINE_LOG_CATEGORY(LogPMXImporter);

class FPMXImporterModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		UE_LOG(LogPMXImporter, Display, TEXT("PMX Importer module started"));
		UInterchangeManager& Manager = UInterchangeManager::GetInterchangeManager();
		Manager.RegisterTranslator(UPmxTranslator::StaticClass());
		UE_LOG(LogPMXImporter, Display, TEXT("Registered PMX translator"));
	}

	virtual void ShutdownModule() override
	{
		UE_LOG(LogPMXImporter, Verbose, TEXT("PMX Importer module shutdown"));
	}

};

IMPLEMENT_MODULE(FPMXImporterModule, PMXImporter);