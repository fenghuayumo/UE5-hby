// Copyright Epic Games, Inc. All Rights Reserved.
#include "HLSLTree/HLSLTreeCommon.h"
#include "HLSLTree/HLSLTreeEmit.h"
#include "Shader/Preshader.h"

namespace UE
{
namespace HLSLTree
{

class FExpressionOperation : public FExpression
{
public:
	FExpressionOperation(EOperation InOp, TConstArrayView<FExpression*> InInputs);

	static constexpr int8 MaxInputs = 2;

	EOperation Op;
	FExpression* Inputs[MaxInputs];

	virtual void ComputeAnalyticDerivatives(FTree& Tree, FExpressionDerivatives& OutResult) const override;
	virtual FExpression* ComputePreviousFrame(FTree& Tree, const FRequestedType& RequestedType) const override;
	virtual bool PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const override;
	virtual void EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const override;
	virtual void EmitValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValuePreshaderResult& OutResult) const override;
};

FOperationDescription::FOperationDescription(const TCHAR* InName, const TCHAR* InOperator, int8 InNumInputs, Shader::EPreshaderOpcode InOpcode)
	: Name(InName), Operator(InOperator), NumInputs(InNumInputs), PreshaderOpcode(InOpcode)
{}

FOperationDescription::FOperationDescription()
	: Name(nullptr), Operator(nullptr), PreshaderOpcode(Shader::EPreshaderOpcode::Nop)
{}


FOperationDescription GetOperationDescription(EOperation Op)
{
	switch (Op)
	{
	case EOperation::None: return FOperationDescription(TEXT("None"), TEXT(""), 0, Shader::EPreshaderOpcode::Nop); break;

	// Unary
	case EOperation::Abs: return FOperationDescription(TEXT("Abs"), TEXT("abs"), 1, Shader::EPreshaderOpcode::Abs); break;
	case EOperation::Neg: return FOperationDescription(TEXT("Neg"), TEXT("-"), 1, Shader::EPreshaderOpcode::Neg); break;
	case EOperation::Rcp: return FOperationDescription(TEXT("Rcp"), TEXT("/"), 1, Shader::EPreshaderOpcode::Rcp); break;
	case EOperation::Sqrt: return FOperationDescription(TEXT("Sqrt"), TEXT("sqrt"), 1, Shader::EPreshaderOpcode::Sqrt); break;
	case EOperation::Rsqrt: return FOperationDescription(TEXT("Rsqrt"), TEXT("rsqrt"), 1, Shader::EPreshaderOpcode::Nop); break; // TODO
	case EOperation::Log2: return FOperationDescription(TEXT("Log2"), TEXT("log2"), 1, Shader::EPreshaderOpcode::Log2); break;
	case EOperation::Exp2: return FOperationDescription(TEXT("Exp2"), TEXT("exp2"), 1, Shader::EPreshaderOpcode::Nop); break; // TODO
	case EOperation::Frac: return FOperationDescription(TEXT("Frac"), TEXT("frac"), 1, Shader::EPreshaderOpcode::Frac); break;
	case EOperation::Floor: return FOperationDescription(TEXT("Floor"), TEXT("floor"), 1, Shader::EPreshaderOpcode::Floor); break;
	case EOperation::Ceil: return FOperationDescription(TEXT("Ceil"), TEXT("ceil"), 1, Shader::EPreshaderOpcode::Ceil); break;
	case EOperation::Round: return FOperationDescription(TEXT("Round"), TEXT("round"), 1, Shader::EPreshaderOpcode::Round); break;
	case EOperation::Trunc: return FOperationDescription(TEXT("Trunc"), TEXT("trunc"), 1, Shader::EPreshaderOpcode::Trunc); break;
	case EOperation::Saturate: return FOperationDescription(TEXT("Saturate"), TEXT("saturate"), 1, Shader::EPreshaderOpcode::Saturate); break;
	case EOperation::Sign: return FOperationDescription(TEXT("Sign"), TEXT("sign"), 1, Shader::EPreshaderOpcode::Sign); break;
	case EOperation::Length: return FOperationDescription(TEXT("Length"), TEXT("length"), 1, Shader::EPreshaderOpcode::Length); break;
	case EOperation::Normalize: return FOperationDescription(TEXT("Normalize"), TEXT("normalize"), 1, Shader::EPreshaderOpcode::Normalize); break;
	case EOperation::Sum: return FOperationDescription(TEXT("Sum"), TEXT("sum"), 1, Shader::EPreshaderOpcode::Nop); break; // TODO
	case EOperation::Sin: return FOperationDescription(TEXT("Sin"), TEXT("sin"), 1, Shader::EPreshaderOpcode::Sin); break;
	case EOperation::Cos: return FOperationDescription(TEXT("Cos"), TEXT("cos"), 1, Shader::EPreshaderOpcode::Cos); break;
	case EOperation::Tan: return FOperationDescription(TEXT("Tan"), TEXT("tan"), 1, Shader::EPreshaderOpcode::Tan); break;
	case EOperation::Asin: return FOperationDescription(TEXT("Asin"), TEXT("asin"), 1, Shader::EPreshaderOpcode::Asin); break;
	case EOperation::AsinFast: return FOperationDescription(TEXT("AsinFast"), TEXT("asinFast"), 1, Shader::EPreshaderOpcode::Asin); break;
	case EOperation::Acos: return FOperationDescription(TEXT("Acos"), TEXT("acos"), 1, Shader::EPreshaderOpcode::Acos); break;
	case EOperation::AcosFast: return FOperationDescription(TEXT("AcosFast"), TEXT("acosFast"), 1, Shader::EPreshaderOpcode::Acos); break;
	case EOperation::Atan: return FOperationDescription(TEXT("Atan"), TEXT("atan"), 1, Shader::EPreshaderOpcode::Atan); break;
	case EOperation::AtanFast: return FOperationDescription(TEXT("AtanFast"), TEXT("atanFast"), 1, Shader::EPreshaderOpcode::Atan); break;

	// Binary
	case EOperation::Add: return FOperationDescription(TEXT("Add"), TEXT("+"), 2, Shader::EPreshaderOpcode::Add); break;
	case EOperation::Sub: return FOperationDescription(TEXT("Subtract"), TEXT("-"), 2, Shader::EPreshaderOpcode::Sub); break;
	case EOperation::Mul: return FOperationDescription(TEXT("Multiply"), TEXT("*"), 2, Shader::EPreshaderOpcode::Mul); break;
	case EOperation::Div: return FOperationDescription(TEXT("Divide"), TEXT("/"), 2, Shader::EPreshaderOpcode::Div); break;
	case EOperation::Fmod: return FOperationDescription(TEXT("Fmod"), TEXT("%"), 2, Shader::EPreshaderOpcode::Fmod); break;
	case EOperation::PowPositiveClamped: return FOperationDescription(TEXT("PowPositiveClamped"), TEXT("PowPositiveClamped"), 2, Shader::EPreshaderOpcode::Nop); break;
	case EOperation::Atan2: return FOperationDescription(TEXT("Atan2"), TEXT("atan2"), 2, Shader::EPreshaderOpcode::Atan2); break;
	case EOperation::Atan2Fast: return FOperationDescription(TEXT("Atan2Fast"), TEXT("atan2Fast"), 2, Shader::EPreshaderOpcode::Atan2); break;
	case EOperation::Min: return FOperationDescription(TEXT("Min"), TEXT("min"), 2, Shader::EPreshaderOpcode::Min); break;
	case EOperation::Max: return FOperationDescription(TEXT("Max"), TEXT("max"), 2, Shader::EPreshaderOpcode::Max); break;
	case EOperation::Less: return FOperationDescription(TEXT("Less"), TEXT("<"), 2, Shader::EPreshaderOpcode::Less); break;
	case EOperation::Greater: return FOperationDescription(TEXT("Greater"), TEXT(">"), 2, Shader::EPreshaderOpcode::Greater); break;
	case EOperation::LessEqual: return FOperationDescription(TEXT("LessEqual"), TEXT("<="), 2, Shader::EPreshaderOpcode::Nop); break;
	case EOperation::GreaterEqual: return FOperationDescription(TEXT("GreaterEqual"), TEXT(">="), 2, Shader::EPreshaderOpcode::Nop); break;
	case EOperation::VecMulMatrix3: return FOperationDescription(TEXT("VecMulMatrix3"), TEXT("mul"), 2, Shader::EPreshaderOpcode::Nop); break;
	case EOperation::VecMulMatrix4: return FOperationDescription(TEXT("VecMulMatrix4"), TEXT("mul"), 2, Shader::EPreshaderOpcode::Nop); break;
	case EOperation::Matrix3MulVec: return FOperationDescription(TEXT("Matrix3MulVec"), TEXT("mul"), 2, Shader::EPreshaderOpcode::Nop); break;
	case EOperation::Matrix4MulVec: return FOperationDescription(TEXT("Matrix4MulVec"), TEXT("mul"), 2, Shader::EPreshaderOpcode::Nop); break;
	default: checkNoEntry(); return FOperationDescription();
	}
}

FExpression* FTree::NewUnaryOp(EOperation Op, FExpression* Input)
{
	FExpression* Inputs[1] = { Input };
	return NewExpression<FExpressionOperation>(Op, Inputs);
}

FExpression* FTree::NewBinaryOp(EOperation Op, FExpression* Lhs, FExpression* Rhs)
{
	FExpression* Inputs[2] = { Lhs, Rhs };
	return NewExpression<FExpressionOperation>(Op, Inputs);
}

FExpression* FTree::NewCross(FExpression* Lhs, FExpression* Rhs)
{
	//c_P[0] = v_A[1] * v_B[2] - v_A[2] * v_B[1];
	//c_P[1] = -(v_A[0] * v_B[2] - v_A[2] * v_B[0]);
	//c_P[2] = v_A[0] * v_B[1] - v_A[1] * v_B[0];
	FExpression* Lhs0 = NewExpression<FExpressionSwizzle>(FSwizzleParameters(1, 0, 0), Lhs);
	FExpression* Lhs1 = NewExpression<FExpressionSwizzle>(FSwizzleParameters(2, 2, 1), Lhs);
	FExpression* Rhs0 = NewExpression<FExpressionSwizzle>(FSwizzleParameters(2, 2, 1), Rhs);
	FExpression* Rhs1 = NewExpression<FExpressionSwizzle>(FSwizzleParameters(1, 0, 0), Rhs);
	return NewSub(NewMul(NewMul(Lhs0, Rhs0), NewConstant(FVector3f(1.0f, -1.0f, 1.0f))), NewMul(Lhs1, Rhs1));
}

FExpressionOperation::FExpressionOperation(EOperation InOp, TConstArrayView<FExpression*> InInputs) : Op(InOp)
{
	const FOperationDescription OpDesc = GetOperationDescription(InOp);
	check(OpDesc.NumInputs == InInputs.Num());
	check(InInputs.Num() <= MaxInputs);

	for (int32 i = 0; i < OpDesc.NumInputs; ++i)
	{
		Inputs[i] = InInputs[i];
		check(Inputs[i]);
	}
}

namespace Private
{
struct FOperationRequestedTypes
{
	FRequestedType InputType[FExpressionOperation::MaxInputs];
	bool bIsMatrixOperation = false;
};
struct FOperationTypes
{
	Shader::EValueType InputType[FExpressionOperation::MaxInputs];
	FPreparedType ResultType;
	bool bIsLWC = false;
};

FOperationRequestedTypes GetOperationRequestedTypes(EOperation Op, const FRequestedType& RequestedType)
{
	const FOperationDescription OpDesc = GetOperationDescription(Op);
	FOperationRequestedTypes Types;
	for (int32 Index = 0; Index < OpDesc.NumInputs; ++Index)
	{
		Types.InputType[Index] = RequestedType;
	}
	switch (Op)
	{
	case EOperation::Length:
	case EOperation::Normalize:
	case EOperation::Sum:
		Types.InputType[0] = ERequestedType::Vector4;
		break;
	case EOperation::VecMulMatrix3:
		Types.bIsMatrixOperation = true;
		Types.InputType[0] = ERequestedType::Vector3;
		Types.InputType[1] = ERequestedType::Matrix4x4;
		break;
	case EOperation::VecMulMatrix4:
		Types.bIsMatrixOperation = true;
		Types.InputType[0] = ERequestedType::Vector3;
		Types.InputType[1] = ERequestedType::Matrix4x4;
		break;
	case EOperation::Matrix3MulVec:
	case EOperation::Matrix4MulVec:
		// No LWC for transpose matrices
		Types.bIsMatrixOperation = true;
		Types.InputType[0] = ERequestedType::Matrix4x4;
		Types.InputType[1] = ERequestedType::Vector3;
		break;
	default:
		break;
	}
	return Types;
}

FOperationTypes GetOperationTypes(EOperation Op, TConstArrayView<FExpression*> Inputs)
{
	FOperationTypes Types;

	if (Op == EOperation::VecMulMatrix3 ||
		Op == EOperation::VecMulMatrix4 ||
		Op == EOperation::Matrix3MulVec ||
		Op == EOperation::Matrix4MulVec)
	{
		FPreparedComponent IntermediateComponent;
		Shader::EValueComponentType IntermediateComponentType = Shader::EValueComponentType::Void;
		for (int32 Index = 0; Index < Inputs.Num(); ++Index)
		{
			const FPreparedType& InputType = Inputs[Index]->GetPreparedType();
			IntermediateComponent = CombineComponents(IntermediateComponent, InputType.GetMergedComponent());
			IntermediateComponentType = Shader::CombineComponentTypes(IntermediateComponentType, InputType.ValueComponentType);
		}

		switch (Op)
		{
		case EOperation::VecMulMatrix3:
			// No LWC for matrix3
			Types.InputType[0] = Shader::EValueType::Float3;
			Types.InputType[1] = Shader::EValueType::Float4x4;
			Types.ResultType = FPreparedType(Shader::EValueType::Float3, IntermediateComponent);
			break;
		case EOperation::VecMulMatrix4:
			Types.InputType[0] = Shader::MakeValueType(IntermediateComponentType, 3);
			Types.InputType[1] = Shader::MakeValueType(IntermediateComponentType, 16);
			Types.ResultType = FPreparedType(Shader::MakeValueType(IntermediateComponentType, 3), IntermediateComponent);
			break;
		case EOperation::Matrix3MulVec:
		case EOperation::Matrix4MulVec:
			// No LWC for transpose matrices
			Types.InputType[0] = Shader::EValueType::Float4x4;
			Types.InputType[1] = Shader::EValueType::Float3;
			Types.ResultType = FPreparedType(Shader::EValueType::Float3, IntermediateComponent);
			break;
		}
	}
	else
	{
		FPreparedType IntermediateType;
		for (int32 Index = 0; Index < Inputs.Num(); ++Index)
		{
			const FPreparedType& InputType = Inputs[Index]->GetPreparedType();
			IntermediateType = MergePreparedTypes(IntermediateType, InputType);
		}

		const Shader::EValueType IntermediateValueType = IntermediateType.GetType();
		for (int32 Index = 0; Index < Inputs.Num(); ++Index)
		{
			Types.InputType[Index] = IntermediateValueType;
		}
		Types.ResultType = IntermediateType;
		Types.bIsLWC = (IntermediateType.ValueComponentType == Shader::EValueComponentType::Double);

		switch (Op)
		{
		case EOperation::Length:
		case EOperation::Sum:
			Types.ResultType = FPreparedType(Shader::MakeValueType(IntermediateType.ValueComponentType, 1), IntermediateType.GetMergedComponent());
			break;
		case EOperation::Normalize:
		case EOperation::Rcp:
		case EOperation::Sqrt:
		case EOperation::Rsqrt:
		case EOperation::Sign:
		case EOperation::Tan:
		case EOperation::Asin:
		case EOperation::AsinFast:
		case EOperation::Acos:
		case EOperation::AcosFast:
		case EOperation::Atan:
		case EOperation::AtanFast:
			Types.ResultType = MakeNonLWCType(IntermediateType);
			break;
		case EOperation::Saturate:
		case EOperation::Frac:
			for (int32 Index = 0; Index < Types.ResultType.PreparedComponents.Num(); ++Index)
			{
				Types.ResultType.SetComponentBounds(Index, Shader::FComponentBounds(Shader::EComponentBound::Zero, Shader::EComponentBound::One));
			}
			Types.ResultType = MakeNonLWCType(IntermediateType);
			break;
		case EOperation::Sin:
		case EOperation::Cos:
			for (int32 Index = 0; Index < Types.ResultType.PreparedComponents.Num(); ++Index)
			{
				Types.ResultType.SetComponentBounds(Index, Shader::FComponentBounds(Shader::EComponentBound::NegOne, Shader::EComponentBound::One));
			}
			Types.ResultType = MakeNonLWCType(IntermediateType);
			break;
		case EOperation::Log2:
		case EOperation::Exp2:
			// No LWC support yet
			Types.InputType[0] = Shader::MakeNonLWCType(IntermediateValueType);
			Types.ResultType = MakeNonLWCType(IntermediateType);
			break;
		case EOperation::Less:
		case EOperation::Greater:
		case EOperation::LessEqual:
		case EOperation::GreaterEqual:
			Types.ResultType.ValueComponentType = Shader::EValueComponentType::Bool;
			break;
		case EOperation::Fmod:
			Types.InputType[1] = Shader::MakeNonLWCType(IntermediateValueType);
			Types.ResultType = MakeNonLWCType(IntermediateType);
			break;
		case EOperation::PowPositiveClamped:
		case EOperation::Atan2:
		case EOperation::Atan2Fast:
			// No LWC support yet
			Types.InputType[0] = Types.InputType[1] = Shader::MakeNonLWCType(IntermediateValueType);
			Types.ResultType = MakeNonLWCType(IntermediateType);
			break;
		case EOperation::Min:
			for (int32 Index = 0; Index < Types.ResultType.PreparedComponents.Num(); ++Index)
			{
				Types.ResultType.SetComponentBounds(Index, Shader::MinBound(Inputs[0]->GetPreparedType().GetComponentBounds(Index), Inputs[1]->GetPreparedType().GetComponentBounds(Index)));
			}
			break;
		case EOperation::Max:
			for (int32 Index = 0; Index < Types.ResultType.PreparedComponents.Num(); ++Index)
			{
				Types.ResultType.SetComponentBounds(Index, Shader::MaxBound(Inputs[0]->GetPreparedType().GetComponentBounds(Index), Inputs[1]->GetPreparedType().GetComponentBounds(Index)));
			}
			break;
		default:
			break;
		}
	}
	return Types;
}
} // namespace Private

void FExpressionOperation::ComputeAnalyticDerivatives(FTree& Tree, FExpressionDerivatives& OutResult) const
{
	// Operations with constant derivatives
	switch (Op)
	{
	case EOperation::Less:
	case EOperation::Greater:
	case EOperation::LessEqual:
	case EOperation::GreaterEqual:
	case EOperation::Floor:
	case EOperation::Ceil:
	case EOperation::Round:
	case EOperation::Trunc:
	case EOperation::Sign:
		OutResult.ExpressionDdx = OutResult.ExpressionDdy = Tree.NewConstant(0.0f);
		break;
	default:
		break;
	}

	if (OutResult.IsValid())
	{
		return;
	}

	const FOperationDescription OpDesc = GetOperationDescription(Op);
	FExpressionDerivatives InputDerivatives[MaxInputs];
	for (int32 Index = 0; Index < OpDesc.NumInputs; ++Index)
	{
		InputDerivatives[Index] = Tree.GetAnalyticDerivatives(Inputs[Index]);
		if (!InputDerivatives[Index].IsValid())
		{
			return;
		}
	}

	switch (Op)
	{
	case EOperation::Neg:
		OutResult.ExpressionDdx = Tree.NewNeg(InputDerivatives[0].ExpressionDdx);
		OutResult.ExpressionDdy = Tree.NewNeg(InputDerivatives[0].ExpressionDdy);
		break;
	case EOperation::Rcp:
	{
		FExpression* Result = Tree.NewRcp(Inputs[0]);
		FExpression* dFdA = Tree.NewNeg(Tree.NewMul(Result, Result));
		OutResult.ExpressionDdx = Tree.NewMul(dFdA, InputDerivatives[0].ExpressionDdx);
		OutResult.ExpressionDdy = Tree.NewMul(dFdA, InputDerivatives[0].ExpressionDdy);
		break;
	}
	case EOperation::Sqrt:
	{
		FExpression* dFdA = Tree.NewMul(Tree.NewRsqrt(Tree.NewMax(Inputs[0], Tree.NewConstant(0.00001f))), Tree.NewConstant(0.5f));
		OutResult.ExpressionDdx = Tree.NewMul(dFdA, InputDerivatives[0].ExpressionDdx);
		OutResult.ExpressionDdy = Tree.NewMul(dFdA, InputDerivatives[0].ExpressionDdy);
		break;
	}
	case EOperation::Rsqrt:
	{
		FExpression* dFdA = Tree.NewMul(Tree.NewMul(Tree.NewRsqrt(Inputs[0]), Tree.NewRcp(Inputs[0])), Tree.NewConstant(-0.5f));
		OutResult.ExpressionDdx = Tree.NewMul(dFdA, InputDerivatives[0].ExpressionDdx);
		OutResult.ExpressionDdy = Tree.NewMul(dFdA, InputDerivatives[0].ExpressionDdy);
		break;
	}
	case EOperation::Sum:
		OutResult.ExpressionDdx = Tree.NewSum(InputDerivatives[0].ExpressionDdx);
		OutResult.ExpressionDdy = Tree.NewSum(InputDerivatives[0].ExpressionDdy);
		break;
	case EOperation::Frac:
		OutResult = InputDerivatives[0];
		break;
	case EOperation::Sin:
	{
		FExpression* dFdA = Tree.NewCos(Inputs[0]);
		OutResult.ExpressionDdx = Tree.NewMul(dFdA, InputDerivatives[0].ExpressionDdx);
		OutResult.ExpressionDdy = Tree.NewMul(dFdA, InputDerivatives[0].ExpressionDdy);
		break;
	}
	case EOperation::Cos:
	{
		FExpression* dFdA = Tree.NewNeg(Tree.NewSin(Inputs[0]));
		OutResult.ExpressionDdx = Tree.NewMul(dFdA, InputDerivatives[0].ExpressionDdx);
		OutResult.ExpressionDdy = Tree.NewMul(dFdA, InputDerivatives[0].ExpressionDdy);
		break;
	}
	case EOperation::Tan:
	{
		FExpression* dFdA = Tree.NewRcp(Tree.NewPow2(Tree.NewCos(Inputs[0])));
		OutResult.ExpressionDdx = Tree.NewMul(dFdA, InputDerivatives[0].ExpressionDdx);
		OutResult.ExpressionDdy = Tree.NewMul(dFdA, InputDerivatives[0].ExpressionDdy);
		break;
	}
	case EOperation::Asin:
	case EOperation::AsinFast:
	{
		FExpression* dFdA = Tree.NewRsqrt(Tree.NewMax(Tree.NewSub(Tree.NewConstant(1.0f), Tree.NewPow2(Inputs[0])), Tree.NewConstant(0.00001f)));
		OutResult.ExpressionDdx = Tree.NewMul(dFdA, InputDerivatives[0].ExpressionDdx);
		OutResult.ExpressionDdy = Tree.NewMul(dFdA, InputDerivatives[0].ExpressionDdy);
		break;
	}
	case EOperation::Acos:
	case EOperation::AcosFast:
	{
		FExpression* dFdA = Tree.NewNeg(Tree.NewRsqrt(Tree.NewMax(Tree.NewSub(Tree.NewConstant(1.0f), Tree.NewPow2(Inputs[0])), Tree.NewConstant(0.00001f))));
		OutResult.ExpressionDdx = Tree.NewMul(dFdA, InputDerivatives[0].ExpressionDdx);
		OutResult.ExpressionDdy = Tree.NewMul(dFdA, InputDerivatives[0].ExpressionDdy);
		break;
	}
	case EOperation::Atan:
	case EOperation::AtanFast:
	{
		FExpression* dFdA = Tree.NewRcp(Tree.NewAdd(Tree.NewPow2(Inputs[0]), Tree.NewConstant(1.0f)));
		OutResult.ExpressionDdx = Tree.NewMul(dFdA, InputDerivatives[0].ExpressionDdx);
		OutResult.ExpressionDdy = Tree.NewMul(dFdA, InputDerivatives[0].ExpressionDdy);
		break;
	}
	case EOperation::Atan2:
	case EOperation::Atan2Fast:
	{
		FExpression* Denom = Tree.NewRcp(Tree.NewAdd(Tree.NewPow2(Inputs[0]), Tree.NewPow2(Inputs[1])));
		FExpression* dFdA = Tree.NewMul(Inputs[1], Denom);
		FExpression* dFdB = Tree.NewMul(Tree.NewNeg(Inputs[0]), Denom);
		OutResult.ExpressionDdx = Tree.NewAdd(Tree.NewMul(dFdA, InputDerivatives[0].ExpressionDdx), Tree.NewMul(dFdB, InputDerivatives[1].ExpressionDdx));
		OutResult.ExpressionDdy = Tree.NewAdd(Tree.NewMul(dFdA, InputDerivatives[0].ExpressionDdy), Tree.NewMul(dFdB, InputDerivatives[1].ExpressionDdy));
		break;
	}
	case EOperation::Length:
	case EOperation::Normalize:
	case EOperation::Abs:
	case EOperation::Saturate:
	case EOperation::PowPositiveClamped:
	case EOperation::Log2:
	case EOperation::Exp2:
		// TODO
		break;
	case EOperation::Add:
		OutResult.ExpressionDdx = Tree.NewAdd(InputDerivatives[0].ExpressionDdx, InputDerivatives[1].ExpressionDdx);
		OutResult.ExpressionDdy = Tree.NewAdd(InputDerivatives[0].ExpressionDdy, InputDerivatives[1].ExpressionDdy);
		break;
	case EOperation::Sub:
		OutResult.ExpressionDdx = Tree.NewSub(InputDerivatives[0].ExpressionDdx, InputDerivatives[1].ExpressionDdx);
		OutResult.ExpressionDdy = Tree.NewSub(InputDerivatives[0].ExpressionDdy, InputDerivatives[1].ExpressionDdy);
		break;
	case EOperation::Mul:
		OutResult.ExpressionDdx = Tree.NewAdd(Tree.NewMul(InputDerivatives[0].ExpressionDdx, Inputs[1]), Tree.NewMul(InputDerivatives[1].ExpressionDdx, Inputs[0]));
		OutResult.ExpressionDdy = Tree.NewAdd(Tree.NewMul(InputDerivatives[0].ExpressionDdy, Inputs[1]), Tree.NewMul(InputDerivatives[1].ExpressionDdy, Inputs[0]));
		break;
	case EOperation::Div:
	{
		FExpression* Denom = Tree.NewRcp(Tree.NewMul(Inputs[1], Inputs[1]));
		FExpression* dFdA = Tree.NewMul(Inputs[1], Denom);
		FExpression* dFdB = Tree.NewNeg(Tree.NewMul(Inputs[0], Denom));
		OutResult.ExpressionDdx = Tree.NewAdd(Tree.NewMul(dFdA, InputDerivatives[0].ExpressionDdx), Tree.NewMul(dFdB, InputDerivatives[1].ExpressionDdx));
		OutResult.ExpressionDdy = Tree.NewAdd(Tree.NewMul(dFdA, InputDerivatives[0].ExpressionDdy), Tree.NewMul(dFdB, InputDerivatives[1].ExpressionDdy));
		break;
	}
	case EOperation::Fmod:
		// Only valid when B derivatives are zero.
		// We can't really do anything meaningful in the non-zero case.
		OutResult = InputDerivatives[0];
		break;
	case EOperation::Min:
	{
		FExpression* Cond = Tree.NewLess(Inputs[0], Inputs[1]);
		OutResult.ExpressionDdx = Tree.NewExpression<FExpressionSelect>(Cond, InputDerivatives[0].ExpressionDdx, InputDerivatives[1].ExpressionDdx);
		OutResult.ExpressionDdy = Tree.NewExpression<FExpressionSelect>(Cond, InputDerivatives[0].ExpressionDdy, InputDerivatives[1].ExpressionDdy);
		break;
	}
	case EOperation::Max:
	{
		FExpression* Cond = Tree.NewGreater(Inputs[0], Inputs[1]);
		OutResult.ExpressionDdx = Tree.NewExpression<FExpressionSelect>(Cond, InputDerivatives[0].ExpressionDdx, InputDerivatives[1].ExpressionDdx);
		OutResult.ExpressionDdy = Tree.NewExpression<FExpressionSelect>(Cond, InputDerivatives[0].ExpressionDdy, InputDerivatives[1].ExpressionDdy);
		break;
	}
	case EOperation::VecMulMatrix3:
	case EOperation::VecMulMatrix4:
	case EOperation::Matrix3MulVec:
	case EOperation::Matrix4MulVec:
		// TODO
		OutResult.ExpressionDdx = OutResult.ExpressionDdy = Tree.NewConstant(FVector3f(0.0f, 0.0f, 0.0f));
		break;
	default:
		checkNoEntry();
		break;
	}
}

FExpression* FExpressionOperation::ComputePreviousFrame(FTree& Tree, const FRequestedType& RequestedType) const
{
	const Private::FOperationRequestedTypes RequestedTypes = Private::GetOperationRequestedTypes(Op, RequestedType);
	const FOperationDescription OpDesc = GetOperationDescription(Op);
	FExpression* PrevFrameInputs[MaxInputs];
	for (int32 Index = 0; Index < OpDesc.NumInputs; ++Index)
	{
		PrevFrameInputs[Index] = Tree.GetPreviousFrame(Inputs[Index], RequestedTypes.InputType[Index]);
	}

	return Tree.NewExpression<FExpressionOperation>(Op, MakeArrayView(PrevFrameInputs, OpDesc.NumInputs));
}

bool FExpressionOperation::PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	const FOperationDescription OpDesc = GetOperationDescription(Op);
	const Private::FOperationRequestedTypes RequestedTypes = Private::GetOperationRequestedTypes(Op, RequestedType);

