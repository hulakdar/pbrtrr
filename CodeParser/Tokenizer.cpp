#include "Tokenizer.h"

#include "Util/Debug.h"
#include "tracy/Tracy.hpp"
#include "Containers/Array.h"
#include "Containers/String.h"

#include <stdio.h>

String StringFromFormat(const char* Format, ...);

bool IsTokenNamed(Tok Token, StringView Name)
{
	if (Token.Text.size() == Name.size())
	{
		return strncmp(Token.Text.data(), Name.data(), Name.size()) == 0;
	}
	return false;
}

bool IsTokenOperator(Tok Token)
{
	switch (Token.Type)
	{
	case TokType::Arrow:
	case TokType::EmptyString:
	case TokType::EmptyParens:
	case TokType::EmptyBrackets:
	case TokType::AndEquals:
	case TokType::OrEquals:
	case TokType::XorEquals:
	case TokType::MulEquals:
	case TokType::PlusEquals:
	case TokType::MinusEquals:
	case TokType::DivEquals:
	case TokType::ModEquals:
	case TokType::LessEquals:
	case TokType::GreatEquals:
	case TokType::Equals:
	case TokType::NotEquals:
	case TokType::Increment:
	case TokType::Decrement:
	case TokType::LogicalOR:
	case TokType::LogicalAND:
	case TokType::Percent:
	case TokType::Ampersand:
	case TokType::Bang:
	case TokType::Less:
	case TokType::More:
	case TokType::Asterisk:
	case TokType::Plus:
	case TokType::Minus:
	case TokType::ForwardSlash:
	case TokType::Pipe:
	case TokType::Carrot:
	case TokType::Assign:
	case TokType::Comma:
		return true;
	default:
		return false;
	}
}

bool IsWhitespace(char C)
{
	return C == ' ' || C == '\t' || C == '\n' || C == '\r';
}

void EatGarbage(CodeGenerator& Tokenizer)
{
    ZoneScoped;
	while (true)
	{
		if (!Tokenizer.At.empty() && Tokenizer.At[0] == '/' && Tokenizer.At[1] == '/')
		{
			Tokenizer.ConsumeLine();
			continue;
		}
		else if (!Tokenizer.At.empty() && Tokenizer.At[0] == '#')
		{
			Tokenizer.ConsumeLine();
			continue;
		}
		else if (!Tokenizer.At.empty() && Tokenizer.At[0] == '/' && Tokenizer.At[1] == '*')
		{
			Tokenizer.ConsumeComment();
			continue;
		}
		else if (!Tokenizer.At.empty() && IsWhitespace(Tokenizer.At[0]))
		{
			Tokenizer.ConsumeWhitespace();
			continue;
		}
		else
		{
			break;
		}
	}
}

