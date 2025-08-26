// Copyright (c) 2025 Jeonghyeon Ha. All Rights Reserved. 

#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR
#include "AssetRegistry/IAssetRegistry.h"
#endif

class UPhysicsAsset;

namespace PmxPhysicsPostProcessor
{
#if WITH_EDITOR
	/**
	 * PMX PhysicsAsset post-processor singleton.
	 * Automatically processes PMX-generated PhysicsAssets when they are added to the asset registry.
	 */
	class PMXIMPORTER_API FPMXPhysicsPostProcessor
	{
	public:
		FPMXPhysicsPostProcessor();
		~FPMXPhysicsPostProcessor();

		// Initialize the post-processor (register asset registry events)
		void Initialize();
		
		// Shutdown the post-processor (unregister events)
		void Shutdown();

	private:
		FDelegateHandle AssetAddedHandle;

		// Asset registry callback - called when new assets are added
		void OnAssetAdded(const FAssetData& AssetData);

		// Process a PMX-generated PhysicsAsset with PMX physics data
		void ProcessPMXPhysicsAsset(UPhysicsAsset* PhysicsAsset);

		// Check if an asset should be processed by this post-processor
		bool ShouldProcessAsset(const FAssetData& AssetData) const;
	};

	// Global instance accessor
	PMXIMPORTER_API FPMXPhysicsPostProcessor& GetInstance();
#endif // WITH_EDITOR
}