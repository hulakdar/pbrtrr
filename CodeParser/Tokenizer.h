#pragma once

#include <tracy/Tracy.hpp>

#include "Common.h"
#include "Containers/StringView.h"
#include "Containers/Array.h"
#include "Util/Debug.h"

bool IsWhitespace(char C);

enum class TokType : u8
{
	EndOfFile,
	Identifier,
	Number,
	StringLiteral,
	CharLiteral,
	Arrow,
	EmptyString,
	EmptyParens,
	EmptyBrackets,

	AndEquals,
	OrEquals,
	XorEquals,
	MulEquals,
	PlusEquals,
	MinusEquals,
	DivEquals,
	ModEquals,
	LessEquals,
	GreatEquals,
	Equals,
	NotEquals,
	ColonColon,

	Increment,
	Decrement,
	LeftShift,
	RightShift,

	LogicalOR,
	LogicalAND,

	Percent = '%',
	Ampersand = '&',

	Bang = '!',

	Less = '<',
	More = '>',

	OpenParen = '(',
	CloseParen = ')',
	OpenBracket = '[',
	CloseBracket = ']',
	OpenCurly = '{',
	CloseCurly = '}',

	Asterisk = '*',
	Plus = '+',
	Minus = '-',
	ForwardSlash = '/',
	BackSlash = '\\',

	Pipe = '|',
	Carrot = '^',

	Assign = '=',

	Dot = '.',
	Comma = ',',

	Colon = ':',
	Semicolon = ';',
	Question = '?',

	Backtick = '`',
	Tilda = '~',
};

struct Tok
{
	StringView Text;
	TokType Type;
	u8 IndentLevel;
};

struct CodeGenerator
{
    CodeGenerator(StringView Code, FILE* File)
    {
        At = Code;
		OutputFile = File;
		FetchLookahead();
    }
	
	void FetchLookahead()
	{
		Lookahead1 = _mm256_setzero_si256();
		Lookahead2 = _mm256_setzero_si256();

		if (At.size() > 0)
			Lookahead1 = _mm256_loadu_si256((__m256i*)At.data());
		if (At.size() > 32)
			Lookahead2 = _mm256_loadu_si256((__m256i*)(At.data() + 32));
	}

	void EatChar(u32 Count)
	{
		ZoneScoped;
		Count = std::min<u32>(Count, At.size());

		At.remove_prefix(Count);
		if (Count == 32)
		{
			Lookahead1 = Lookahead2;
			Lookahead2 = _mm256_setzero_si256();
			if (At.size() > 32)
			{
				Lookahead2 = _mm256_loadu_si256((__m256i*)(At.data() + 32));
			}
		}
		else
		{
			FetchLookahead();
		}
	}

	void ConsumeComment()
	{
		StringView DebugStartLine = At;
		if (At.empty())
			return;

		ZoneScoped;
		__m256i Ast = _mm256_set1_epi8('*');
		__m256i Slash = _mm256_set1_epi8('/');
		
		while (true)
		{
			__m256i Comparison1 = _mm256_cmpeq_epi8(Lookahead1, Slash);
			__m256i Comparison2 = _mm256_cmpeq_epi8(Lookahead2, Slash);

			u32 Mask1 = _mm256_movemask_epi8(Comparison1);
			u32 Mask2 = _mm256_movemask_epi8(Comparison2);
			u64 Mask = u64(Mask1) | (u64(Mask2) << 32);

			{
				Comparison1 = _mm256_cmpeq_epi8(Lookahead1, Ast);
				Comparison2 = _mm256_cmpeq_epi8(Lookahead2, Ast);

				Mask1 = _mm256_movemask_epi8(Comparison1);
				Mask2 = _mm256_movemask_epi8(Comparison2);
				u64 SlashMask = u64(Mask1) | (u64(Mask2) << 32);
				Mask &= SlashMask << 1;
			}

			if (Mask != 0)
			{
				u64 Index = _tzcnt_u64(Mask);
				EatChar(Index + 1);
				return;
			}
			EatChar(63);
		}
	}