Tok GetTokenCanBeEnd(CodeGenerator& Tokenizer)
{
    ZoneScoped;

	EatGarbage(Tokenizer);

	Tok Result;
	Result.IndentLevel = Tokenizer.IndentLevel;

	if (!Tokenizer.ShouldContinue())
	{
		Result.Type = TokType::EndOfFile;
		return Result;
	}

	u32 Len = 1;
	StringView Start = Tokenizer.At;

	Tokenizer.EatChar(1);

	switch (Start[0])
	{
	case '\\': Result.Type = TokType::BackSlash; break;
	case '\0': Result.Type = TokType::EndOfFile; break;

	case '{': 
	{
		Tokenizer.IndentLevel++;
		Result.Type = TokType::OpenCurly;
		break;
	}
	case '}': 
	{
		CHECK(Tokenizer.IndentLevel);
		Tokenizer.IndentLevel--;
		Result.Type = TokType::CloseCurly;
		if (Tokenizer.Scopes.size() && Tokenizer.Scopes.back().IndentLevel == Tokenizer.IndentLevel)
		{
			Tokenizer.Scopes.pop_back();
		}
		break;
	}
	case ';': Result.Type = TokType::Semicolon; break;
	case ']': Result.Type = TokType::CloseBracket; break;
	case ',': Result.Type = TokType::Comma; break;
	case '.': Result.Type = TokType::Dot; break;
	case '~': Result.Type = TokType::Tilda; break;
	case '`': Result.Type = TokType::Backtick; break;
	case '?': Result.Type = TokType::Question; break;
	case ')': Result.Type = TokType::CloseParen; break;

	case '[':
		Result.Type = TokType::OpenBracket;
		if (Tokenizer.At[0] == ']')
		{
			Result.Type = TokType::EmptyBrackets;
			Tokenizer.EatChar(1);
		}
		break;

	case '-':
		if (Tokenizer.At[0] == '>')
		{
			Result.Type = TokType::Arrow;
			Tokenizer.EatChar(1);
			break;
		}
	case ':':
	case '&':
	case '|':
	case '+':
	case '=':
	case '<':
	case '>':
		if (Tokenizer.At[0] == Start[0])
		{
			if (Start[0] == '+') Result.Type = TokType::Increment;
			else if (Start[0] == '-') Result.Type = TokType::Decrement;
			else if (Start[0] == '&') Result.Type = TokType::LogicalAND;
			else if (Start[0] == '|') Result.Type = TokType::LogicalOR;
			else if (Start[0] == ':') Result.Type = TokType::ColonColon;
			else if (Start[0] == '<') Result.Type = TokType::LeftShift;
			else if (Start[0] == '>') Result.Type = TokType::RightShift;
			else if (Start[0] == '=') Result.Type = TokType::Equals;
			else CHECK(false);

			Tokenizer.EatChar(1);
			break;
		}
	case '%':
	case '^':
	case '*':
	case '/':
	case '!':
		if (Tokenizer.At[0] == '=')
		{
			if (Start[0] == '*')      Result.Type = TokType::MulEquals;
			else if (Start[0] == '%') Result.Type = TokType::ModEquals;
			else if (Start[0] == '/') Result.Type = TokType::DivEquals;
			else if (Start[0] == '+') Result.Type = TokType::PlusEquals;
			else if (Start[0] == '-') Result.Type = TokType::MinusEquals;
			else if (Start[0] == '&') Result.Type = TokType::AndEquals;
			else if (Start[0] == '|') Result.Type = TokType::OrEquals;
			else if (Start[0] == '^') Result.Type = TokType::XorEquals;
			else if (Start[0] == '<') Result.Type = TokType::LessEquals;
			else if (Start[0] == '>') Result.Type = TokType::GreatEquals;
			else if (Start[0] == '!') Result.Type = TokType::NotEquals;
			else CHECK(false);

			Tokenizer.EatChar(1);
		}
		else
		{
			Result.Type = (TokType)Start[0];
		}
		break;
	case '(':
		Result.Type = TokType::OpenParen;
		if (Tokenizer.At[0] == ')')
		{
			Result.Type = TokType::EmptyParens;
			Tokenizer.EatChar(1);
		}
		break;

	case '\'':
		while (!Tokenizer.At.empty() && Tokenizer.At[0] != '\'')
		{
			Tokenizer.EatChar(1);
		}
		Tokenizer.EatChar(1);

		Result.Type = TokType::CharLiteral;
		break;
	case '\"':
		if (Tokenizer.At[0] == '\"')
		{
			Result.Type = TokType::EmptyString;
			Tokenizer.EatChar(1);
		}
		else
		{
			Result.Type = TokType::StringLiteral;
			Tokenizer.ConsumeStringLiteral();
		}
		break;
	default:
		if (isalpha(Start[0]) || Start[0] == '_')
		{
			Tokenizer.ConsumeIdentifier();

			Result.Type = TokType::Identifier;
		}
		else if (isdigit(Start[0]))
		{
			Tokenizer.ConsumeNumber();

			Result.Type = TokType::Number;
		}
		else
		{
			CHECK(false);
		}
	}
	Len = Tokenizer.At.data() - Start.data();

	Result.Text = Start.substr(0, Len);
	return Result;
}

Tok GetToken(CodeGenerator& Tokenizer)
{
	Tok Result = GetTokenCanBeEnd(Tokenizer);
	CHECK(Result.Type != TokType::EndOfFile);
	return Result;
}

void ExitCurrentScope(CodeGenerator& Tokenizer)
{
	u32 CurrentIndentLevel = 1;
	while (CurrentIndentLevel)
	{
		Tok It = GetToken(Tokenizer);
		if (It.Type == TokType::OpenCurly) CurrentIndentLevel++;
		if (It.Type == TokType::CloseCurly) CurrentIndentLevel--;
	}
}

Tok RequireToken(CodeGenerator& Tokenizer, TokType Type)
{
	Tok Result = GetToken(Tokenizer);
	CHECK(Type == Result.Type);
	return Result;
}

Tok RequireOperator(CodeGenerator& Tokenizer)
{
	Tok Result = GetToken(Tokenizer);
	CHECK(IsTokenOperator(Result));
	return Result;
}

