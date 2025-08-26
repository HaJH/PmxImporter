#include "PmxConsoleVariables.h"

namespace PMXImporter
{
	// Import scale (uniform). Users can override via console: PMXImporter.Scale
	TAutoConsoleVariable<float> CVarPMXImporterScale(
		TEXT("PMXImporter.Scale"),
		8.0f,
		TEXT("Baseline uniform scale applied for PMX import. Final scale = Baseline * (Interchange UniformScale, if any). Default = 8.0"),
		ECVF_Default);
	
	// Minimum morph delta threshold (in cm, after PMX->UE transform). Users can override via console: PMXImporter.MorphMinDelta
	TAutoConsoleVariable<float> CVarPMXImporterMorphMinDelta(
		TEXT("PMXImporter.MorphMinDelta"),
		0.0f,
		TEXT("Minimum morph delta length (in cm, after PMX->UE transform). Deltas smaller than this are ignored. Default = 0 (disabled)"),
		ECVF_Default);

	// Optionally drop morph targets that end up with zero effective vertex changes after filtering.
	TAutoConsoleVariable<bool> CVarPMXImporterMorphDropEmpty(
		TEXT("PMXImporter.MorphDropEmpty"),
		false,
		TEXT("If true, drop morph targets that have zero effective vertex changes after filtering. Default = false"),
		ECVF_Default);
}