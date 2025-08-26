// Copyright (c) 2025 Jeonghyeon Ha. All Rights Reserved. 

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "PhysicsEngine/ConstraintTypes.h"
#include "PmxImporterSettings.generated.h"

/**
 * PMX Importer physics constraint defaults.
 * Exposed in Project Settings -> Plugins -> PMX Importer Physics.
 */
UCLASS(config=Game, defaultconfig, meta=(DisplayName="PMX Importer Settings", CategoryName="Plugins"))
class PMXIMPORTER_API UPmxImporterSettings : public UDeveloperSettings
{
	GENERATED_BODY()
public:
	UPmxImporterSettings();

public:
	/** If true, ignore PMX-provided joint linear/angular limits and use these defaults for all created constraints. */
	UPROPERTY(EditAnywhere, Config, Category="Physics|General")
	bool bOverrideJointConstraints = true;

	/** Make any physics body that is not referenced by any joint (unconstrained) Kinematic to act as an anchor and prevent explosions. */
	UPROPERTY(EditAnywhere, Config, Category="Physics|General")
	bool bKinematicForUnconstrainedBodies = true;

	/** Enable constraint projection to reduce initial error and jitter. */
	UPROPERTY(EditAnywhere, Config, Category="Physics|General")
	bool bEnableProjection = true;

	/** Projection linear tolerance (cm). Suggested 1.0~2.0 */
	UPROPERTY(EditAnywhere, Config, Category="Physics|General", meta=(ClampMin="0.0", UIMin="0.0"))
	float ProjectionLinearTolerance = 2.0f;

	/** Projection angular tolerance (deg). Suggested 5~10 */
	UPROPERTY(EditAnywhere, Config, Category="Physics|General", meta=(ClampMin="0.0", UIMin="0.0"))
	float ProjectionAngularTolerance = 10.0f;

	// Linear (translation) motions
	UPROPERTY(EditAnywhere, Config, Category="Physics|Constraints|Linear")
	TEnumAsByte<ELinearConstraintMotion> LinearX = ELinearConstraintMotion::LCM_Locked;

	UPROPERTY(EditAnywhere, Config, Category="Physics|Constraints|Linear")
	TEnumAsByte<ELinearConstraintMotion> LinearY = ELinearConstraintMotion::LCM_Locked;

	UPROPERTY(EditAnywhere, Config, Category="Physics|Constraints|Linear")
	TEnumAsByte<ELinearConstraintMotion> LinearZ = ELinearConstraintMotion::LCM_Locked;

	/** Unified linear limit size (cm). Chaos uses a single radius-like limit. */
	UPROPERTY(EditAnywhere, Config, Category="Physics|Constraints|Linear", meta=(ClampMin="0.0", UIMin="0.0"))
	float LinearLimitSize = 0.0f;

	// Angular (rotation) motions
	UPROPERTY(EditAnywhere, Config, Category="Physics|Constraints|Angular")
	TEnumAsByte<EAngularConstraintMotion> TwistMotion = EAngularConstraintMotion::ACM_Limited;

	UPROPERTY(EditAnywhere, Config, Category="Physics|Constraints|Angular", meta=(ClampMin="0.0", ClampMax="180.0", UIMin="0.0", UIMax="180.0"))
	float TwistLimitDegrees = 15.0f;

	UPROPERTY(EditAnywhere, Config, Category="Physics|Constraints|Angular")
	TEnumAsByte<EAngularConstraintMotion> Swing1Motion = EAngularConstraintMotion::ACM_Limited;

	UPROPERTY(EditAnywhere, Config, Category="Physics|Constraints|Angular", meta=(ClampMin="0.0", ClampMax="180.0", UIMin="0.0", UIMax="180.0"))
	float Swing1LimitDegrees = 15.0f;

	UPROPERTY(EditAnywhere, Config, Category="Physics|Constraints|Angular")
	TEnumAsByte<EAngularConstraintMotion> Swing2Motion = EAngularConstraintMotion::ACM_Limited;

	UPROPERTY(EditAnywhere, Config, Category="Physics|Constraints|Angular", meta=(ClampMin="0.0", ClampMax="180.0", UIMin="0.0", UIMax="180.0"))
	float Swing2LimitDegrees = 15.0f;

	/** If true, disable all linear/angular drives for created constraints. */
	UPROPERTY(EditAnywhere, Config, Category="Physics|Drives")
	bool bDisableAllDrives = true;
};
