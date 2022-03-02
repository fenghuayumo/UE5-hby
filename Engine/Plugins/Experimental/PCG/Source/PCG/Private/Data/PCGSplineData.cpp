// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/PCGSplineData.h"

#include "PCGHelpers.h"
#include "Data/PCGPointData.h"
#include "Data/PCGSurfaceData.h"

#include "Components/SplineComponent.h"

void UPCGSplineData::Initialize(USplineComponent* InSpline)
{
	check(InSpline);
	Spline = InSpline;
	TargetActor = InSpline->GetOwner();

	CachedBounds = PCGHelpers::GetActorBounds(Spline->GetOwner());

	// Expand bounds by the radius of points, otherwise sections of the curve that are close
	// to the bounds will report an invalid density.
	FVector SplinePointsRadius = FVector::ZeroVector;
	const FInterpCurveVector& SplineScales = Spline->GetSplinePointsScale();
	for (const FInterpCurvePoint<FVector>& SplineScale : SplineScales.Points)
	{
		SplinePointsRadius = FVector::Max(SplinePointsRadius, SplineScale.OutVal.GetAbs());
	}

	CachedBounds = CachedBounds.ExpandBy(SplinePointsRadius, SplinePointsRadius);
}

int UPCGSplineData::GetNumSegments() const
{
	return 1;
}

float UPCGSplineData::GetSegmentLength(int SegmentIndex) const
{
	return Spline->GetSplineLength();
}

FVector UPCGSplineData::GetLocationAtDistance(int SegmentIndex, float Distance) const
{
	return Spline->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
}

const UPCGPointData* UPCGSplineData::CreatePointData() const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPCGSplineData::CreatePointData);
	UPCGPointData* Data = NewObject<UPCGPointData>(const_cast<UPCGSplineData*>(this));
	Data->TargetActor = TargetActor;

	// TODO: introduce settings to sample, 
	// should be part of the settings passed in param.
	float SplineLength = Spline->GetSplineLength();
	float Offset = (SplineLength - FMath::Floor(SplineLength)) / 2.0f;

	TArray<FPCGPoint>& Points = Data->GetMutablePoints();
	int NumPoints = int(SplineLength) + 1;
	Points.SetNum(NumPoints);

	int Index = 0;
	float Distance = Offset;
	while (Distance < SplineLength)
	{
		Points[Index].Transform = Spline->GetTransformAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
		// Exception: considering the spline holds size data in its scale, we'll reset it to identity here
		Points[Index].Transform.SetScale3D(FVector::One());
		Points[Index].Seed = PCGHelpers::ComputeSeed((int)Distance);
		Points[Index].Density = 1.0f;
		++Index;
		Distance += 1.0f;
	}

	UE_LOG(LogPCG, Verbose, TEXT("Spline %s generated %d points"), *Spline->GetFName().ToString(), Points.Num());

	return Data;
}

FBox UPCGSplineData::GetBounds() const
{
	return CachedBounds;
}

float UPCGSplineData::GetDensityAtPosition(const FVector& InPosition) const
{
	// Find nearest point on spline
	float NearestPointKey = Spline->FindInputKeyClosestToWorldLocation(InPosition);

	FTransform NearestTransform = Spline->GetTransformAtSplineInputKey(NearestPointKey, ESplineCoordinateSpace::World, true);

	FVector LocalPoint = NearestTransform.InverseTransformPosition(InPosition);

	// Linear fall off based on the distance to the nearest point
	// TODO: should be based on explicit settings
	float Distance = LocalPoint.Length();
	return FMath::Max(0, 1.0f - Distance);
}

UPCGProjectionData* UPCGSplineData::ProjectOn(const UPCGSpatialData* InOther) const
{
	if(InOther->GetDimension() == 2)
	{
		UPCGSplineProjectionData* SplineProjectionData = NewObject<UPCGSplineProjectionData>(const_cast<UPCGSplineData*>(this));
		SplineProjectionData->Initialize(this, InOther);
		return SplineProjectionData;
	}
	else
	{
		return Super::ProjectOn(InOther);
	}
}