String MemberDefinition(StringView Type, StringView Name, StringView Scopes, StringView TemplateParams, StringView ArrayNum, bool IsArray)
{
	String TemplateText;
	if (!TemplateParams.empty())
	{
		TemplateText = StringFromFormat("<%.*s>", VIEW_PRINT(TemplateParams));
	}
	String NewMember;
	if (IsArray)
	{
		NewMember = StringFromFormat(
			"{[](void* In)->String{ auto* AfterCast = (%.*s %.*s*)In; String Result; for (int i = 0; i < %.*s; ++i) Result += ToString(&AfterCast->%.*s[i]); return Result;},"
			" [](void* In){ auto* AfterCast = (%.*s %.*s*)In; ImGui::Text(\"%.*s: \"); ImGui::SameLine(); if (ImGui::TreeNodeEx(&AfterCast->%.*s, ImGuiTreeNodeFlags_None, \"Array\")) {for (int i = 0; i < %.*s; ++i) {%s ToUI(&AfterCast->%.*s[i]);} ImGui::TreePop();}},",
			VIEW_PRINT(Scopes),
			VIEW_PRINT(TemplateText),
			VIEW_PRINT(ArrayNum),
			VIEW_PRINT(Name),
			VIEW_PRINT(Scopes),
			VIEW_PRINT(TemplateText),
			VIEW_PRINT(Name),
			VIEW_PRINT(Name),
			VIEW_PRINT(ArrayNum),
			"ImGui::Text(\"[%d]:\", i); ImGui::SameLine();",
			VIEW_PRINT(Name)
		);
	}
	else
	{
		NewMember = StringFromFormat(
			"{[](void* In)->String{ auto* AfterCast = (%.*s %.*s*)In; return ToString(&AfterCast->%.*s);},"
			" [](void* In){ auto* AfterCast = (%.*s %.*s*)In; ImGui::Text(\"%.*s: \"); ImGui::SameLine(); ToUI(&AfterCast->%.*s);},",
			VIEW_PRINT(Scopes),
			VIEW_PRINT(TemplateText),
			VIEW_PRINT(Name),
			VIEW_PRINT(Scopes),
			VIEW_PRINT(TemplateText),
			VIEW_PRINT(Name),
			VIEW_PRINT(Name)
		);
	}
	NewMember += StringFromFormat(
		"\"%.*s\", offsetof(%.*s%.*s, %.*s), sizeof(%.*s%.*s::%.*s), %.*s},\n",
		VIEW_PRINT(Name),
		VIEW_PRINT(Scopes),
		VIEW_PRINT(TemplateText),
		VIEW_PRINT(Name),
		VIEW_PRINT(Scopes),
		VIEW_PRINT(TemplateText),
		VIEW_PRINT(Name),
		VIEW_PRINT(ArrayNum)
	);
	return NewMember;
}

void ParseEnum(CodeGenerator& Generator, StringView EnumName)
{
	CHECK(Generator.IndentLevel > 0);
	TArray<StringView> Enumeration;

	String Scopes;
	for (auto ScopeTok : Generator.Scopes)
	{
		Scopes.append("::");
		Scopes.append(ScopeTok.Text.data(), ScopeTok.Text.size());
	}

	while (true)
	{
		StringView Start = Generator.At;

		Tok It = GetToken(Generator);
		if (It.Type == TokType::CloseCurly) // end
		{
			break;
		}
		else
		{
			Tok Name = It;

			It = GetToken(Generator);
			if (It.Type == TokType::Comma)
			{
				Enumeration.push_back(Name.Text.substr(0, It.Text.data() - Name.Text.data()));
			}
			else if (It.Type == TokType::Assign)
			{
				Enumeration.push_back(Name.Text.substr(0, It.Text.data() - Name.Text.data()));
				while (It.Type != TokType::Comma && It.Type != TokType::CloseCurly)
				{
					It = GetToken(Generator);
				}
			}
			if (It.Type == TokType::CloseCurly)
			{
				break;
			}
		}
	}
	RequireToken(Generator, TokType::Semicolon);
	
	String EnumsToPrint = StringFromFormat("	static EnumToString %.*s_Table[] = {\n", VIEW_PRINT(EnumName));
	for (StringView E : Enumeration)
	{
		EnumsToPrint += StringFromFormat("		{(u32)%.*s::%.*s, \"%.*s\"},\n", VIEW_PRINT(Scopes), VIEW_PRINT(E), VIEW_PRINT(E));
	}
	EnumsToPrint += "	};\n";
	EnumsToPrint += StringFromFormat(
	"\n	static String ToString(%.*s In)\n"
	"	{\n"
	"		for (int i = 0; i < ArrayCount(%.*s_Table);++i)\n"
	"		{\n"
	"			if ((u32)In == %.*s_Table[i].Value)\n"
	"			{\n"
	"				return String(%.*s_Table[i].Text);\n"
	"			}\n"
	"		}\n"
	"		return \"\";\n"
	"	}\n\n",
		VIEW_PRINT(Scopes),
		VIEW_PRINT(EnumName),
		VIEW_PRINT(EnumName),
		VIEW_PRINT(EnumName)
	);

	fwrite(EnumsToPrint.data(), 1, EnumsToPrint.size(), Generator.OutputFile);
}

void DebugPrint(const char* Format, ...);

