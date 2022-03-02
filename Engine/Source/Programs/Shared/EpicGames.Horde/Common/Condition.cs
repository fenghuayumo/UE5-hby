// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Serialization;
using EpicGames.Serialization.Converters;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Text.RegularExpressions;

namespace EpicGames.Horde.Common
{
	enum TokenType : byte
	{
		Error,
		End,
		Identifier,
		Scalar,
		Not,
		Eq,
		Neq,
		LogicalOr,
		LogicalAnd,
		Lt,
		Lte,
		Gt,
		Gte,
		Lparen,
		Rparen,
		Regex,
		True,
		False,
	}

	class TokenReader
	{
		public string Input { get; private set; }
		public int Offset { get; private set; }
		public int Length { get; private set; }

		public StringView Token => new StringView(Input, Offset, Length);
		public TokenType Type { get; private set; }
		public string? Scalar { get; private set; }

		public TokenReader(string Input)
		{
			this.Input = Input;
			MoveNext();
		}

		private void SetCurrent(int Length, TokenType Type, string? Scalar = null)
		{
			this.Length = Length;
			this.Type = Type;
			this.Scalar = Scalar;
		}

		public void MoveNext()
		{
			Offset += Length;

			while (Offset < Input.Length)
			{
				switch (Input[Offset])
				{
					case ' ':
					case '\t':
						Offset++;
						break;
					case '(':
						SetCurrent(1, TokenType.Lparen);
						return;
					case ')':
						SetCurrent(1, TokenType.Rparen);
						return;
					case '!':
						if (Offset + 1 < Input.Length && Input[Offset + 1] == '=')
						{
							SetCurrent(2, TokenType.Neq);
						}
						else
						{
							SetCurrent(1, TokenType.Not);
						}
						return;
					case '&':
						if (RequireCharacter(Input, Offset + 1, '&'))
						{
							SetCurrent(2, TokenType.LogicalAnd);
						}
						return;
					case '|':
						if (RequireCharacter(Input, Offset + 1, '|'))
						{
							SetCurrent(2, TokenType.LogicalOr);
						}
						return;
					case '=':
						if (RequireCharacter(Input, Offset + 1, '='))
						{
							SetCurrent(2, TokenType.Eq);
						}
						return;
					case '~':
						if (RequireCharacter(Input, Offset + 1, '='))
						{
							SetCurrent(2, TokenType.Regex);
						}
						return;
					case '>':
						if (Offset + 1 < Input.Length && Input[Offset + 1] == '=')
						{
							SetCurrent(2, TokenType.Gte);
						}
						else
						{
							SetCurrent(1, TokenType.Gt);
						}
						return;
					case '<':
						if (Offset + 1 < Input.Length && Input[Offset + 1] == '=')
						{
							SetCurrent(2, TokenType.Lte);
						}
						else
						{
							SetCurrent(1, TokenType.Lt);
						}
						return;
					case '\"':
					case '\'':
						ParseString();
						return;
					case char Char when IsNumber(Char):
						ParseNumber();
						return;
					case char Char when IsIdentifier(Char):
						int EndIdx = Offset + 1;
						while (EndIdx < Input.Length && IsIdentifierTail(Input[EndIdx]))
						{
							EndIdx++;
						}

						TokenType Type;
						if (EndIdx == Offset + 4 && String.Compare(Input, Offset, "true", 0, 4, StringComparison.OrdinalIgnoreCase) == 0)
						{
							Type = TokenType.True;
						}
						else if (EndIdx == Offset + 5 && String.Compare(Input, Offset, "false", 0, 5, StringComparison.OrdinalIgnoreCase) == 0)
						{
							Type = TokenType.False;
						}
						else
						{
							Type = TokenType.Identifier;
						}

						SetCurrent(EndIdx - Offset, Type);
						return;
					default:
						SetCurrent(1, TokenType.Error, $"Invalid character at offset {Offset}: '{Input[Offset]}'");
						return;
				}
			}

			SetCurrent(0, TokenType.End);
		}