	Shader::FValue ConstantInput[MaxInputs];
	bool bConstantZeroInput[MaxInputs] = { false };
	for (int32 Index = 0; Index < OpDesc.NumInputs; ++Index)
	{
		const FPreparedType& InputType = Context.PrepareExpression(Inputs[Index], Scope, RequestedTypes.InputType[Index]);
		if (InputType.IsVoid())
		{
			return false;
		}

		if (!InputType.IsNumeric())
		{
			return Context.Errors->AddError(TEXT("Invalid arithmetic between non-numeric types"));
		}
		
		const EExpressionEvaluation InputEvaluation = InputType.GetEvaluation(Scope, RequestedType);
		if (InputEvaluation == EExpressionEvaluation::Constant)
		{
			ConstantInput[Index] = Inputs[Index]->GetValueConstant(Context, Scope, RequestedType);
			bConstantZeroInput[Index] = ConstantInput[Index].IsZero();
		}
	}

	Private::FOperationTypes Types = Private::GetOperationTypes(Op, MakeArrayView(Inputs, OpDesc.NumInputs));
	if (OpDesc.PreshaderOpcode == Shader::EPreshaderOpcode::Nop)
	{
		// No preshader support
		Types.ResultType.SetEvaluation(EExpressionEvaluation::Shader);
	}