FVector2D UPCGSplineProjectionData::Project(const FVector& InVector) const
{
	const FVector& SurfaceNormal = GetSurface()->GetNormal();
	FVector Projection = InVector - InVector.ProjectOnToNormal(SurfaceNormal);

	// Ignore smallest absolute coordinate value.
	// Normally, one should be zero, but because of numerical precision,
	// We'll make sure we're doing it right
	FVector::FReal SmallestCoordinate = TNumericLimits<FVector::FReal>::Max();
	int SmallestCoordinateAxis = -1;

	for (int Axis = 0; Axis < 3; ++Axis)
	{
		FVector::FReal AbsoluteCoordinateValue = FMath::Abs(Projection[Axis]);
		if (AbsoluteCoordinateValue < SmallestCoordinate)
		{
			SmallestCoordinate = AbsoluteCoordinateValue;
			SmallestCoordinateAxis = Axis;
		}
	}

	FVector2D Projection2D;
	int AxisIndex = 0;
	for (int Axis = 0; Axis < 3; ++Axis)
	{
		if (Axis != SmallestCoordinateAxis)
		{
			Projection2D[AxisIndex++] = Projection[Axis];
		}
	}

	return Projection2D;
}

void UPCGSplineProjectionData::Initialize(const UPCGSplineData* InSourceSpline, const UPCGSpatialData* InTargetSurface)
{
	Super::Initialize(InSourceSpline, InTargetSurface);

	const USplineComponent* Spline = GetSpline()->Spline.Get();
	const FVector& SurfaceNormal = GetSurface()->GetNormal();

	if (Spline)
	{
		const FInterpCurveVector& SplinePosition = Spline->GetSplinePointsPosition();

		// Build projected spline data
		ProjectedPosition.bIsLooped = SplinePosition.bIsLooped;
		ProjectedPosition.LoopKeyOffset = SplinePosition.LoopKeyOffset;

		ProjectedPosition.Points.Reserve(SplinePosition.Points.Num());

		for (const FInterpCurvePoint<FVector>& SplinePoint : SplinePosition.Points)
		{
			FInterpCurvePoint<FVector2D>& ProjectedPoint = ProjectedPosition.Points.Emplace_GetRef();

			ProjectedPoint.InVal = SplinePoint.InVal;
			ProjectedPoint.OutVal = Project(SplinePoint.OutVal);
			// TODO: correct tangent if it becomes null
			ProjectedPoint.ArriveTangent = Project(SplinePoint.ArriveTangent).GetSafeNormal();
			ProjectedPoint.LeaveTangent = Project(SplinePoint.LeaveTangent).GetSafeNormal();
			ProjectedPoint.InterpMode = SplinePoint.InterpMode;
		}
	}
}

float UPCGSplineProjectionData::GetDensityAtPosition(const FVector& InPosition) const
{
	// Find nearest point on projected spline
	const USplineComponent* Spline = GetSpline()->Spline.Get();
	const FVector& SurfaceNormal = GetSurface()->GetNormal();

	// Project input point to 2D space
	FVector LocalPosition = Spline->GetComponentTransform().InverseTransformPosition(InPosition);
	FVector2D LocalPosition2D = Project(LocalPosition);
	float Dummy;
	// Find nearest key on 2D spline	
	float NearestInputKey = ProjectedPosition.InaccurateFindNearest(LocalPosition2D, Dummy);
	// TODO: if we didn't want to hand off density computation to the spline and do it here instead, we could do it in 2D space.
	// Find point on original spline using the previously found key
	// Note: this is an approximation that might not hold true since we are changing the curve length
	const FVector NearestPointOnSpline = Spline->GetLocationAtSplineInputKey(NearestInputKey, ESplineCoordinateSpace::World);
	const FVector PointOnLine = FMath::ClosestPointOnInfiniteLine(InPosition, InPosition + SurfaceNormal, NearestPointOnSpline);

	return GetSpline()->GetDensityAtPosition(PointOnLine);
}

const UPCGSplineData* UPCGSplineProjectionData::GetSpline() const
{
	return Cast<const UPCGSplineData>(Source);
}

const UPCGSpatialData* UPCGSplineProjectionData::GetSurface() const
{
	return Target;
}