		bool ParseString()
		{
			char QuoteChar = Input[Offset];
			if (QuoteChar != '\'' && QuoteChar != '\"')
			{
				SetCurrent(1, TokenType.Error, $"Invalid quote character '{(char)QuoteChar}' at offset {Offset}");
				return false;
			}

			int NumEscapeChars = 0;

			int EndIdx = Offset + 1;
			for (; ; EndIdx++)
			{
				if (EndIdx >= Input.Length)
				{
					SetCurrent(EndIdx - Offset, TokenType.Error, "Unterminated string in expression");
					return false;
				}
				else if (Input[EndIdx] == '\\')
				{
					NumEscapeChars++;
					EndIdx++;
				}
				else if (Input[EndIdx] == QuoteChar)
				{
					break;
				}
			}

			char[] Copy = new char[EndIdx - (Offset + 1) - NumEscapeChars];

			int InputIdx = Offset + 1;
			int OutputIdx = 0;
			while (OutputIdx < Copy.Length)
			{
				if (Input[InputIdx] == '\\')
				{
					InputIdx++;
				}
				Copy[OutputIdx++] = Input[InputIdx++];
			}

			SetCurrent(EndIdx + 1 - Offset, TokenType.Scalar, new string(Copy));
			return true;
		}

		static Dictionary<StringView, ulong> SizeSuffixes = new Dictionary<StringView, ulong>(StringViewComparer.OrdinalIgnoreCase)
		{
			["kb"] = 1024UL,
			["Mb"] = 1024UL * 1024,
			["Gb"] = 1024UL * 1024 * 1024,
			["Tb"] = 1024UL * 1024 * 1024 * 1024,
		};

		bool ParseNumber()
		{
			ulong Value = 0;

			int EndIdx = Offset;
			while (EndIdx < Input.Length && IsNumber(Input[EndIdx]))
			{
				Value = (Value * 10) + (uint)(Input[EndIdx] - '0');
				EndIdx++;
			}

			if (EndIdx < Input.Length && IsIdentifier(Input[EndIdx]))
			{
				int Offset = EndIdx++;
				while (EndIdx < Input.Length && IsIdentifierTail(Input[EndIdx]))
				{
					EndIdx++;
				}

				StringView Suffix = new StringView(Input, Offset, EndIdx - Offset);

				ulong Size;
				if (!SizeSuffixes.TryGetValue(Suffix, out Size))
				{
					SetCurrent((EndIdx + 1) - Offset, TokenType.Error, $"'{Suffix}' is not a valid numerical suffix");
					return false;
				}
				Value *= Size;
			}

			SetCurrent(EndIdx - Offset, TokenType.Scalar, Value.ToString(CultureInfo.InvariantCulture));
			return true;
		}

		bool RequireCharacter(string Text, int Idx, char Character)
		{
			if (Idx == Text.Length || Text[Idx] != Character)
			{
				SetCurrent(1, TokenType.Error, $"Invalid character at position {Idx}; expected '{Character}'.");
				return false;
			}
			return true;
		}

		static bool IsNumber(char Character)
		{
			return Character >= '0' && Character <= '9';
		}

		static bool IsIdentifier(char Character)
		{
			return (Character >= 'a' && Character <= 'z') || (Character >= 'A' && Character <= 'Z') || Character == '_';
		}

		static bool IsIdentifierTail(char Character)
		{
			return IsIdentifier(Character) || IsNumber(Character) || Character == '-' || Character == '.';
		}
	}

	/// <summary>
	/// Exception thrown when a condition is not valid
	/// </summary>
	public class ConditionException : Exception
	{
		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Error"></param>
		internal ConditionException(string Error)
			: base(Error)
		{
		}
	}

	/// <summary>
	/// A conditional expression that can be evaluated against a particular object
	/// </summary>
	[JsonSchemaString]
	[TypeConverter(typeof(ConditionTypeConverter))]
	[CbConverter(typeof(ConditionCbConverter))]
	[JsonConverter(typeof(ConditionJsonConverter))]
	public class Condition
	{
		[DebuggerDisplay("{Type}")]
		readonly struct Token
		{
			public readonly TokenType Type;
			public readonly byte Index;

			public Token(TokenType Type, int Index)
			{
				this.Type = Type;
				this.Index = (byte)Index;
			}
		}