	switch (Op)
	{
	case EOperation::Mul:
		if (bConstantZeroInput[0] || bConstantZeroInput[1])
		{
			// X * 0 == 0
			Types.ResultType.SetEvaluation(EExpressionEvaluation::ConstantZero);
		}
		break;
	default:
		break;
	}

	return OutResult.SetType(Context, RequestedType, Types.ResultType);
}

void FExpressionOperation::EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	const FOperationDescription OpDesc = GetOperationDescription(Op);
	const Private::FOperationRequestedTypes RequestedTypes = Private::GetOperationRequestedTypes(Op, RequestedType);
	const Private::FOperationTypes Types = Private::GetOperationTypes(Op, MakeArrayView(Inputs, OpDesc.NumInputs));
	FEmitShaderExpression* InputValue[MaxInputs] = { nullptr };
	for (int32 Index = 0; Index < OpDesc.NumInputs; ++Index)
	{
		InputValue[Index] = Inputs[Index]->GetValueShader(Context, Scope, RequestedTypes.InputType[Index], Types.InputType[Index]);
	}

	const Shader::EValueType ResultType = Types.ResultType.GetType();
	check(Shader::IsNumericType(ResultType));

	switch (Op)
	{
	// Unary Ops
	case EOperation::Abs: OutResult.Code = Context.EmitExpression(Scope, ResultType, Types.bIsLWC ? TEXT("LWCAbs(%)") : TEXT("abs(%)"), InputValue[0]); break;
	case EOperation::Neg:
		if (Types.bIsLWC)
		{
			OutResult.Code = Context.EmitExpression(Scope, ResultType, TEXT("LWCNegate(%)"), InputValue[0]);
		}
		else
		{
			OutResult.Code = Context.EmitInlineExpression(Scope, ResultType, TEXT("(-%)"), InputValue[0]);
		}
		break;
	case EOperation::Rcp: OutResult.Code = Context.EmitExpression(Scope, ResultType, Types.bIsLWC ? TEXT("LWCRcp(%)") : TEXT("rcp(%)"), InputValue[0]); break;
	case EOperation::Sqrt: OutResult.Code = Context.EmitExpression(Scope, ResultType, Types.bIsLWC ? TEXT("LWCSqrt(%)") : TEXT("sqrt(%)"), InputValue[0]); break;
	case EOperation::Rsqrt: OutResult.Code = Context.EmitExpression(Scope, ResultType, Types.bIsLWC ? TEXT("LWCRsqrt(%)") : TEXT("rsqrt(%)"), InputValue[0]); break;
	case EOperation::Log2: OutResult.Code = Context.EmitExpression(Scope, ResultType, TEXT("log2(%)"), InputValue[0]); break;
	case EOperation::Exp2: OutResult.Code = Context.EmitExpression(Scope, ResultType, TEXT("exp2(%)"), InputValue[0]); break;
	case EOperation::Frac: OutResult.Code = Context.EmitExpression(Scope, ResultType, Types.bIsLWC ? TEXT("LWCFrac(%)") : TEXT("frac(%)"), InputValue[0]); break;
	case EOperation::Floor: OutResult.Code = Context.EmitExpression(Scope, ResultType, Types.bIsLWC ? TEXT("LWCFloor(%)") : TEXT("floor(%)"), InputValue[0]); break;
	case EOperation::Ceil: OutResult.Code = Context.EmitExpression(Scope, ResultType, Types.bIsLWC ? TEXT("LWCCeil(%)") : TEXT("ceil(%)"), InputValue[0]); break;
	case EOperation::Round: OutResult.Code = Context.EmitExpression(Scope, ResultType, Types.bIsLWC ? TEXT("LWCRound(%)") : TEXT("round(%)"), InputValue[0]); break;
	case EOperation::Trunc: OutResult.Code = Context.EmitExpression(Scope, ResultType, Types.bIsLWC ? TEXT("LWCTrunc(%)") : TEXT("trunc(%)"), InputValue[0]); break;
	case EOperation::Saturate: OutResult.Code = Context.EmitExpression(Scope, ResultType, Types.bIsLWC ? TEXT("LWCSaturate(%)") : TEXT("saturate(%)"), InputValue[0]); break;
	case EOperation::Sign: OutResult.Code = Context.EmitExpression(Scope, ResultType, Types.bIsLWC ? TEXT("LWCSign(%)") : TEXT("sign(%)"), InputValue[0]); break;
	case EOperation::Length: OutResult.Code = Context.EmitExpression(Scope, ResultType, Types.bIsLWC ? TEXT("LWCLength(%)") : TEXT("length(%)"), InputValue[0]); break;
	case EOperation::Normalize: OutResult.Code = Context.EmitExpression(Scope, ResultType, Types.bIsLWC ? TEXT("LWCNormalize(%)") : TEXT("normalize(%)"), InputValue[0]); break;
	case EOperation::Sum: OutResult.Code = Context.EmitExpression(Scope, ResultType, Types.bIsLWC ? TEXT("LWCVectorSum(%)") : TEXT("VectorSum(%)"), InputValue[0]); break;
	case EOperation::Sin: OutResult.Code = Context.EmitExpression(Scope, ResultType, Types.bIsLWC ? TEXT("LWCSin(%)") : TEXT("sin(%)"), InputValue[0]); break;
	case EOperation::Cos: OutResult.Code = Context.EmitExpression(Scope, ResultType, Types.bIsLWC ? TEXT("LWCCos(%)") : TEXT("cos(%)"), InputValue[0]); break;
	case EOperation::Tan: OutResult.Code = Context.EmitExpression(Scope, ResultType, Types.bIsLWC ? TEXT("LWCTan(%)") : TEXT("tan(%)"), InputValue[0]); break;
	case EOperation::Asin: OutResult.Code = Context.EmitExpression(Scope, ResultType, Types.bIsLWC ? TEXT("LWCAsin(%)") : TEXT("asin(%)"), InputValue[0]); break;
	case EOperation::AsinFast: OutResult.Code = Context.EmitExpression(Scope, ResultType, Types.bIsLWC ? TEXT("LWCAsin(%)") : TEXT("asinFast(%)"), InputValue[0]); break;
	case EOperation::Acos: OutResult.Code = Context.EmitExpression(Scope, ResultType, Types.bIsLWC ? TEXT("LWCAcos(%)") : TEXT("acos(%)"), InputValue[0]); break;
	case EOperation::AcosFast: OutResult.Code = Context.EmitExpression(Scope, ResultType, Types.bIsLWC ? TEXT("LWCAcos(%)") : TEXT("acosFast(%)"), InputValue[0]); break;
	case EOperation::Atan: OutResult.Code = Context.EmitExpression(Scope, ResultType, Types.bIsLWC ? TEXT("LWCAtan(%)") : TEXT("atan(%)"), InputValue[0]); break;
	case EOperation::AtanFast: OutResult.Code = Context.EmitExpression(Scope, ResultType, Types.bIsLWC ? TEXT("LWCAtan(%)") : TEXT("atanFast(%)"), InputValue[0]); break;
	
	// Binary Ops
	case EOperation::Add: OutResult.Code = Context.EmitExpression(Scope, ResultType, Types.bIsLWC ? TEXT("LWCAdd(%, %)") : TEXT("(% + %)"), InputValue[0], InputValue[1]); break;
	case EOperation::Sub: OutResult.Code = Context.EmitExpression(Scope, ResultType, Types.bIsLWC ? TEXT("LWCSubtract(%, %)") : TEXT("(% - %)"), InputValue[0], InputValue[1]); break;
	case EOperation::Mul: OutResult.Code = Context.EmitExpression(Scope, ResultType, Types.bIsLWC ? TEXT("LWCMultiply(%, %)") : TEXT("(% * %)"), InputValue[0], InputValue[1]); break;
	case EOperation::Div: OutResult.Code = Context.EmitExpression(Scope, ResultType, Types.bIsLWC ? TEXT("LWCDivide(%, %)") : TEXT("(% / %)"), InputValue[0], InputValue[1]); break;
	case EOperation::Fmod: OutResult.Code = Context.EmitExpression(Scope, ResultType, Types.bIsLWC ? TEXT("LWCFmod(%, %)") : TEXT("fmod(%, %)"), InputValue[0], InputValue[1]); break;
	case EOperation::PowPositiveClamped: OutResult.Code = Context.EmitExpression(Scope, ResultType, TEXT("PositiveClampedPow(%, %)"), InputValue[0], InputValue[1]); break;
	case EOperation::Atan2: OutResult.Code = Context.EmitExpression(Scope, ResultType, TEXT("atan2(%, %)"), InputValue[0], InputValue[1]); break;
	case EOperation::Atan2Fast: OutResult.Code = Context.EmitExpression(Scope, ResultType, TEXT("atan2Fast(%, %)"), InputValue[0], InputValue[1]); break;
	case EOperation::Min: OutResult.Code = Context.EmitExpression(Scope, ResultType, Types.bIsLWC ? TEXT("LWCMin(%, %)") : TEXT("min(%, %)"), InputValue[0], InputValue[1]); break;
	case EOperation::Max: OutResult.Code = Context.EmitExpression(Scope, ResultType, Types.bIsLWC ? TEXT("LWCMax(%, %)") : TEXT("max(%, %)"), InputValue[0], InputValue[1]); break;
	case EOperation::Less: OutResult.Code = Context.EmitExpression(Scope, ResultType, Types.bIsLWC ? TEXT("LWCLess(%, %)") : TEXT("(% < %)"), InputValue[0], InputValue[1]); break;
	case EOperation::Greater: OutResult.Code = Context.EmitExpression(Scope, ResultType, Types.bIsLWC ? TEXT("LWCGreater(%, %)") : TEXT("(% > %)"), InputValue[0], InputValue[1]); break;
	case EOperation::LessEqual: OutResult.Code = Context.EmitExpression(Scope, ResultType, Types.bIsLWC ? TEXT("LWCLessEqual(%, %)") : TEXT("(% <= %)"), InputValue[0], InputValue[1]); break;
	case EOperation::GreaterEqual: OutResult.Code = Context.EmitExpression(Scope, ResultType, Types.bIsLWC ? TEXT("LWCGreaterEqual(%, %)") : TEXT("(% >= %)"), InputValue[0], InputValue[1]); break;
	case EOperation::VecMulMatrix3:
		if (Types.bIsLWC)
		{
			OutResult.Code = Context.EmitExpression(Scope, ResultType, TEXT("LWCMultiply(%, %)"), InputValue[0], InputValue[1]);
		}
		else
		{
			OutResult.Code = Context.EmitExpression(Scope, ResultType, TEXT("mul(%, (float3x3)%)"), InputValue[0], InputValue[1]);
		}
		break;
	case EOperation::VecMulMatrix4:
		if (Types.bIsLWC)
		{
			OutResult.Code = Context.EmitExpression(Scope, ResultType, TEXT("LWCMultiply(%, %)"), InputValue[0], InputValue[1]);
		}
		else
		{
			OutResult.Code = Context.EmitExpression(Scope, ResultType, TEXT("mul(%, %)"), InputValue[0], InputValue[1]);
		}
		break;
	case EOperation::Matrix3MulVec:
		OutResult.Code = Context.EmitExpression(Scope, ResultType, TEXT("mul((float3x3)%, %)"), InputValue[0], InputValue[1]);
		break;
	case EOperation::Matrix4MulVec:
		OutResult.Code = Context.EmitExpression(Scope, ResultType, TEXT("mul(%, %)"), InputValue[0], InputValue[1]);
		break;
	default:
		checkNoEntry();
		break;
	}
}

void FExpressionOperation::EmitValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValuePreshaderResult& OutResult) const
{
	const FOperationDescription OpDesc = GetOperationDescription(Op);
	const Private::FOperationRequestedTypes RequestedTypes = Private::GetOperationRequestedTypes(Op, RequestedType);
	const Private::FOperationTypes Types = Private::GetOperationTypes(Op, MakeArrayView(Inputs, OpDesc.NumInputs));
	check(OpDesc.PreshaderOpcode != Shader::EPreshaderOpcode::Nop);

	for (int32 Index = 0; Index < OpDesc.NumInputs; ++Index)
	{
		Inputs[Index]->GetValuePreshader(Context, Scope, RequestedTypes.InputType[Index], OutResult.Preshader);
	}

	const int32 NumInputsToPop = OpDesc.NumInputs - 1;
	if (NumInputsToPop > 0)
	{
		check(Context.PreshaderStackPosition >= NumInputsToPop);
		Context.PreshaderStackPosition -= NumInputsToPop;
	}

	OutResult.Preshader.WriteOpcode(OpDesc.PreshaderOpcode);
	OutResult.Type = Types.ResultType.GetType();
}

} // namespace HLSLTree
} // namespace UE