void ParseStruct(CodeGenerator& Generator, StringView StructName, StringView TemplateParams, bool IsPublic)
{
	TArray<String> Members;

	CHECK(Generator.IndentLevel > 0);

	String Scopes;
	for (auto ScopeTok : Generator.Scopes)
	{
		Scopes.append("::");
		Scopes.append(ScopeTok.Text.data(), ScopeTok.Text.size());
	}

	u32 StructIndentLevel = Generator.IndentLevel;
	while (Generator.IndentLevel >= StructIndentLevel)
	{
		StringView Start = Generator.At;

		Tok NextToken = GetToken(Generator);

		if (IsTokenNamed(NextToken, "private"))
		{
			RequireToken(Generator, TokType::Colon);
			IsPublic = false;
			continue;
		}
		if (IsTokenNamed(NextToken, "public"))
		{
			RequireToken(Generator, TokType::Colon);
			IsPublic = true;
			continue;
		}

		if (NextToken.Type == TokType::Semicolon)
		{
			continue;
		}
		else if (NextToken.Type == TokType::CloseCurly) // end
		{
			RequireToken(Generator, TokType::Semicolon);
			continue;
		}
		else if (NextToken.Type == TokType::Ampersand) // destructor
		{
			CHECK(IsTokenNamed(RequireToken(Generator, TokType::Identifier), StructName));

			Tok It = GetToken(Generator);
			while (It.Type != TokType::EmptyParens)
			{
				It = GetToken(Generator);
			}
			It = GetToken(Generator);
			if (It.Type == TokType::OpenCurly)
			{
				ExitCurrentScope(Generator);
			}
			else 
			{
				CHECK(It.Type == TokType::Semicolon);
			}
		}
		else if (IsTokenNamed(NextToken, "TracyLockable")
		|| IsTokenNamed(NextToken, "TracySharedLockable"))
		{
			Tok It = RequireToken(Generator, TokType::OpenParen);
			Tok Type = RequireToken(Generator, TokType::Identifier);
			RequireToken(Generator, TokType::Comma);
			Tok Name = RequireToken(Generator, TokType::Identifier);
			RequireToken(Generator, TokType::CloseParen);
			RequireToken(Generator, TokType::Semicolon);
			//String NewMember = MemberDefinition(Type.Text, Name.Text, Scopes, "1", IsPointer ? TypeInfoFlags::IsPointer : 0);
			if (IsPublic)
			{
				String NewMember = MemberDefinition(Type.Text, Name.Text, Scopes, TemplateParams, "1", false);
				NewMember.insert((size_t)0, (size_t)Generator.IndentLevel + 1, '\t');
				Members.push_back(NewMember);
			}
		}
		else if (IsTokenNamed(NextToken, StructName)) // constructor
		{
			Tok It = GetToken(Generator);
			while (It.Type != TokType::CloseParen && It.Type != TokType::EmptyParens)
			{
				It = GetToken(Generator);
			}
			It = GetToken(Generator);
			if (It.Type == TokType::Semicolon)
			{
				continue;
			}
			else
			{
				while (It.Type != TokType::OpenCurly && It.Type != TokType::Semicolon)
				{
					It = GetToken(Generator);
				}

				if (It.Type == TokType::OpenCurly)
				{
					ExitCurrentScope(Generator);
				}
			}
		}
		else if (IsTokenNamed(NextToken, "struct")
			|| IsTokenNamed(NextToken, "union"))
		{
			Tok It = GetToken(Generator);
			if (It.Type == TokType::OpenCurly)
			{
				continue;
			}
			Tok InnerStructName = It;

			It = GetToken(Generator);
			if (It.Type == TokType::Semicolon)
			{
				continue;
			}

			CHECK(It.Type == TokType::OpenCurly);

			Generator.Scopes.push_back(InnerStructName);
			ParseStruct(Generator, InnerStructName.Text, TemplateParams, true);
		}
		else if (IsTokenNamed(NextToken, "enum"))
		{
			Tok EnumName = RequireToken(Generator, TokType::Identifier);
			Tok It = GetToken(Generator);
			if (It.Type == TokType::Semicolon)
			{
				return;
			}
			else if (It.Type == TokType::Colon)
			{
				GetToken(Generator);
				It = GetToken(Generator);
			}

			CHECK(It.Type == TokType::OpenCurly);

			Generator.Scopes.push_back(EnumName);
			ParseEnum(Generator, EnumName.Text);
		}
		else if (IsTokenNamed(NextToken, "using"))
		{
			Tok It = GetToken(Generator);
			while (It.Type != TokType::Semicolon)
			{
				It = GetToken(Generator);
			}
		}
		else
		{
			bool IsPointer = false;
			bool IsReference = false;
			bool IsArray = false;
			StringView ArrayNum = "1";

			Tok It = NextToken;
			Tok Name;
			Tok Type;

			if (IsTokenNamed(It, "friend"))
			{
				It = GetToken(Generator);
			}

			if (IsTokenNamed(It, "operator"))
			{
				Name = It;
				Type = GetToken(Generator);
				while (It.Type != TokType::EmptyParens)
				{
					It = GetToken(Generator);
					CHECK(It.Type != TokType::OpenParen);
				}
			}
			else
			{
				if (IsTokenNamed(It, "const"))
				{
					Type = RequireToken(Generator, TokType::Identifier);
				}
				else
				{
					Type = It;
				}

				It = GetToken(Generator);
				if (It.Type == TokType::Less)
				{
					u32 IndentLevel = 1;
					while (IndentLevel)
					{
						It = GetToken(Generator);
						if      (It.Type == TokType::Less)       IndentLevel += 1;
						else if (It.Type == TokType::More)       IndentLevel -= 1;
						else if (It.Type == TokType::LeftShift)  CHECK(false);
						else if (It.Type == TokType::RightShift) IndentLevel -= 2;
					}
					It = GetToken(Generator);
				}

				if (It.Type == TokType::OpenParen)
				{
					RequireToken(Generator, TokType::Asterisk);
					IsPointer = true;
					Name = RequireToken(Generator, TokType::Identifier);
					It = RequireToken(Generator, TokType::CloseParen);

					if (IsPublic)
					{
						String NewMember = MemberDefinition(Type.Text, Name.Text, Scopes, TemplateParams, "1", false);
						NewMember.insert((size_t)0, (size_t)Generator.IndentLevel + 1, '\t');
						Members.push_back(NewMember);
					}
				}
				else if (It.Type == TokType::Ampersand)
				{
					while (It.Type == TokType::Ampersand)
					{
						It = GetToken(Generator);
					}
					IsReference = true;
					Name = It;
				}
				else if (It.Type == TokType::Asterisk)
				{
					while (It.Type == TokType::Asterisk)
					{
						It = GetToken(Generator);
					}
					IsPointer = true;
					Name = It;
				}
				else
				{
					Name = It;
				}
				if (IsTokenNamed(Name, "operator"))
				{
					It = RequireOperator(Generator);
					It = GetToken(Generator);
					CHECK(It.Type == TokType::OpenParen || It.Type == TokType::EmptyParens);
				}
				else
				{
					It = GetToken(Generator);
				}

				if (It.Type == TokType::OpenBracket)
				{
					Tok StartArrayNum = GetToken(Generator);
					while (It.Type != TokType::CloseBracket)
					{
						It = GetToken(Generator);
					}
					ArrayNum = StringView(StartArrayNum.Text.data(), It.Text.data() - StartArrayNum.Text.data());
					It = GetToken(Generator);
					IsArray = true;
				}
			}

			if (It.Type == TokType::Colon)
			{
				// bitset not supported
				It = RequireToken(Generator, TokType::Number);
				It = RequireToken(Generator, TokType::Semicolon);
			}
			else if (It.Type == TokType::Assign
					|| It.Type == TokType::OpenCurly
					|| It.Type == TokType::Semicolon)
			{
				while (It.Type != TokType::Semicolon)
				{
					It = GetToken(Generator);
				}
				if (IsPublic)
				{
					String NewMember = MemberDefinition(Type.Text, Name.Text, Scopes, TemplateParams, ArrayNum, IsArray);
					NewMember.insert((size_t)0, (size_t)Generator.IndentLevel + 1, '\t');
					Members.push_back(NewMember);
				}
			}
			else if (It.Type == TokType::OpenParen || It.Type == TokType::EmptyParens) // method
			{
				while (It.Type != TokType::CloseParen && It.Type != TokType::EmptyParens)
				{
					It = GetToken(Generator);
				}

				It = GetToken(Generator);

				if (IsTokenNamed(It, "const"))
				{
					It = GetToken(Generator);
				}
				if (IsTokenNamed(It, "noexcept"))
				{
					It = GetToken(Generator);
				}
				if (It.Type == TokType::Semicolon)
				{
					continue;
				}
				else if (It.Type == TokType::OpenCurly)
				{
					ExitCurrentScope(Generator);
				}
				else{CHECK(false);}
			}
			else
			{
				// something weird
				// try and find end of this thing
				while (It.Type != TokType::Semicolon && It.Type != TokType::OpenCurly && It.Type != TokType::EndOfFile)
				{
					It = GetToken(Generator);
				}
				if (It.Type == TokType::OpenCurly)
				{
					ExitCurrentScope(Generator);
				}

				String DebugData = "// could not parse: ";
				DebugData.append(Type.Text.data(), It.Text.data() + 1 - Type.Text.data());
				u64 Pos = DebugData.find('\n');
				while (Pos != String::npos)
				{
					DebugData.replace(Pos, 1, 1, ' ');
					Pos = DebugData.find('\n', Pos);
				}
				Pos = DebugData.find('\r');
				while (Pos != String::npos)
				{
					DebugData.replace(Pos, 1, 1, ' ');
					Pos = DebugData.find('\r', Pos);
				}
				DebugData.append(1, '\n');

				DebugPrint("%.*s", VIEW_PRINT(DebugData));
				//CHECK(FALSE);
			}
		}
	}

	if (Members.empty())
	{
		return;
	}

	String TemplateText;
	if (!TemplateParams.empty())
	{
		TemplateText = StringFromFormat("template<%.*s>\n", VIEW_PRINT(TemplateParams));
		TemplateText.insert((size_t)0, (size_t)Generator.IndentLevel + 1, '\t');
	}
	String MembersToPrint = StringFromFormat("static TypeInfoMember _%.*s_TypeTable[] = {\n", VIEW_PRINT(StructName));
	MembersToPrint.insert((size_t)0, (size_t)Generator.IndentLevel + 1, '\t');
	MembersToPrint.insert((size_t)0, TemplateText);
	for (auto& M : Members)
	{
		MembersToPrint += M;
	}
	MembersToPrint.append((size_t)Generator.IndentLevel + 1, '\t');
	MembersToPrint += "};\n";

	if (!TemplateParams.empty())
	{
		MembersToPrint += StringFromFormat("	template<%.*s>\n", VIEW_PRINT(TemplateParams));
		TemplateText = StringFromFormat("<%.*s>", VIEW_PRINT(TemplateParams));
	}

	MembersToPrint += StringFromFormat(
		"	static String ToString(%.*s %.*s* In)\n"
		"	{\n"
		"		return ToString(_%.*s_TypeTable%.*s, %d, In);\n"
		"	}\n\n",
		VIEW_PRINT(Scopes),
		VIEW_PRINT(TemplateText),
		VIEW_PRINT(StructName),
		VIEW_PRINT(TemplateText),
		(int)Members.size()
	);

	if (!TemplateParams.empty())
	{
		MembersToPrint += StringFromFormat("	template<%.*s>\n", VIEW_PRINT(TemplateParams));
	}

	{
		MembersToPrint += StringFromFormat(
			"	static void ToUI(%.*s %.*s* In)\n"
			"	{\n"
			"		ToUI(_%.*s_TypeTable%.*s, %d, In, \"%.*s\");\n"
			"	}\n\n",
			VIEW_PRINT(Scopes),
			VIEW_PRINT(TemplateText),
			VIEW_PRINT(StructName),
			VIEW_PRINT(TemplateText),
			(int)Members.size(),
			VIEW_PRINT(StructName)
		);
	}
	fwrite(MembersToPrint.data(), 1, MembersToPrint.size(), Generator.OutputFile);
}