		/// <summary>
		/// The condition text
		/// </summary>
		public string Text { get; }

		/// <summary>
		/// Error produced when parsing the condition
		/// </summary>
		public string? Error { get; private set; }
		List<Token> Tokens = new List<Token>();
		List<string> Strings = new List<string>();

		private Condition(string Text)
		{
			this.Text = Text;

			TokenReader Reader = new TokenReader(Text);
			if (Reader.Type != TokenType.End)
			{
				Error = ParseOrExpr(Reader);

				if (Reader.Type == TokenType.Error)
				{
					Error = Reader.Scalar;
				}
				else if (Error == null && Reader.Type != TokenType.End)
				{
					Error = $"Unexpected token at offset {Reader.Offset}: {Reader.Token}";
				}
			}
		}

		/// <summary>
		/// Determines if the condition is empty
		/// </summary>
		/// <returns>True if the condition is empty</returns>
		public bool IsEmpty() => Tokens.Count == 0 && IsValid();

		/// <summary>
		/// Checks if the condition has been parsed correctly
		/// </summary>
		public bool IsValid() => Error == null;

		/// <summary>
		/// Parse the given text as a condition
		/// </summary>
		/// <param name="Text">Condition text to parse</param>
		/// <returns>The new condition object</returns>
		public static Condition Parse(string Text)
		{
			Condition Condition = new Condition(Text);
			if(!Condition.IsValid())
			{
				throw new ConditionException(Condition.Error!);
			}
			return Condition;
		}

		/// <summary>
		/// Attempts to parse the given text as a condition
		/// </summary>
		/// <param name="Text">Condition to parse</param>
		/// <returns>The parsed condition. Does not validate whether the parse completed successfully; call <see cref="Condition.IsValid"/> to verify.</returns>
		public static Condition TryParse(string Text)
		{
			return new Condition(Text);
		}

		string? ParseOrExpr(TokenReader Reader)
		{
			int StartCount = Tokens.Count;

			string? Error = ParseAndExpr(Reader);
			while (Error == null && Reader.Type == TokenType.LogicalOr)
			{
				Tokens.Insert(StartCount++, new Token(TokenType.LogicalOr, 0));
				Reader.MoveNext();
				Error = ParseAndExpr(Reader);
			}

			return Error;
		}

		string? ParseAndExpr(TokenReader Reader)
		{
			int StartCount = Tokens.Count;

			string? Error = ParseBooleanExpr(Reader);
			while (Error == null && Reader.Type == TokenType.LogicalAnd)
			{
				Tokens.Insert(StartCount++, new Token(TokenType.LogicalAnd, 0));
				Reader.MoveNext();
				Error = ParseBooleanExpr(Reader);
			}

			return Error;
		}

		string? ParseBooleanExpr(TokenReader Reader)
		{
			switch (Reader.Type)
			{
				case TokenType.Not:
					Tokens.Add(new Token(Reader.Type, 0));
					Reader.MoveNext();
					if (Reader.Type != TokenType.Lparen)
					{
						return $"Expected '(' at offset {Reader.Offset}";
					}
					return ParseSubExpr(Reader);
				case TokenType.Lparen:
					return ParseSubExpr(Reader);
				case TokenType.True:
				case TokenType.False:
					Tokens.Add(new Token(Reader.Type, 0));
					Reader.MoveNext();
					return null;
				default:
					return ParseComparisonExpr(Reader);
			}
		}

		string? ParseSubExpr(TokenReader Reader)
		{
			Reader.MoveNext();

			string? Error = ParseOrExpr(Reader);
			if (Error == null)
			{
				if (Reader.Type == TokenType.Rparen)
				{
					Reader.MoveNext();
				}
				else
				{
					Error = $"Missing ')' at offset {Reader.Offset}";
				}
			}
			return Error;
		}

