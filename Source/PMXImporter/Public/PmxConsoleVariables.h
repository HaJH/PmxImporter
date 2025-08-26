#pragma once

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"

/**
 * PMX Importer Console Variables
 * Centralized location for all PMX importer console variables
 */
namespace PMXImporter
{
	// Import scale (uniform). Users can override via console: PMXImporter.Scale
	extern TAutoConsoleVariable<float> CVarPMXImporterScale;

	// Minimum morph delta threshold (in cm, after PMX->UE transform). Users can override via console: PMXImporter.MorphMinDelta
	extern TAutoConsoleVariable<float> CVarPMXImporterMorphMinDelta;

	// Optionally drop morph targets that end up with zero effective vertex changes after filtering.
	extern TAutoConsoleVariable<bool> CVarPMXImporterMorphDropEmpty;
}