void ParseCode(StringView Code, StringView InputFilename, StringView OutputFilename)
{
    ZoneScoped;

	FILE* OutputFile = fopen(OutputFilename.data(), "w");

	CodeGenerator Generator(Code, OutputFile);
	while (Generator.ShouldContinue())
	{
		Tok T = GetTokenCanBeEnd(Generator);
		CHECK(Generator.IndentLevel < 200);
		switch (T.Type)
		{
		case TokType::CloseCurly:
		{
			break;
		}
		case TokType::Identifier:
		{
			if (IsTokenNamed(T, "template"))
			{
				RequireToken(Generator, TokType::Less);
				while (T.Type != TokType::More)
				{
					T = GetToken(Generator);
				}
				T = RequireToken(Generator, TokType::Identifier);
			}

			if (IsTokenNamed(T, "typedef"))
			{
				Tok It = RequireToken(Generator, TokType::Identifier);
				if (IsTokenNamed(It, "struct"))
				{
					RequireToken(Generator, TokType::OpenCurly);
					ExitCurrentScope(Generator);
					RequireToken(Generator, TokType::Identifier);
					RequireToken(Generator, TokType::Semicolon);
				}
			}
			else if (IsTokenNamed(T, "class")
				|| IsTokenNamed(T, "struct"))
			{
				Tok It = GetToken(Generator);
				while (It.Type != TokType::OpenCurly && It.Type != TokType::Semicolon)
				{
					It = GetToken(Generator);
				}
				if (It.Type == TokType::Semicolon)
				{
					continue;
				}

				ExitCurrentScope(Generator);
				RequireToken(Generator, TokType::Semicolon);
			}
			else if (IsTokenNamed(T, "enum"))
			{
				Tok EnumName = RequireToken(Generator, TokType::Identifier);
				if (IsTokenNamed(EnumName, "class"))
				{
					EnumName = RequireToken(Generator, TokType::Identifier);
				}

				Tok It = GetToken(Generator);
				if (It.Type == TokType::Colon)
				{
					RequireToken(Generator, TokType::Identifier);
					It = GetToken(Generator);
				}
				if (It.Type == TokType::Semicolon)
				{
					continue;
				}

				ExitCurrentScope(Generator);
				RequireToken(Generator, TokType::Semicolon);
			}
			else if (IsTokenNamed(T, "namespace"))
			{
				Tok Name = GetToken(Generator);
				if (Name.Type == TokType::OpenCurly) // anonymous, don't export
				{
					ExitCurrentScope(Generator);
				}
				else
				{
					Generator.Scopes.push_back(Name);
				}
			}
			else if (IsTokenNamed(T, "static") || IsTokenNamed(T, "extern") || IsTokenNamed(T, "inline") || T.Text.find("::") != StringView::npos)
			{
				Tok It = T;
				while (It.Type != TokType::OpenCurly && It.Type != TokType::Semicolon)
				{
					It = GetToken(Generator);
				}
				if (It.Type == TokType::OpenCurly)
				{
					ExitCurrentScope(Generator);
				}
			}
			else if (IsTokenNamed(T, "TracyLockable")
			|| IsTokenNamed(T, "TracySharedLockable"))
			{
				Tok It = RequireToken(Generator, TokType::OpenParen);
				Tok Type = RequireToken(Generator, TokType::Identifier);
				RequireToken(Generator, TokType::Comma);
				Tok Name = RequireToken(Generator, TokType::Identifier);
				RequireToken(Generator, TokType::CloseParen);
				RequireToken(Generator, TokType::Semicolon);

				String Declaration;
				if (IsTokenNamed(T, "TracySharedLockable"))
				{
					Declaration = StringFromFormat("extern SharedLockableBase(%.*s) %.*s", VIEW_PRINT(Type.Text), VIEW_PRINT(Name.Text));
				}
				else
				{
					Declaration = StringFromFormat("extern LockableBase(%.*s) %.*s", VIEW_PRINT(Type.Text), VIEW_PRINT(Name.Text));
				}
				Declaration.insert((size_t)0, (size_t)Generator.IndentLevel, '\t');
				Declaration.append(";\n");
				fwrite(Declaration.c_str(), 1, Declaration.size(), OutputFile);
			}
			else
			{
				Tok Type = T;
				if (IsTokenNamed(T, "const"))
				{
					T = RequireToken(Generator, TokType::Identifier);
				}
				Tok It = GetToken(Generator);
				if (It.Type == TokType::Less)
				{
					u32 IndentLevel = 1;
					while (IndentLevel)
					{
						It = GetToken(Generator);
						if      (It.Type == TokType::Less)       IndentLevel += 1;
						else if (It.Type == TokType::More)       IndentLevel -= 1;
						else if (It.Type == TokType::LeftShift)  CHECK(false);
						else if (It.Type == TokType::RightShift) IndentLevel -= 2;
					}
					It = GetToken(Generator);
				}

				Tok Name = It;
				if (It.Type == TokType::Asterisk || It.Type == TokType::Ampersand)
				{
					Name = RequireToken(Generator, TokType::Identifier);
				}
				if (Name.Text.find("::") != StringView::npos)
				{
					while (It.Type != TokType::OpenCurly && It.Type != TokType::Semicolon)
					{
						It = GetToken(Generator);
					}

					if (It.Type == TokType::OpenCurly)
					{
						ExitCurrentScope(Generator);
					}
				}
				else
				{
					if (IsTokenNamed(Name, "operator"))
					{
						It = GetToken(Generator);
						if (IsTokenNamed(It, "new") || IsTokenNamed(It, "delete"))
						{
							It = GetToken(Generator);
							if (It.Type == TokType::EmptyBrackets)
							{
								It = RequireToken(Generator, TokType::OpenParen);
							}
						}
						else
						{
							CHECK(IsTokenOperator(It));
							It = RequireToken(Generator, TokType::OpenParen);
						}
					}
					else
					{
						It = GetToken(Generator);
					}
					if (It.Type == TokType::OpenBracket)
					{
						while (It.Type != TokType::CloseBracket)
						{
							It = GetToken(Generator);
						}
						It = GetToken(Generator);
					}

					if (It.Type == TokType::Assign
						|| It.Type == TokType::OpenCurly
						|| It.Type == TokType::Semicolon) // declaration
					{
						StringView DeclView = StringView(Type.Text.data(), It.Text.data() - Type.Text.data());

						String Declaration = StringFromFormat("extern %.*s", VIEW_PRINT(DeclView));
						//Declaration.trim();
						Declaration.insert((size_t)0, (size_t)Generator.IndentLevel, ' ');
						Declaration.append(";\n");
						fwrite(Declaration.c_str(), 1, Declaration.size(), OutputFile);

						while (It.Type != TokType::Semicolon)
						{
							It = GetToken(Generator);
						}
					}
					else if (It.Type == TokType::OpenParen || It.Type == TokType::EmptyParens)
					{
						if (It.Type == TokType::OpenParen)
						{
							u32 IndentLevel = 1;
							while (IndentLevel)
							{
								It = GetToken(Generator);
								if (It.Type == TokType::OpenParen) IndentLevel++;
								if (It.Type == TokType::CloseParen) IndentLevel--;
							}
						}

						StringView DeclView = StringView(Type.Text.data(), It.Text.data() + 1 + (It.Type == TokType::EmptyParens) - Type.Text.data());

						String Declaration = String(DeclView);
						//Declaration.trim();
						u64 Pos = Declaration.find('\n');
						while (Pos != String::npos)
						{
							Declaration.erase(Pos, 1);
							Pos = Declaration.find('\n');
						}
						Pos = Declaration.find('\r');
						while (Pos != String::npos)
						{
							Declaration.erase(Pos, 1);
							Pos = Declaration.find('\r');
						}
						Declaration.append(";\n");

						fwrite(Declaration.c_str(), 1, Declaration.size(), OutputFile);

						It = GetToken(Generator);
						if (IsTokenNamed(It, "noexcept"))
						{
							It = GetToken(Generator);
						}
						CHECK(It.Type == TokType::OpenCurly);

						ExitCurrentScope(Generator);
					}
					else {CHECK(false);}
				}
			}
			break;
		}
		default:
			break;
		}
	}
	fclose(OutputFile);
}

