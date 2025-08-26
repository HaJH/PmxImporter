// Copyright (c) 2025 Jeonghyeon Ha. All Rights Reserved. 

#include "PmxPhysicsPostProcessor.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Engine/SkeletalMesh.h"
#include "PmxTranslator.h"
#include "PmxPhysicsMapping.h"
#include "PmxStructs.h"
#include "LogPMXImporter.h"
#include "Modules/ModuleManager.h"

using namespace PmxPhysicsPostProcessor;

FPMXPhysicsPostProcessor::FPMXPhysicsPostProcessor()
{
}

FPMXPhysicsPostProcessor::~FPMXPhysicsPostProcessor()
{
	Shutdown();
}

void FPMXPhysicsPostProcessor::Initialize()
{
	// Register AssetRegistry event for PMX PhysicsAsset post-processing
	if (FModuleManager::Get().IsModuleLoaded("AssetRegistry"))
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		AssetAddedHandle = AssetRegistryModule.Get().OnAssetAdded().AddRaw(this, &FPMXPhysicsPostProcessor::OnAssetAdded);
		UE_LOG(LogPMXImporter, Display, TEXT("PMX PostProcessor: Successfully registered AssetRegistry event handler"));
	}
	else
	{
		UE_LOG(LogPMXImporter, Warning, TEXT("PMX PostProcessor: AssetRegistry module not loaded, cannot register event handler"));
	}
}

void FPMXPhysicsPostProcessor::Shutdown()
{
	// Unregister AssetRegistry event
	if (AssetAddedHandle.IsValid() && FModuleManager::Get().IsModuleLoaded("AssetRegistry"))
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		AssetRegistryModule.Get().OnAssetAdded().Remove(AssetAddedHandle);
		AssetAddedHandle.Reset();
	}
}

void FPMXPhysicsPostProcessor::OnAssetAdded(const FAssetData& AssetData)
{
	// Check if this asset should be processed
	if (!ShouldProcessAsset(AssetData))
	{
		return;
	}

	const FString AssetNameStr = AssetData.AssetName.ToString();
	UE_LOG(LogPMXImporter, VeryVerbose, TEXT("PMX PostProcessor: Processing asset '%s' (%s)"), 
		*AssetNameStr, *AssetData.AssetClassPath.ToString());

	// 2) PhysicsAsset: build from PMX physics data if available
	if (UPhysicsAsset* PhysicsAsset = Cast<UPhysicsAsset>(AssetData.GetAsset()))
	{
		ProcessPMXPhysicsAsset(PhysicsAsset);
		return;
	}

	UE_LOG(LogPMXImporter, Verbose, TEXT("PMX PostProcessor: Asset '%s' is not a supported type"), *AssetNameStr);
}