	void ConsumeLine()
	{
		StringView DebugStartLine = At;
		if (At.empty())
			return;

		ZoneScoped;
		__m256i NL = _mm256_set1_epi8('\n');
		__m256i CR = _mm256_set1_epi8('\r');
		__m256i End = _mm256_set1_epi8(0);
		
		while (true)
		{
			__m256i Comparison1 = _mm256_or_si256(_mm256_cmpeq_epi8(Lookahead1, NL), _mm256_cmpeq_epi8(Lookahead1, CR));
			__m256i Comparison2 = _mm256_or_si256(_mm256_cmpeq_epi8(Lookahead2, NL), _mm256_cmpeq_epi8(Lookahead2, CR));

			Comparison1 = _mm256_or_si256(Comparison1, _mm256_cmpeq_epi8(Lookahead1, End));
			Comparison2 = _mm256_or_si256(Comparison2, _mm256_cmpeq_epi8(Lookahead2, End));

			u32 Mask1 = _mm256_movemask_epi8(Comparison1);
			u32 Mask2 = _mm256_movemask_epi8(Comparison2);
			u64 Mask = u64(Mask1) | (u64(Mask2) << 32);

			{
				__m256i Escape = _mm256_set1_epi8('\\');
				Comparison1 = _mm256_cmpeq_epi8(Lookahead1, Escape);
				Comparison2 = _mm256_cmpeq_epi8(Lookahead2, Escape);
				Mask1 = _mm256_movemask_epi8(Comparison1);
				Mask2 = _mm256_movemask_epi8(Comparison2);
				u64 EscapeMask = u64(Mask1) | (u64(Mask2) << 32);
				EscapeMask &= Mask >> 1;
				Mask ^= EscapeMask << 1;
				Mask ^= EscapeMask << 2;
			}

			if (Mask == 0)
			{
				EatChar(60);
				continue;
			}
			u64 Index = _tzcnt_u64(Mask);

			Mask >>= Index;
			Mask = ~Mask;
			Index += _tzcnt_u64(Mask);

			Index = std::min<u64>(Index, At.size());

			EatChar(Index);
			if (Index == 64)
			{
				ConsumeWhitespace();
			}
			return;
		}
	}

	void ConsumeWhitespace()
	{
		if (At.empty())
			return;

		ZoneScoped;
		__m256i Comparison1 = _mm256_cmpeq_epi8(Lookahead1, _mm256_set1_epi8(' '));
		__m256i Comparison2 = _mm256_cmpeq_epi8(Lookahead2, _mm256_set1_epi8(' '));

		Comparison1 = _mm256_or_si256( Comparison1, _mm256_cmpeq_epi8(Lookahead1, _mm256_set1_epi8('\t')));
		Comparison2 = _mm256_or_si256( Comparison2, _mm256_cmpeq_epi8(Lookahead2, _mm256_set1_epi8('\t')));

		Comparison1 = _mm256_or_si256( Comparison1, _mm256_cmpeq_epi8(Lookahead1, _mm256_set1_epi8('\n')));
		Comparison2 = _mm256_or_si256( Comparison2, _mm256_cmpeq_epi8(Lookahead2, _mm256_set1_epi8('\n')));

		Comparison1 = _mm256_or_si256( Comparison1, _mm256_cmpeq_epi8(Lookahead1, _mm256_set1_epi8('\r')));
		Comparison2 = _mm256_or_si256( Comparison2, _mm256_cmpeq_epi8(Lookahead2, _mm256_set1_epi8('\r')));

		//Comparison1 = _mm256_or_si256( Comparison1, _mm256_cmpeq_epi8(Lookahead1, _mm256_set1_epi8(0)));
		//Comparison2 = _mm256_or_si256( Comparison2, _mm256_cmpeq_epi8(Lookahead2, _mm256_set1_epi8(0)));

		u32 Mask1 = _mm256_movemask_epi8(Comparison1);
		u32 Mask2 = _mm256_movemask_epi8(Comparison2);
		u64 Mask = u64(Mask1) | (u64(Mask2) << 32);

		if (Mask == 0)
		{
			CHECK(false);
			return;
		}
		u64 Index = _tzcnt_u64(~Mask);
		EatChar(Index);
		CHECK(IsWhitespace(At[0]) == false);
	}