		string? ParseComparisonExpr(TokenReader Reader)
		{
			int StartCount = Tokens.Count;

			string? Error = ParseScalarExpr(Reader);
			if (Error == null)
			{
				switch(Reader.Type)
				{
					case TokenType.Lt:
					case TokenType.Lte:
					case TokenType.Gt:
					case TokenType.Gte:
					case TokenType.Eq:
					case TokenType.Neq:
					case TokenType.Regex:
						Tokens.Insert(StartCount, new Token(Reader.Type, 0));
						Reader.MoveNext();
						Error = ParseScalarExpr(Reader);
						break;
				}
			}

			return Error;
		}

		string? ParseScalarExpr(TokenReader Reader)
		{
			switch (Reader.Type)
			{
				case TokenType.Identifier:
					Strings.Add(Reader.Token.ToString());
					Tokens.Add(new Token(TokenType.Identifier, Strings.Count - 1));
					Reader.MoveNext();
					return null;
				case TokenType.Scalar:
					Strings.Add(Reader.Scalar!);
					Tokens.Add(new Token(TokenType.Scalar, Strings.Count - 1));
					Reader.MoveNext();
					return null;
				default:
					return $"Unexpected token '{Reader.Token}' at offset {Reader.Offset}";
			}
		}

		/// <summary>
		/// Evaluate the condition using the given callback to retreive property values
		/// </summary>
		/// <param name="GetPropertyValues"></param>
		/// <returns></returns>
		public bool Evaluate(Func<string, IEnumerable<string>> GetPropertyValues)
		{
			if (IsEmpty())
			{
				return true;
			}

			int Idx = 0;
			return IsValid() && EvaluateCondition(ref Idx, GetPropertyValues);
		}

		bool EvaluateCondition(ref int Idx, Func<string, IEnumerable<string>> GetPropertyValues)
		{
			bool LhsBool;
			bool RhsBool;
			IEnumerable<string> LhsScalar;
			IEnumerable<string> RhsScalar;

			Token Token = Tokens[Idx++];
			switch (Token.Type)
			{
				case TokenType.True:
					return true;
				case TokenType.False:
					return false;
				case TokenType.Not:
					return !EvaluateCondition(ref Idx, GetPropertyValues);
				case TokenType.Eq:
					LhsScalar = EvaluateScalar(ref Idx, GetPropertyValues);
					RhsScalar = EvaluateScalar(ref Idx, GetPropertyValues);
					return LhsScalar.Any(x => RhsScalar.Contains(x, StringComparer.OrdinalIgnoreCase));
				case TokenType.Neq:
					LhsScalar = EvaluateScalar(ref Idx, GetPropertyValues);
					RhsScalar = EvaluateScalar(ref Idx, GetPropertyValues);
					return !LhsScalar.Any(x => RhsScalar.Contains(x, StringComparer.OrdinalIgnoreCase));
				case TokenType.LogicalOr:
					LhsBool = EvaluateCondition(ref Idx, GetPropertyValues);
					RhsBool = EvaluateCondition(ref Idx, GetPropertyValues);
					return LhsBool || RhsBool;
				case TokenType.LogicalAnd:
					LhsBool = EvaluateCondition(ref Idx, GetPropertyValues);
					RhsBool = EvaluateCondition(ref Idx, GetPropertyValues);
					return LhsBool && RhsBool;
				case TokenType.Lt:
					LhsScalar = EvaluateScalar(ref Idx, GetPropertyValues);
					RhsScalar = EvaluateScalar(ref Idx, GetPropertyValues);
					return AsIntegers(LhsScalar).Any(x => AsIntegers(RhsScalar).Any(y => x < y));
				case TokenType.Lte:
					LhsScalar = EvaluateScalar(ref Idx, GetPropertyValues);
					RhsScalar = EvaluateScalar(ref Idx, GetPropertyValues);
					return AsIntegers(LhsScalar).Any(x => AsIntegers(RhsScalar).Any(y => x <= y));
				case TokenType.Gt:
					LhsScalar = EvaluateScalar(ref Idx, GetPropertyValues);
					RhsScalar = EvaluateScalar(ref Idx, GetPropertyValues);
					return AsIntegers(LhsScalar).Any(x => AsIntegers(RhsScalar).Any(y => x > y));
				case TokenType.Gte:
					LhsScalar = EvaluateScalar(ref Idx, GetPropertyValues);
					RhsScalar = EvaluateScalar(ref Idx, GetPropertyValues);
					return AsIntegers(LhsScalar).Any(x => AsIntegers(RhsScalar).Any(y => x >= y));
				case TokenType.Regex:
					LhsScalar = EvaluateScalar(ref Idx, GetPropertyValues);
					RhsScalar = EvaluateScalar(ref Idx, GetPropertyValues);
					return LhsScalar.Any(x => RhsScalar.Any(y => Regex.IsMatch(x, y, RegexOptions.IgnoreCase)));
				default:
					throw new InvalidOperationException("Invalid token type");
			}
		}