void ParseHeader(StringView Code, StringView InputFilename, StringView OutputFilename, FILE* DeclarationsFile)
{
    ZoneScoped;

	FILE* OutputFile = fopen(OutputFilename.data(), "w");

	String IncludeString = StringFromFormat("#pragma once\n#include \"Util/TypeInfo.h\"\n#include \"%.*s\"\n", (int)InputFilename.size(), InputFilename.data());

	fwrite(IncludeString.data(), 1, IncludeString.size(), OutputFile);

	CodeGenerator Generator(Code, OutputFile);
	char Prefix[] = "namespace Generated {\n";
	fwrite(Prefix, 1, sizeof(Prefix) - 1, OutputFile);
	while (Generator.ShouldContinue())
	{
		Tok T = GetTokenCanBeEnd(Generator);
		switch (T.Type)
		{
		case TokType::Identifier:
		{
			if (IsTokenNamed(T, "namespace"))
			{
				Tok Name = GetToken(Generator);
				if (Name.Type == TokType::OpenCurly) // anonymous, don't export
				{
					ExitCurrentScope(Generator);
				}
				else
				{
					Generator.Scopes.push_back(Name);
				}
			}
			else if (IsTokenNamed(T, "template"))
			{
				Tok It = RequireToken(Generator, TokType::Less);

				Tok TemplateArgsStart = RequireToken(Generator, TokType::Identifier);
				while (It.Type != TokType::More)
				{
					It = GetToken(Generator);
				}
				Tok TemplateArgsEnd = It;

				Tok Type = RequireToken(Generator, TokType::Identifier);
				if (IsTokenNamed(Type, "struct") || IsTokenNamed(Type, "class"))
				{
					Tok Name = RequireToken(Generator, TokType::Identifier);

					while (It.Type != TokType::OpenCurly && It.Type != TokType::Semicolon)
					{
						It = GetToken(Generator);
					}

					Generator.Scopes.push_back(Name);
					if (It.Type == TokType::OpenCurly)
					{
						StringView TemplateParams = StringView(TemplateArgsStart.Text.data(), TemplateArgsEnd.Text.data() - TemplateArgsStart.Text.data());
						ParseStruct(Generator, Name.Text, TemplateParams, IsTokenNamed(Type, "struct"));
					}
				}
				else
				{
					while (It.Type != TokType::OpenCurly && It.Type != TokType::Semicolon)
					{
						It = GetToken(Generator);
					}
					if (It.Type == TokType::OpenCurly)
					{
						ExitCurrentScope(Generator);
					}
				}
			}
			else if (IsTokenNamed(T, "typedef"))
			{
				Tok It = RequireToken(Generator, TokType::Identifier);
				if (IsTokenNamed(It, "struct"))
				{
					RequireToken(Generator, TokType::OpenCurly);
					ExitCurrentScope(Generator);
					RequireToken(Generator, TokType::Identifier);
					RequireToken(Generator, TokType::Semicolon);
				}
			}
			else if (IsTokenNamed(T, "class") || IsTokenNamed(T, "struct"))
			{
				Tok StructName = RequireToken(Generator, TokType::Identifier);
				Tok It = GetToken(Generator);
				if (It.Type == TokType::Semicolon)
				{
					continue;
				}

				Generator.Scopes.push_back(StructName);
				String DeclarationToPrint = String(T.Text.data(), It.Text.data() - T.Text.data());
				DeclarationToPrint.erase(DeclarationToPrint.find_last_not_of('\n') + 1);
				DeclarationToPrint.erase(DeclarationToPrint.find_last_not_of('\r') + 1);
				//DeclarationToPrint.trim();
				DeclarationToPrint.append(";\n");

				fwrite(DeclarationToPrint.data(), 1, DeclarationToPrint.size(), DeclarationsFile);
				ParseStruct(Generator, StructName.Text, "", IsTokenNamed(T, "struct"));
			}
			else if (IsTokenNamed(T, "enum"))
			{
				Tok EnumName = RequireToken(Generator, TokType::Identifier);
				if (IsTokenNamed(EnumName, "class"))
				{
					EnumName = RequireToken(Generator, TokType::Identifier);
				}

				Tok It = GetToken(Generator);
				if (It.Type == TokType::Colon)
				{
					RequireToken(Generator, TokType::Identifier);
					It = GetToken(Generator);
				}
				if (It.Type == TokType::Semicolon)
				{
					continue;
				}

				CHECK(It.Type == TokType::OpenCurly);

				Generator.Scopes.push_back(EnumName);

				String DeclarationToPrint = String(T.Text.data(), It.Text.data() - T.Text.data());
				DeclarationToPrint.erase(DeclarationToPrint.find_last_not_of('\n') + 1);
				DeclarationToPrint.erase(DeclarationToPrint.find_last_not_of('\r') + 1);
				//DeclarationToPrint.trim();
				DeclarationToPrint.append(";\n");
				fwrite(DeclarationToPrint.data(), 1, DeclarationToPrint.size(), DeclarationsFile);

				ParseEnum(Generator, EnumName.Text);
			}
			break;
		}
		default:
			break;
		}
	}
	char Suffix[] = "}\n";
	fwrite(Suffix, 1, sizeof(Suffix) - 1, OutputFile);
	fclose(OutputFile);
}
