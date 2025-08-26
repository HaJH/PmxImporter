// Copyright (c) 2025 Jeonghyeon Ha. All Rights Reserved. 

#include "PmxMaterialMapping.h"

#include "InterchangeMaterialInstanceNode.h"
#include "LogPMXImporter.h"
#include "HAL/IConsoleManager.h"
#include "PmxUtils.h"

// Console variables needed for material mapping
// Parent material for Material Instances. Users can override via console: PMXImporter.ParentMaterial
static TAutoConsoleVariable<FString> CVarPMXImporterParentMaterial(
	TEXT("PMXImporter.ParentMaterial"),
	TEXT("/PMXImporter/M_PMX_Base.M_PMX_Base"),
	TEXT("Parent material path for PMX Material Instances. Default = /PMXImporter/M_PMX_Base.M_PMX_Base"),
	ECVF_Default);

void FPmxMaterialMapping::CreateMaterials(const FPmxModel& PmxModel, const TMap<int32, FString>& TextureUidMap, 
	UInterchangeBaseNodeContainer& BaseNodeContainer, TArray<FString>& OutMaterialUids, TArray<FString>& OutSlotNames)
{
	OutMaterialUids.Reserve(PmxModel.Materials.Num());
	OutSlotNames.Reserve(PmxModel.Materials.Num());
	
	// Track used labels to ensure uniqueness
	TMap<FString, int32> UsedLabels;

	// Counters for summary
	int32 CountTwoSided = 0;
	int32 CountTranslucent = 0;
	int32 CountMasked = 0;
	
	for (int32 MatIdx = 0; MatIdx < PmxModel.Materials.Num(); ++MatIdx)
	{
		const FPmxMaterial& PmxMat = PmxModel.Materials[MatIdx];
		// Prefer original PMX material name (can be CJK), fallback to English, then index
		FString SlotLabel = PmxMat.Name;
		SlotLabel.TrimStartAndEndInline();
		if (SlotLabel.IsEmpty())
		{
			SlotLabel = PmxMat.NameEng;
			SlotLabel.TrimStartAndEndInline();
		}
		if (SlotLabel.IsEmpty())
		{
			SlotLabel = FString::Printf(TEXT("Mat_%d"), MatIdx);
		}
		// Unique-ify the label if needed
		int32& Count = UsedLabels.FindOrAdd(SlotLabel);
		FString UniqueLabel = SlotLabel;
		if (Count > 0)
		{
			UniqueLabel = FString::Printf(TEXT("%s_%d"), *SlotLabel, Count);
		}
		++Count;

		// All material instances must have MI_ prefix
		const FString DisplayLabel = FString::Printf(TEXT("MI_%s"), *UniqueLabel);

		// Create a Material Instance node only; parent will be set by Import UI pipeline.
		const FString MatSanitized = FPmxUtils::SanitizeAsciiToken(UniqueLabel);
		const FString MiNodeUid = FString::Printf(TEXT("/PMX/Materials/%d_%s"), MatIdx, *MatSanitized);
		UInterchangeMaterialInstanceNode* MiNode = UInterchangeMaterialInstanceNode::Create(&BaseNodeContainer, *DisplayLabel, *MiNodeUid);
		UE_LOG(LogPMXImporter, Verbose, TEXT("PMX Translator: Create MI Node Display='%s' SlotLabel='%s' NodeUid='%s'"), *DisplayLabel, *UniqueLabel, *MiNodeUid);
		if (ensure(MiNode))
		{
			FString ParentPath = CVarPMXImporterParentMaterial.GetValueOnAnyThread();
			ParentPath.TrimStartAndEndInline();
			if (!ParentPath.IsEmpty())
			{
				MiNode->SetCustomParent(ParentPath);
				UE_LOG(LogPMXImporter, Display, TEXT("PMX Translator: Set parent material to '%s' for MI '%s'"), *ParentPath, *DisplayLabel);
			}
			else
			{
				UE_LOG(LogPMXImporter, Warning, TEXT("PMX Translator: No parent material path specified in CVar for MI '%s'"), *DisplayLabel);
			}

			// ---- IM4U-inspired mappings ----
			// BaseColor via texture index if available, else fallback to diffuse color.
			FString BaseColorTexUid;
			if (PmxMat.TextureIndex >= 0)
			{
				if (const FString* FoundTexUid = TextureUidMap.Find(PmxMat.TextureIndex))
				{
					BaseColorTexUid = *FoundTexUid;
				}
			}
			if (!BaseColorTexUid.IsEmpty())
			{
				MiNode->AddTextureParameterValue(TEXT("BaseColorTexture"), BaseColorTexUid);
			}
			else
			{
				MiNode->AddVectorParameterValue(TEXT("BaseColorTint"), PmxMat.Diffuse);
			}

			// TwoSided from PMX drawing flags (bit 0 = culling off/double-sided)
			const bool bTwoSided = ((PmxMat.DrawingFlags & 0x01) != 0);
			if (bTwoSided)
			{
				MiNode->AddStaticSwitchParameterValue(TEXT("bTwoSided"), true);
				++CountTwoSided;
			}

			// Opacity from PMX diffuse alpha (conservative)
			const float Opacity = FMath::Clamp(PmxMat.Diffuse.A, 0.0f, 1.0f);
			MiNode->AddScalarParameterValue(TEXT("Opacity"), Opacity);

			// Conservative blend inference: treat alpha < 1 as Translucent hint (Masked requires explicit support later)
			if (Opacity < 0.999f)
			{
				MiNode->AddStaticSwitchParameterValue(TEXT("bTranslucentHint"), true);
				++CountTranslucent;
			}

			// Metadata preservation as numeric parameters so they survive through pipelines
			MiNode->AddScalarParameterValue(TEXT("pmx.mat.index"), static_cast<float>(MatIdx));
			// Sphere/Toon metadata (numeric only here)
			MiNode->AddScalarParameterValue(TEXT("pmx.sphere.mode"), static_cast<float>(PmxMat.SphereMode));
			MiNode->AddStaticSwitchParameterValue(TEXT("pmx.toon.mode"), static_cast<bool>(PmxMat.SharedToonFlag));

			// Edge, Specular, Ambient (store common fields if available)
			MiNode->AddStaticSwitchParameterValue(TEXT("pmx.edge.draw"), PmxMat.EdgeSize > 0.0f);
			MiNode->AddVectorParameterValue(TEXT("pmx.edge.color"), PmxMat.EdgeColor);
			MiNode->AddScalarParameterValue(TEXT("pmx.edge.size"), PmxMat.EdgeSize);
			MiNode->AddVectorParameterValue(TEXT("pmx.specular.rgb"), FLinearColor(PmxMat.Specular.X, PmxMat.Specular.Y, PmxMat.Specular.Z, 1.0f));
			MiNode->AddScalarParameterValue(TEXT("pmx.specular.power"), PmxMat.SpecularStrength);
			MiNode->AddVectorParameterValue(TEXT("pmx.ambient.rgb"), FLinearColor(PmxMat.Ambient.X, PmxMat.Ambient.Y, PmxMat.Ambient.Z, 1.0f));

			// Store slot mapping with MI node UID
			OutSlotNames.Add(UniqueLabel);
			OutMaterialUids.Add(MiNode->GetUniqueID());
		}
		else
		{
			// Fallback: keep slot index with empty material to avoid misalignment
			OutSlotNames.Add(UniqueLabel);
			OutMaterialUids.Add(TEXT(""));
		}
	}

	UE_LOG(LogPMXImporter, Display, TEXT("PMX Translator: Materials=%d, TwoSided=%d, Masked=%d, Translucent=%d"),
		PmxModel.Materials.Num(), CountTwoSided, CountMasked, CountTranslucent);
}

// SanitizeAsciiToken function moved to FPmxUtils class