void FPMXPhysicsPostProcessor::ProcessPMXPhysicsAsset(UPhysicsAsset* PhysicsAsset)
{
	UE_LOG(LogPMXImporter, VeryVerbose, TEXT("PMX PostProcessor: ProcessPMXPhysicsAsset called for '%s'"), 
		PhysicsAsset ? *PhysicsAsset->GetName() : TEXT("NULL"));

	if (!PhysicsAsset)
	{
		UE_LOG(LogPMXImporter, Display, TEXT("PMX PostProcessor: PhysicsAsset is NULL"));
		return;
	}

	// Find associated SkeletalMesh
	USkeletalMesh* SkeletalMesh = PhysicsAsset->GetPreviewMesh();
	if (!SkeletalMesh)
	{
		UE_LOG(LogPMXImporter, Warning, TEXT("PMX Physics: No preview mesh found for PhysicsAsset '%s'"), *PhysicsAsset->GetName());
		return;
	}

	UE_LOG(LogPMXImporter, VeryVerbose, TEXT("PMX PostProcessor: Found preview mesh '%s' for PhysicsAsset '%s'"), 
		*SkeletalMesh->GetName(), *PhysicsAsset->GetName());
	
	for (const auto& CachePair : UPmxTranslator::MeshPayloadCache)
	{
		UE_LOG(LogPMXImporter, VeryVerbose, TEXT("PMX PostProcessor: [Translator] Cache key: '%s', Valid: %s"), 
			*CachePair.Key, CachePair.Value.IsValid() ? TEXT("Yes") : TEXT("No"));
	}

	// Try to find cached PMX data
	const TSharedPtr<FPmxModel>* CachedModel = UPmxTranslator::MeshPayloadCache.Find(TEXT("PMX_GEOMETRY"));
	if (!CachedModel || !CachedModel->IsValid())
	{
		UE_LOG(LogPMXImporter, Verbose, TEXT("PMX Physics: No cached PMX data found for PhysicsAsset '%s'"), *PhysicsAsset->GetName());
		return;
	}

	UE_LOG(LogPMXImporter, Display, TEXT("PMX PostProcessor: Found cached PMX model with %d RigidBodies and %d Joints"), 
		(*CachedModel)->RigidBodies.Num(), (*CachedModel)->Joints.Num());

	// If physics data is missing (e.g., PMX parsing aborted), skip processing to preserve auto-generated physics
	if ((*CachedModel)->RigidBodies.Num() == 0)
	{
		UE_LOG(LogPMXImporter, Warning, TEXT("PMX PostProcessor: Cached PMX has no RigidBodies (Joints=%d). Skipping PhysicsAsset rebuild for '%s' to preserve existing content."), (*CachedModel)->Joints.Num(), *PhysicsAsset->GetName());
		return;
	}

	// Snapshot counts before
	const int32 PrevBodies = PhysicsAsset->SkeletalBodySetups.Num();
	const int32 PrevConstraints = PhysicsAsset->ConstraintSetup.Num();

	// Apply PMX physics data to the PhysicsAsset
	const PmxPhysics::FCreateResult Result = PmxPhysics::CreatePhysicsAssetFromPMX(**CachedModel, PhysicsAsset, SkeletalMesh);
	const int32 NewBodies = PhysicsAsset->SkeletalBodySetups.Num();
	const int32 NewConstraints = PhysicsAsset->ConstraintSetup.Num();
	if (Result.bSuccess)
	{
		UE_LOG(LogPMXImporter, Display, TEXT("PMX Physics: Successfully processed PhysicsAsset '%s' with %d bodies, %d constraints (prev: %d/%d, delta: %+d/%+d)"), 
			*PhysicsAsset->GetName(), NewBodies, NewConstraints, PrevBodies, PrevConstraints, NewBodies - PrevBodies, NewConstraints - PrevConstraints);
		if (Result.BodiesCreated != (*CachedModel)->RigidBodies.Num() || Result.ConstraintsCreated != (*CachedModel)->Joints.Num())
		{
			UE_LOG(LogPMXImporter, Warning, TEXT("PMX Physics: Model bodies=%d, created=%d; model joints=%d, created=%d. See verbose logs for reasons (invalid indices, identical bones, etc.)."),
				(*CachedModel)->RigidBodies.Num(), Result.BodiesCreated, (*CachedModel)->Joints.Num(), Result.ConstraintsCreated);
		}
		// Mark the asset as dirty so it gets saved
		PhysicsAsset->MarkPackageDirty();
	}
	else
	{
		UE_LOG(LogPMXImporter, Warning, TEXT("PMX Physics: Failed to process PhysicsAsset '%s'"), *PhysicsAsset->GetName());
	}
}

bool FPMXPhysicsPostProcessor::ShouldProcessAsset(const FAssetData& AssetData) const
{
	// Process both SkeletalMesh and PhysicsAsset created during PMX import
	const FTopLevelAssetPath ClassPath = AssetData.AssetClassPath;
	if (ClassPath == USkeletalMesh::StaticClass()->GetClassPathName())
	{
		return true;
	}
	if (ClassPath == UPhysicsAsset::StaticClass()->GetClassPathName())
	{
		// Narrow PhysicsAsset to PMX-generated ones by name
		const FString AssetName = AssetData.AssetName.ToString();
		return AssetName.EndsWith(TEXT("_PhysicsAsset"));
	}
	return false;
}

FPMXPhysicsPostProcessor& PmxPhysicsPostProcessor::GetInstance()
{
	static FPMXPhysicsPostProcessor Instance;
	return Instance;
}

#endif // WITH_EDITOR