		IEnumerable<string> EvaluateScalar(ref int Idx, Func<string, IEnumerable<string>> GetPropertyValues)
		{
			Token Token = Tokens[Idx++];
			switch (Token.Type)
			{
				case TokenType.Identifier:
					return GetPropertyValues(Strings[Token.Index]);
				case TokenType.Scalar:
					return new string[] { Strings[Token.Index] };
				default:
					throw new InvalidOperationException("Invalid token type");
			}
		}

		static IEnumerable<long> AsIntegers(IEnumerable<string> Scalars)
		{
			foreach (string Scalar in Scalars)
			{
				long Value;
				if (long.TryParse(Scalar, out Value))
				{
					yield return Value;
				}
			}
		}

		/// <summary>
		/// Implicit conversion from string to conditions
		/// </summary>
		/// <param name="Text"></param>
		[return: NotNullIfNotNull("Text")]
		public static implicit operator Condition?(string? Text)
		{
			if (Text == null)
			{
				return null;
			}
			else
			{
				return new Condition(Text);
			}
		}

		/// <inheritdoc/>
		public override string ToString() => (Error != null) ? $"[Error] {Text}" : Text;
	}

	/// <summary>
	/// Converter from conditions to compact binary objects
	/// </summary>
	public class ConditionCbConverter : CbConverterBase<Condition>
	{
		/// <inheritdoc/>
		public override Condition Read(CbField Field) => Condition.TryParse(Field.AsUtf8String().ToString());

		/// <inheritdoc/>
		public override void Write(CbWriter Writer, Condition Value) => Writer.WriteUtf8StringValue(Value.Text);

		/// <inheritdoc/>
		public override void WriteNamed(CbWriter Writer, Utf8String Name, Condition Value) => Writer.WriteUtf8String(Name, Value.Text);
	}

	/// <summary>
	/// Type converter from strings to condition objects
	/// </summary>
	sealed class ConditionTypeConverter : TypeConverter
	{
		/// <inheritdoc/>
		public override bool CanConvertFrom(ITypeDescriptorContext Context, Type SourceType) => SourceType == typeof(string);

		/// <inheritdoc/>
		public override object ConvertFrom(ITypeDescriptorContext Context, CultureInfo Culture, object Value) => Condition.TryParse((string)Value);

		/// <inheritdoc/>
		public override bool CanConvertTo(ITypeDescriptorContext Context, Type DestinationType) => DestinationType == typeof(string);

		/// <inheritdoc/>
		public override object? ConvertTo(ITypeDescriptorContext Context, CultureInfo Culture, object Value, Type DestinationType) => ((Condition)Value).Text;
	}

	/// <summary>
	/// Type converter from Json strings to condition objects
	/// </summary>
	sealed class ConditionJsonConverter : JsonConverter<Condition>
	{
		/// <inheritdoc/>
		public override bool CanConvert(Type typeToConvert) => typeToConvert == typeof(Condition);

		public override Condition Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
		{
			string? String = reader.GetString();
			if(String == null)
			{
				throw new InvalidOperationException();
			}
			return Condition.Parse(String);
		}

		public override void Write(Utf8JsonWriter writer, Condition value, JsonSerializerOptions options)
		{
			writer.WriteStringValue(value.ToString());
		}
	}
}