	void ConsumeIdentifier()
	{
		if (At.empty())
			return;

		ZoneScoped;
		__m256i Compare1;
		__m256i Compare2;
		{
			__m256i Start = _mm256_set1_epi8('0' - 1);
			__m256i End = _mm256_set1_epi8('9');

			__m256i CompareGreater1 = _mm256_cmpgt_epi8(Lookahead1, Start);
			__m256i CompareGreater2 = _mm256_cmpgt_epi8(Lookahead2, Start);

			__m256i CompareLess1 = _mm256_cmpgt_epi8(Lookahead1, End);
			__m256i CompareLess2 = _mm256_cmpgt_epi8(Lookahead2, End);

			Compare1 = _mm256_andnot_si256(CompareLess1, CompareGreater1);
			Compare2 = _mm256_andnot_si256(CompareLess2, CompareGreater2);
		}

		{
			__m256i Start = _mm256_set1_epi8('a' - 1);
			__m256i End = _mm256_set1_epi8('z');

			__m256i CompareGreater1 = _mm256_cmpgt_epi8(Lookahead1, Start);
			__m256i CompareGreater2 = _mm256_cmpgt_epi8(Lookahead2, Start);

			__m256i CompareLess1 = _mm256_cmpgt_epi8(Lookahead1, End);
			__m256i CompareLess2 = _mm256_cmpgt_epi8(Lookahead2, End);

			Compare1 = _mm256_or_si256(_mm256_andnot_si256(CompareLess1, CompareGreater1), Compare1);
			Compare2 = _mm256_or_si256(_mm256_andnot_si256(CompareLess2, CompareGreater2), Compare2);
		}

		{
			__m256i Start = _mm256_set1_epi8('A' - 1);
			__m256i End = _mm256_set1_epi8('Z');

			__m256i CompareGreater1 = _mm256_cmpgt_epi8(Lookahead1, Start);
			__m256i CompareGreater2 = _mm256_cmpgt_epi8(Lookahead2, Start);

			__m256i CompareLess1 = _mm256_cmpgt_epi8(Lookahead1, End);
			__m256i CompareLess2 = _mm256_cmpgt_epi8(Lookahead2, End);

			Compare1 = _mm256_or_si256(_mm256_andnot_si256(CompareLess1, CompareGreater1), Compare1);
			Compare2 = _mm256_or_si256(_mm256_andnot_si256(CompareLess2, CompareGreater2), Compare2);
		}

		{
			__m256i Underscore = _mm256_set1_epi8('_');

			__m256i CompareEq1 = _mm256_cmpeq_epi8(Lookahead1, Underscore);
			__m256i CompareEq2 = _mm256_cmpeq_epi8(Lookahead2, Underscore);

			Compare1 = _mm256_or_si256(CompareEq1, Compare1);
			Compare2 = _mm256_or_si256(CompareEq2, Compare2);
		}

		u32 Mask1 = _mm256_movemask_epi8(Compare1);
		u32 Mask2 = _mm256_movemask_epi8(Compare2);
		u64 Mask = u64(Mask1) | (u64(Mask2) << 32);

		{
			__m256i Colon = _mm256_set1_epi8(':');

			__m256i CompareEq1 = _mm256_cmpeq_epi8(Lookahead1, Colon);
			__m256i CompareEq2 = _mm256_cmpeq_epi8(Lookahead2, Colon);

			u32 CompMask1 = _mm256_movemask_epi8(CompareEq1);
			u32 CompMask2 = _mm256_movemask_epi8(CompareEq2);
			u64 CompMask = u64(CompMask1) | (u64(CompMask2) << 32);
			u64 ShiftedAnd1 = CompMask & (CompMask << 1);
			u64 ShiftedAnd2 = CompMask & (CompMask >> 1);
			Mask |= ShiftedAnd1 | ShiftedAnd2;
		}

		Mask = ~Mask;

		if (Mask == 0)
		{
			EatChar(32);
			ConsumeIdentifier();
			return;
		}
		u64 Index = _tzcnt_u64(Mask);
		EatChar(Index);
	}

	void ConsumeStringLiteral()
	{
		if (At.empty())
			return;

		ZoneScoped;
		u64 Mask = 0;
		{
			__m256i Quote = _mm256_set1_epi8('\"');
			__m256i Comparison1 = _mm256_cmpeq_epi8(Lookahead1, Quote);
			__m256i Comparison2 = _mm256_cmpeq_epi8(Lookahead2, Quote);
			u32 Mask1 = _mm256_movemask_epi8(Comparison1);
			u32 Mask2 = _mm256_movemask_epi8(Comparison2);
			Mask = u64(Mask1) | (u64(Mask2) << 32);
		}

		{
			__m256i Escape = _mm256_set1_epi8('\\');
			__m256i Comparison1 = _mm256_cmpeq_epi8(Lookahead1, Escape);
			__m256i Comparison2 = _mm256_cmpeq_epi8(Lookahead2, Escape);
			u32 Mask1 = _mm256_movemask_epi8(Comparison1);
			u32 Mask2 = _mm256_movemask_epi8(Comparison2);
			u64 EscapeMask = u64(Mask1) | (u64(Mask2) << 32);
			EscapeMask &= Mask >> 1;
			Mask ^= EscapeMask << 1;
		}

		if (Mask == 0)
		{
			EatChar(32);
			ConsumeStringLiteral();
			return;
		}
		u64 Index = _tzcnt_u64(Mask);
		EatChar(Index + 1);
	}

	void ConsumeNumber()
	{
		ZoneScoped;
		while (isdigit(At[0]) || At[0] == '.')
		{
			EatChar(1);
		}
	}

	bool ShouldContinue()
	{
		return !At.empty();
	}

	StringView At;
	__m256i Lookahead1;
	__m256i Lookahead2;

	FILE* OutputFile;

	u32 IndentLevel = 0;
	TArray<Tok> Scopes;
};

void ParseCode(StringView Code, StringView InputFilename, StringView OutputFilename);
void ParseHeader(StringView Code, StringView InputFilename, StringView OutputFilename);