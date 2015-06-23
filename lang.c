// lang.c -- a language which compiles to mint vm bytecode
/* 
 * TODO:
 * - virtual machine knows how many arguments each function takes; stop supplying it as integers in the bytecode (fix CompileExpr, ExecuteCycle)
 * - enums
 * 
 * PARTIALLY COMPLETE (I.E NOT FULLY SATISFIED WITH SOLUTION):
 * - using compound binary operators causes an error
 * - proper operators: need more operators
 * - include/require other scripts (copy bytecode into vm at runtime?): can only include things at compile time (with cmd line)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <stdarg.h>

#include "vm.h"

#define MAX_LEX_LENGTH 256
#define MAX_ID_NAME_LENGTH 64
#define MAX_SCOPES 32
#define MAX_ARGS 32 

int LineNumber = 0;
const char* FileName = NULL;

int NumNumbers = 0;
int NumStrings = 0;

int NumGlobals = 0;
int NumFunctions = 0;
int NumExterns = 0;
int EntryPoint = 0;

Word* Code = NULL;
int CodeCapacity = 0;
int CodeLength = 0;

struct
{
	int* lineNumbers;
	int* fileNameIndices;
	int length;
	int capacity;
} Debug = { NULL, NULL, 0, 0 };

void ErrorExit(const char* format, ...)
{
	fprintf(stderr, "Error (%s:%i): ", FileName, LineNumber);
	
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	
	exit(1);
}

void _AppendCode(Word code, int line, int fileNameIndex)
{	
	while(CodeLength + 1 >= CodeCapacity)
	{
		if(!Code) CodeCapacity = 8;
		else CodeCapacity *= 2;
		
		void* newCode = realloc(Code, sizeof(Word) * CodeCapacity);
		assert(newCode);
		Code = newCode;
	}

	Code[CodeLength++] = code;
	
	while(Debug.length + 1 >= Debug.capacity)
	{
		if(Debug.capacity == 0) Debug.capacity = 8;
		else Debug.capacity *= 2;
		
		int* newLineNumbers = realloc(Debug.lineNumbers, sizeof(int) * Debug.capacity);
		assert(newLineNumbers);
		Debug.lineNumbers = newLineNumbers;
		
		int* newFileNameIndices = realloc(Debug.fileNameIndices, sizeof(int) * Debug.capacity);
		assert(newFileNameIndices);
		Debug.fileNameIndices = newFileNameIndices;
	}
	
	Debug.lineNumbers[Debug.length] = line;
	Debug.fileNameIndices[Debug.length] = fileNameIndex;
	Debug.length += 1;
}

#define AppendCode(code) _AppendCode((code), exp->line, RegisterString(exp->file)->index)

void _AppendInt(int value, int line, int file)
{
	Word* code = (Word*)(&value);
	for(int i = 0; i < sizeof(int) / sizeof(Word); ++i)
		_AppendCode(*code++, line, file);
}

#define AppendInt(value) _AppendInt((value), exp->line, RegisterString(exp->file)->index)

void _AllocatePatch(int length, int line, int file)
{
	for(int i = 0; i < length; ++i)
		_AppendCode(0, line, file);
}

#define AllocatePatch(length) _AllocatePatch((length), exp->line, RegisterString(exp->file)->index)

void EmplaceInt(int loc, int value)
{
	if(loc >= CodeLength)
		ErrorExit("Compiler error: attempted to emplace in outside of code array\n");
		
	Word* code = (Word*)(&value);
	for(int i = 0; i < sizeof(int) / sizeof(Word); ++i)
		Code[loc + i] = *code++;
}

typedef enum
{
	CONST_NUM,
	CONST_STR,
} ConstType;

typedef struct _ConstDecl
{
	struct _ConstDecl* next;
	ConstType type;
	int index;
	
	union
	{
		double number;
		char string[MAX_LEX_LENGTH];
	};
} ConstDecl;

struct _Expr;
typedef struct _FuncDecl
{	
	struct _FuncDecl* next;
	
	char isExtern;
	
	char name[MAX_ID_NAME_LENGTH];
	int index;
	int numLocals;
	Word numArgs;
	int pc;
	
	char hasEllipsis;
} FuncDecl;

typedef struct _VarDecl
{
	struct _VarDecl* next;
	
	char isGlobal;
	char name[MAX_ID_NAME_LENGTH];
	int index;
	
	int type;
} VarDecl;

ConstDecl* Constants = NULL;
ConstDecl* ConstantsCurrent = NULL;

FuncDecl* CurFunc = NULL;

FuncDecl* Functions = NULL;
FuncDecl* FunctionsCurrent = NULL;

VarDecl* VarStack[MAX_SCOPES];
int VarStackDepth = 0;


/* BINARY FORMAT:
entry point as integer

program length (in words) as integer
program code (must be no longer than program length specified previously)

number of global variables as integer
names of global variables as string lengths followed by characters

number of functions as integer
function entry points as integers
number of arguments to each function as integers
function names [string length followed by string as chars]

number of external functions referenced as integer
string length followed by string as chars (names of external functions as referenced in the code)

number of number constants as integer
number constants as doubles

number of string constants
string length followed by string as chars

whether the program has code metadata (i.e line numbers and file names for errors) (represented by char)
if so (length of each of the arrays below must be the same as program length):
line numbers mapping to pcs
file names as indices into string table (integers)
*/

void OutputCode(FILE* out)
{
	printf("=================================\n");
	printf("Summary:\n");
	printf("Entry point: %i\n", EntryPoint);
	printf("Program length: %i\n", CodeLength);
	printf("Number of global variables: %i\n", NumGlobals);
	printf("Number of functions: %i\n", NumFunctions);
	printf("Number of externs: %i\n", NumExterns);
	printf("Number of numbers: %i\n", NumNumbers);
	printf("Number of strings: %i\n", NumStrings);
	printf("=================================\n");
	
	
	fwrite(&EntryPoint, sizeof(int), 1, out);
	
	fwrite(&CodeLength, sizeof(int), 1, out);
	fwrite(Code, sizeof(Word), CodeLength, out);
	
	fwrite(&NumGlobals, sizeof(int), 1, out);
	for(VarDecl* decl = VarStack[0]; decl != NULL; decl = decl->next)
	{
		int len = strlen(decl->name);
		fwrite(&len, sizeof(int), 1, out);
		fwrite(decl->name, sizeof(char), len, out);
	}
	
	fwrite(&NumFunctions, sizeof(int), 1, out);	
	for(FuncDecl* decl = Functions; decl != NULL; decl = decl->next)
	{
		if(!decl->isExtern)
			fwrite(&decl->pc, sizeof(int), 1, out);
	}
	for(FuncDecl* decl = Functions; decl != NULL; decl = decl->next)
	{
		if(!decl->isExtern)
			fwrite(&decl->numArgs, sizeof(int), 1, out);
	}
	for(FuncDecl* decl = Functions; decl != NULL; decl = decl->next)
	{
		if(!decl->isExtern)
		{
			int len = strlen(decl->name);
			fwrite(&len, sizeof(int), 1, out);
			fwrite(decl->name, sizeof(char), len, out);
		}
	}
	
	fwrite(&NumExterns, sizeof(int), 1, out);	
	for(FuncDecl* decl = Functions; decl != NULL; decl = decl->next)
	{
		if(decl->isExtern)
		{
			int len = strlen(decl->name);
			fwrite(&len, sizeof(int), 1, out);
			fwrite(decl->name, sizeof(char), len, out);
		}
	}
	
	fwrite(&NumNumbers, sizeof(int), 1, out);
	for(ConstDecl* decl = Constants; decl != NULL; decl = decl->next)
	{
		if(decl->type == CONST_NUM)
			fwrite(&decl->number, sizeof(double), 1, out);
	}
	
	fwrite(&NumStrings, sizeof(int), 1, out);
	for(ConstDecl* decl = Constants; decl != NULL; decl = decl->next)
	{
		if(decl->type == CONST_STR)
		{
			int len = strlen(decl->string);
			fwrite(&len, sizeof(int), 1, out);
			fwrite(decl->string, sizeof(char), len, out);
		}
	}
	
	char hasCodeMetadata = 1;
	fwrite(&hasCodeMetadata, sizeof(char), 1, out);
	fwrite(Debug.lineNumbers, sizeof(int), Debug.length, out);
	fwrite(Debug.fileNameIndices, sizeof(int), Debug.length, out);
}

ConstDecl* RegisterNumber(double number)
{
	for(ConstDecl* decl = Constants; decl != NULL; decl = decl->next)
	{
		if(decl->type == CONST_NUM)
		{
			if(decl->number == number)
				return decl;
		}
	}
	
	ConstDecl* decl = malloc(sizeof(ConstDecl));
	assert(decl);
	
	decl->next = NULL;
	
	if(!Constants)
	{
		Constants = decl;
		ConstantsCurrent = Constants;
	}
	else
	{
		ConstantsCurrent->next = decl;
		ConstantsCurrent = decl;
	}

	decl->type = CONST_NUM;
	decl->number = number;
	decl->index = NumNumbers++;	
	
	return decl;
}

ConstDecl* RegisterString(const char* string)
{
	for(ConstDecl* decl = Constants; decl != NULL; decl = decl->next)
	{
		if(decl->type == CONST_STR)
		{
			if(strcmp(decl->string, string) == 0)
				return decl;
		}
	}
	
	ConstDecl* decl = malloc(sizeof(ConstDecl));
	assert(decl);
	
	decl->next = NULL;
	
	if(!Constants)
	{
		Constants = decl;
		ConstantsCurrent = Constants;
	}
	else
	{
		ConstantsCurrent->next = decl;
		ConstantsCurrent = decl;
	}

	decl->type = CONST_STR;
	strcpy(decl->string, string);
	decl->index = NumStrings++;
	
	return decl;
}

FuncDecl* RegisterExtern(const char* name)
{
	for(FuncDecl* decl = Functions; decl != NULL; decl = decl->next)
	{
		if(strcmp(decl->name, name) == 0)
			return decl;
	}
	
	FuncDecl* decl = malloc(sizeof(FuncDecl));
	assert(decl);
	
	decl->next = NULL;
	
	if(!Functions)
	{
		Functions = decl;
		FunctionsCurrent = Functions;
	}
	else
	{
		FunctionsCurrent->next = decl;
		FunctionsCurrent = decl;
	}
	
	decl->isExtern = 1;
	
	strcpy(decl->name, name);
	
	decl->index = NumExterns++;
	decl->numArgs = 0;
	decl->numLocals = 0;
	decl->pc = -1;
	decl->hasEllipsis = 0;
	
	return decl;
}

void PushScope()
{
	VarStack[++VarStackDepth] = NULL;
}

void PopScope()
{
	--VarStackDepth;
}

FuncDecl* DeclareFunction(const char* name)
{
	FuncDecl* decl = malloc(sizeof(FuncDecl));
	assert(decl);

	decl->next = NULL;

	if(!Functions)
	{
		Functions = decl;
		FunctionsCurrent = Functions;
	}
	else
	{
		FunctionsCurrent->next = decl;
		FunctionsCurrent = decl;
	}
	
	decl->isExtern = 0;
	decl->hasEllipsis = 0;
	
	strcpy(decl->name, name);
	decl->index = NumFunctions++;
	decl->numArgs = 0;
	decl->numLocals = 0;
	decl->pc = -1;
	
	return decl;
}

FuncDecl* EnterFunction(const char* name)
{
	CurFunc = DeclareFunction(name);	
	return CurFunc;
}

void ExitFunction()
{
	CurFunc = NULL;
}

FuncDecl* ReferenceFunction(const char* name)
{
	for(FuncDecl* decl = Functions; decl != NULL; decl = decl->next)
	{
		if(strcmp(decl->name, name) == 0)
			return decl;
	}
	
	return NULL;
}

VarDecl* RegisterVariable(const char* name)
{	
	VarDecl* decl = malloc(sizeof(VarDecl));
	assert(decl);
	
	decl->next = VarStack[VarStackDepth];
	VarStack[VarStackDepth] = decl;
	
	decl->isGlobal = 0;
	
	strcpy(decl->name, name);
	if(!CurFunc)
	{	
		decl->index = NumGlobals++;
		decl->isGlobal = 1;
	}
	else
		decl->index = CurFunc->numLocals++;
	
	decl->type = -1;
	
	return decl;
}

void RegisterArgument(const char* name)
{
	if(!CurFunc)
		ErrorExit("(INTERNAL) Must be inside function when registering arguments\n");
	
	VarDecl* decl = RegisterVariable(name);
	--CurFunc->numLocals;
	decl->index = -(++CurFunc->numArgs);
}

VarDecl* ReferenceVariable(const char* name)
{
	for(int i = VarStackDepth; i >= 0; --i)
	{
		for(VarDecl* decl = VarStack[i]; decl != NULL; decl = decl->next)
		{
			if(strcmp(decl->name, name) == 0)
				return decl;
		}
	}
	
	return NULL;
}

enum
{
	TOK_IDENT = -1,
	TOK_NUMBER = -2,
	TOK_EOF = -3,
	TOK_VAR = -4,
	TOK_WHILE = -5,
	TOK_END = -6,
	TOK_FUNC = -7,
	TOK_IF = -8,
	TOK_RETURN = -9,
	TOK_EXTERN = -10,
	TOK_STRING = -11,
	TOK_EQUALS = -12,
	TOK_LTE = -13,
	TOK_GTE = -14,
	TOK_NOTEQUAL = -15,
	TOK_HEXNUM = -16,
	TOK_FOR = -17,
	TOK_ELSE = -18,
	TOK_ELIF = -19,
	TOK_AND = -20,
	TOK_OR = -21,
	TOK_LSHIFT = -22,
	TOK_RSHIFT = -23,
	TOK_NULL = -24,
	TOK_INLINE = -25,
	TOK_LAMBDA = -26,
	TOK_CADD = -27,
	TOK_CSUB = -28,
	TOK_CMUL = -29,
	TOK_CDIV = -30,
	TOK_CONTINUE = -31,
	TOK_BREAK = -32,
	TOK_ELLIPSIS = -33,
	TOK_NEW = -34,
};

char Lexeme[MAX_LEX_LENGTH];
int CurTok = 0;
char ResetLex = 0;

int GetToken(FILE* in)
{
	static char last = ' ';
	
	if(ResetLex)
	{
		last = ' ';
		ResetLex = 0;
	}
	
	while(isspace(last))
	{
		if(last == '\n') ++LineNumber;
		last = getc(in);
	}
	
	if(isalpha(last) || last == '_')
	{
		int i = 0;
		while(isalnum(last) || last == '_')
		{
			Lexeme[i++] = last;
			last = getc(in);
		}
		Lexeme[i] = '\0';
		
		if(strcmp(Lexeme, "var") == 0) return TOK_VAR;
		if(strcmp(Lexeme, "while") == 0) return TOK_WHILE;
		if(strcmp(Lexeme, "end") == 0) return TOK_END;
		if(strcmp(Lexeme, "func") == 0) return TOK_FUNC;
		if(strcmp(Lexeme, "if") == 0) return TOK_IF;
		if(strcmp(Lexeme, "return") == 0) return TOK_RETURN;
		if(strcmp(Lexeme, "extern") == 0) return TOK_EXTERN;
		if(strcmp(Lexeme, "true") == 0)
		{
			Lexeme[0] = '1';
			Lexeme[1] = '\0';
			return TOK_NUMBER;
		}
		if(strcmp(Lexeme, "false") == 0)
		{
			Lexeme[0] = '0';
			Lexeme[1] = '\0';
			return TOK_NUMBER;
		}
		if(strcmp(Lexeme, "for") == 0) return TOK_FOR;
		if(strcmp(Lexeme, "else") == 0) return TOK_ELSE;
		if(strcmp(Lexeme, "elif") == 0) return TOK_ELIF;
		if(strcmp(Lexeme, "null") == 0) return TOK_NULL;
		if(strcmp(Lexeme, "inline") == 0) return TOK_INLINE;
		if(strcmp(Lexeme, "lambda") == 0) return TOK_LAMBDA;
		if(strcmp(Lexeme, "continue") == 0) return TOK_CONTINUE;
		if(strcmp(Lexeme, "break") == 0) return TOK_BREAK;
		if(strcmp(Lexeme, "new") == 0) return TOK_NEW;
		
		return TOK_IDENT;
	}
		
	if(isdigit(last))
	{	
		int i = 0;
		while(isdigit(last) || last == '.')
		{
			Lexeme[i++] = last;
			last = getc(in);
		}
		Lexeme[i] = '\0';
		
		return TOK_NUMBER;
	}

	if(last == '$')
	{
		last = getc(in);
		int i = 0;
		while(isxdigit(last))
		{
			Lexeme[i++] = last;
			last = getc(in);
		}
		Lexeme[i] = '\0';
		
		return TOK_HEXNUM;
	}
	
	if(last == '"')
	{
		int i = 0;
		last = getc(in);
	
		while(last != '"')
		{
			if(last == '\\')
			{
				last = getc(in);
				switch(last)
				{
					case 'n': last = '\n'; break;
					case 'r': last = '\r'; break;
					case 't': last = '\t'; break;
					case '"': last = '"'; break;
					case '\'': last = '\''; break;
					case '\\': last = '\\'; break;
				}
			}
			
			Lexeme[i++] = last;
			last = getc(in);
		}
		Lexeme[i] = '\0';
		last = getc(in);
		
		return TOK_STRING;
	}
	
	if(last == '\'')
	{
		last = getc(in);

		if(last == '\\')
		{
			last = getc(in);
			switch(last)
			{
				case 'n': last = '\n'; break;
				case 'r': last = '\r'; break;
				case 't': last = '\t'; break;
				case '"': last = '"'; break;
				case '\'': last = '\''; break;
				case '\\': last = '\\'; break;
			}
		}

		sprintf(Lexeme, "%i", last);
		last = getc(in);
			
		if(last != '\'')
			ErrorExit("Expected ' after previous '\n");
		
		last = getc(in);
		return TOK_NUMBER;
	}
	
	if(last == '#')
	{
		last = getc(in);
		if(last == '~')
		{
			while(last != EOF)
			{
				last = getc(in);
				if(last == '\n' || last == '\r') ++LineNumber;
				if(last == '~')
				{
					last = getc(in);
					if(last == '#')
						break;
				}
			}
		}
		else
		{
			while(last != '\n' && last != '\r' && last != EOF)
				last = getc(in);
		}
		if(last == EOF) return TOK_EOF;
		else return GetToken(in);
	}
	
	if(last == EOF)
		return TOK_EOF;
	
	int lastChar = last;
	last = getc(in);
	
	if(lastChar == '=' && last == '=')
	{
		last = getc(in);
		return TOK_EQUALS;
	}
	else if(lastChar == '!' && last == '=')
	{
		last = getc(in);
		return TOK_NOTEQUAL;
	}
	else if(lastChar == '<' && last == '=')
	{
		last = getc(in);
		return TOK_LTE;
	}
	else if(lastChar == '>' && last == '=')
	{
		last = getc(in);
		return TOK_GTE;
	}
	else if(lastChar == '<' && last == '<')
	{
		last = getc(in);
		return TOK_LSHIFT;
	}
	else if(lastChar == '>' && last == '>')
	{
		last = getc(in);
		return TOK_RSHIFT;
	}
	else if(lastChar == '&' && last == '&')
	{
		last = getc(in);
		return TOK_AND;
	}
	else if(lastChar == '|' && last == '|')
	{
		last = getc(in);
		return TOK_OR;
	}
	else if(lastChar == '+' && last == '=')
	{
		last = getc(in);
		return TOK_CADD;
	}
	else if(lastChar == '-' && last == '=')
	{
		last = getc(in);
		return TOK_CSUB;
	}
	else if(lastChar == '*' && last == '=')
	{
		last = getc(in);
		return TOK_CMUL;
	}
	else if(lastChar == '/' && last == '=')
	{
		last = getc(in);
		return TOK_CDIV;
	}
	else if(lastChar == '.' && last == '.')
	{
		last = getc(in);
		if(lastChar != '.')
			ErrorExit("Invalid token '...'\n");
		last = getc(in);
		return TOK_ELLIPSIS;
	}
	
	return lastChar;
}

typedef enum
{
	EXP_NUMBER,
	EXP_STRING,
	EXP_IDENT,
	EXP_CALL,
	EXP_VAR,
	EXP_BIN,
	EXP_PAREN,
	EXP_WHILE,
	EXP_FUNC,
	EXP_IF,
	EXP_RETURN,
	EXP_EXTERN,
	EXP_ARRAY_LITERAL,
	EXP_ARRAY_INDEX,
	EXP_UNARY,
	EXP_FOR,
	EXP_DOT,
	EXP_DICT_LITERAL,
	EXP_NULL,
	EXP_CONTINUE,
	EXP_BREAK,
	EXP_COLON,
	EXP_NEW,
	EXP_LINKED_BINARY_CODE
} ExprType;

typedef struct _Expr
{
	struct _Expr* next;
	ExprType type;
	const char* file;
	int line;
	FuncDecl* curFunc;
	
	union
	{
		ConstDecl* constDecl; // NOTE: for both EXP_NUMBER and EXP_STRING
		struct { VarDecl* varDecl; char name[MAX_ID_NAME_LENGTH]; } varx; // NOTE: for both EXP_IDENT and EXP_VAR
		struct { struct _Expr* args[MAX_ARGS]; int numArgs; struct _Expr* func; } callx;
		struct { struct _Expr *lhs, *rhs; int op; } binx;
		struct _Expr* parenExpr;
		struct { struct _Expr *cond, *bodyHead; } whilex;
		struct { FuncDecl* decl; struct _Expr* bodyHead; } funcx;
		FuncDecl* extDecl;
		struct { struct _Expr *cond, *bodyHead, *alt; } ifx;
		struct _Expr* retExpr;
		struct { struct _Expr* head; int length; } arrayx;
		struct { struct _Expr* arrExpr; struct _Expr* indexExpr; } arrayIndex;
		struct { int op; struct _Expr* expr; } unaryx;
		struct { struct _Expr *init, *cond, *iter, *bodyHead; } forx;
		struct { struct _Expr* dict; char name[MAX_ID_NAME_LENGTH]; } dotx;
		struct { struct _Expr* pairsHead; int length; } dictx;
		struct { struct _Expr* dict; char name[MAX_ID_NAME_LENGTH]; } colonx;
		struct _Expr* newExpr;
		struct { Word* bytes; int length; FuncDecl** toBeRetargeted; int* pcFileTable; int* pcLineTable; int numFunctions; } code;
	};
} Expr;

void ErrorExitE(Expr* exp, const char* format, ...)
{
	fprintf(stderr, "Error (%s:%i): ", exp->file, exp->line);
	
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	
	exit(1);
}

int GetNextToken(FILE* in)
{
	CurTok = GetToken(in);
	return CurTok;
}

Expr* CreateExpr(ExprType type)
{
	Expr* exp = malloc(sizeof(Expr));
	assert(exp);
	
	exp->next = NULL;
	exp->type = type;
	exp->file = FileName;
	exp->line = LineNumber;
	exp->curFunc = CurFunc;

	return exp;
}

Expr* ParseExpr(FILE* in);

Expr* ParseIf(FILE* in)
{
	GetNextToken(in);
	
	Expr* exp = CreateExpr(EXP_IF);
	
	Expr* cond = ParseExpr(in);
				
	PushScope();
	
	Expr* exprHead = NULL;
	Expr* exprCurrent = NULL;
	
	while(CurTok != TOK_ELSE && CurTok != TOK_ELIF && CurTok != TOK_END)
	{
		if(!exprHead)
		{
			exprHead = ParseExpr(in);
			exprCurrent = exprHead;
		}
		else
		{
			exprCurrent->next = ParseExpr(in);
			exprCurrent = exprCurrent->next;
		}
	}
	
	exp->ifx.cond = cond;
	exp->ifx.bodyHead = exprHead;
	exp->ifx.alt = NULL;
	
	PopScope();
	
	if(CurTok == TOK_ELIF)
		exp->ifx.alt = ParseIf(in);
	else if(CurTok == TOK_ELSE)
	{
		GetNextToken(in);
	
		PushScope();
		
		Expr* exprHead = NULL;
		Expr* exprCurrent = NULL;
		
		while(CurTok != TOK_END)
		{
			if(!exprHead)
			{
				exprHead = ParseExpr(in);
				exprCurrent = exprHead;
			}
			else
			{
				exprCurrent->next = ParseExpr(in);
				exprCurrent = exprCurrent->next;
			}
		}
		GetNextToken(in);
		
		exp->ifx.alt = exprHead;
		
		PopScope();
	}
	else if(CurTok == TOK_END)
	{
		GetNextToken(in);
		exp->ifx.alt = NULL;
	}
	
	return exp;
}

Expr* ParseFactor(FILE* in)
{
	switch(CurTok)
	{
		case TOK_NUMBER:
		{
			Expr* exp = CreateExpr(EXP_NUMBER);
			exp->constDecl = RegisterNumber(strtod(Lexeme, NULL));
			
			GetNextToken(in);
			
			return exp;
		} break;
		
		case TOK_HEXNUM:
		{
			Expr* exp = CreateExpr(EXP_NUMBER);
			exp->constDecl = RegisterNumber(strtol(Lexeme, NULL, 16));
			
			GetNextToken(in);
			
			return exp;
		} break;
		
		case TOK_STRING:
		{
			Expr* exp = CreateExpr(EXP_STRING);
			exp->constDecl = RegisterString(Lexeme);
			
			GetNextToken(in);
			
			return exp;
		} break;
		
		case TOK_NULL:
		{
			GetNextToken(in);
			Expr* exp = CreateExpr(EXP_NULL);
			return exp;
		} break;
		
		case TOK_NEW:
		{
			GetNextToken(in);
			Expr* exp = CreateExpr(EXP_NEW);
			exp->newExpr = ParseExpr(in);
			return exp;
		} break;
		
		case TOK_CONTINUE:
		{
			GetNextToken(in);
			Expr* exp = CreateExpr(EXP_CONTINUE);
			return exp;
		} break;
		
		case TOK_BREAK:
		{
			GetNextToken(in);
			Expr* exp = CreateExpr(EXP_BREAK);
			return exp;
		} break;
		
		case '[':
		{
			GetNextToken(in);
			
			Expr* exp = CreateExpr(EXP_ARRAY_LITERAL);
			
			Expr* exprHead = NULL;
			Expr* exprCurrent = NULL;
			int len = 0;
			while(CurTok != ']')
			{
				if(!exprHead)
				{
					exprHead = ParseExpr(in);
					exprCurrent = exprHead;
				}
				else
				{
					exprCurrent->next = ParseExpr(in);
					exprCurrent = exprCurrent->next;
				}
				if(CurTok == ',') GetNextToken(in);
				++len;
			}			
			GetNextToken(in);
			exp->arrayx.head = exprHead;
			exp->arrayx.length = len;
			
			return exp;
		} break;
		
		case '{':
		{
			GetNextToken(in);
			
			Expr* exp = CreateExpr(EXP_DICT_LITERAL);
			
			Expr* exprHead = NULL;
			Expr* exprCurrent = NULL;
			int len = 0;
			while(CurTok != '}')
			{
				if(!exprHead)
				{
					exprHead = ParseExpr(in);
					exprCurrent = exprHead;
				}
				else
				{
					exprCurrent->next = ParseExpr(in);
					exprCurrent = exprCurrent->next;
				}
				if(CurTok == ',') GetNextToken(in);
				++len;
			}			
			GetNextToken(in);
			exp->dictx.pairsHead = exprHead;
			exp->dictx.length = len;
			
			return exp;
		} break;
		
		case TOK_IDENT:
		{
			Expr* exp = CreateExpr(EXP_IDENT);
			
			char name[MAX_ID_NAME_LENGTH];
			strcpy(name, Lexeme);
			
			GetNextToken(in);
			
			exp->varx.varDecl = ReferenceVariable(name);
			strcpy(exp->varx.name, name);
			return exp;		
		} break;
		
		case TOK_VAR:
		{
			Expr* exp = CreateExpr(EXP_VAR);
			
			GetNextToken(in);
			
			if(CurTok != TOK_IDENT)
				ErrorExit("Expected ident after 'var' but received something else\n");
						
			exp->varx.varDecl = RegisterVariable(Lexeme);
			strcpy(exp->varx.name, Lexeme);
			
			GetNextToken(in);
			
			return exp;
		} break;
		
		case '(':
		{
			Expr* exp = CreateExpr(EXP_PAREN);
			GetNextToken(in);
			
			exp->parenExpr = ParseExpr(in);
			
			if(CurTok != ')')
				ErrorExit("Expected ')' after previous '('\n");
			
			GetNextToken(in);
			
			return exp;
		} break;
		
		case TOK_WHILE:
		{
			Expr* exp = CreateExpr(EXP_WHILE);
			GetNextToken(in);
			
			exp->whilex.cond = ParseExpr(in);
		
			Expr* exprHead = NULL;
			Expr* exprCurrent = NULL;
			
			PushScope();
			while(CurTok != TOK_END)
			{
				if(!exprHead)
				{
					exprHead = ParseExpr(in);
					exprCurrent = exprHead;
				}
				else
				{
					exprCurrent->next = ParseExpr(in);
					exprCurrent = exprCurrent->next;
				}
			}
			PopScope();
			GetNextToken(in);
			
			exp->whilex.bodyHead = exprHead;
			
			return exp;
		} break;
		
		case TOK_FOR:
		{
			Expr* exp = CreateExpr(EXP_FOR);
			GetNextToken(in);
			
			PushScope();
			exp->forx.init = ParseExpr(in);
			
			if(CurTok != ',')
				ErrorExit("Expected ',' after for initial expression\n");
			GetNextToken(in);
			
			exp->forx.cond = ParseExpr(in);
			
			if(CurTok != ',')
				ErrorExit("Expected ',' after for condition\n");
			GetNextToken(in);
				
			exp->forx.iter = ParseExpr(in);
			
			Expr* exprHead = NULL;
			Expr* exprCurrent = NULL;
			
			while(CurTok != TOK_END)
			{
				if(!exprHead)
				{
					exprHead = ParseExpr(in);
					exprCurrent = exprHead;
				}
				else
				{
					exprCurrent->next = ParseExpr(in);
					exprCurrent = exprCurrent->next;
				}
			}
			GetNextToken(in);
			PopScope();
		
			exp->forx.bodyHead = exprHead;
			
			return exp;
		} break;
		
		case TOK_FUNC:
		{
			Expr* exp = CreateExpr(EXP_FUNC);
			GetNextToken(in);
			
			char name[MAX_ID_NAME_LENGTH];
			if(CurTok != TOK_IDENT)
			{
				ErrorExit("Expected identifier after 'func'\n");
				
			}
			strcpy(name, Lexeme);
			
			GetNextToken(in);
			
			if(CurTok != '(')
				ErrorExit("Expected '(' after 'func' %s\n", name);
			GetNextToken(in);
			
			FuncDecl* decl = EnterFunction(name);
			
			char args[MAX_ARGS][MAX_ID_NAME_LENGTH];
			int numArgs = 0;
			
			while(CurTok != ')')
			{	
				if(CurTok == TOK_ELLIPSIS)
				{
					decl->hasEllipsis = 1;
					GetNextToken(in);
					if(CurTok != ')')
						ErrorExit("Attempted to place ellipsis in the middle of an argument list\n");
					break;
				}
				
				if(CurTok != TOK_IDENT)
					ErrorExit("Expected identifier/ellipsis in argument list for function '%s' but received something else (%c, %i)\n", name, CurTok, CurTok);
				
				strcpy(args[numArgs++], Lexeme);
				GetNextToken(in);
			
				if(CurTok == ',') GetNextToken(in);
				else if(CurTok != ')') ErrorExit("Expected ',' or ')' in function '%s' argument list\n", name);
			}
			GetNextToken(in);
			
			PushScope();
			
			for(int i = 0; i < numArgs; ++i)
				RegisterArgument(args[i]);
			if(decl->hasEllipsis)
			{
				RegisterArgument("args");
				--decl->numArgs;
			}
			
			Expr* exprHead = NULL;
			Expr* exprCurrent = NULL;
			
			while(CurTok != TOK_END)
			{
				if(!exprHead)
				{
					exprHead = ParseExpr(in);
					exprCurrent = exprHead;
				}
				else
				{
					exprCurrent->next = ParseExpr(in);
					exprCurrent = exprCurrent->next;
				}
			}
			GetNextToken(in);
			ExitFunction();
			PopScope();
			
			exp->funcx.decl = decl;
			exp->funcx.bodyHead = exprHead;
			
			return exp;
		} break;
		
		case TOK_IF:
		{
			return ParseIf(in);
		} break;
		
		case TOK_RETURN:
		{
			if(CurFunc)
			{
				Expr* exp = CreateExpr(EXP_RETURN);
				GetNextToken(in);
				if(CurTok != ';')
				{
					exp->retExpr = ParseExpr(in);
					return exp;
				}
				
				GetNextToken(in);
			
				exp->retExpr = NULL;
				return exp;	
			}
			
			ErrorExit("WHAT ARE YOU TRYING TO RETURN FROM! YOU'RE NOT EVEN INSIDE A FUNCTION BODY!\n");
			
			
			return NULL;
		} break;
		
		case TOK_EXTERN:
		{
			Expr* exp = CreateExpr(EXP_EXTERN);
			GetNextToken(in);
			
			exp->extDecl = RegisterExtern(Lexeme);
			
			GetNextToken(in);
			
			return exp;
		} break;
	};
	
	ErrorExit("Unexpected token '%c' (%i) (%s)\n", CurTok, CurTok, Lexeme);
	
	return NULL;
}

char IsPostOperator()
{
	switch(CurTok)
	{
		case '[': case '.':  case '(': case ':':
			return 1;
	}
	return 0;
}

Expr* ParsePost(FILE* in, Expr* pre)
{
	switch(CurTok)
	{
		case '[':
		{
			GetNextToken(in);
			
			Expr* exp = CreateExpr(EXP_ARRAY_INDEX);
			exp->arrayIndex.arrExpr = pre;
			exp->arrayIndex.indexExpr = ParseExpr(in);
			if(CurTok != ']')
				ErrorExit("Expected ']' after previous '['\n");
			GetNextToken(in);
			if(!IsPostOperator()) return exp;
			return ParsePost(in, exp);
		} break;
		
		case '.':
		{
			GetNextToken(in);
			
			if(CurTok != TOK_IDENT)
				ErrorExit("Expected identfier after '.'\n");
			
			Expr* exp = CreateExpr(EXP_DOT);
			strcpy(exp->dotx.name, Lexeme);
			exp->dotx.dict = pre;
			
			GetNextToken(in);
			if(!IsPostOperator()) return exp;
			return ParsePost(in, exp);
		} break;
		
		case '(':
		{
			GetNextToken(in);
			
			Expr* exp = CreateExpr(EXP_CALL);
			
			exp->callx.numArgs = 0;
			exp->callx.func = pre;
			
			while(CurTok != ')')
			{
				exp->callx.args[exp->callx.numArgs++] = ParseExpr(in);
				if(CurTok == ',') GetNextToken(in);
			}
			
			GetNextToken(in);
			
			if(!IsPostOperator()) return exp;
			else return ParsePost(in, exp);
		} break;
		
		case ':':
		{
			GetNextToken(in);
			
			if(CurTok != TOK_IDENT)
				ErrorExit("Expected identfier after '.'\n");
			
			Expr* exp = CreateExpr(EXP_COLON);
			strcpy(exp->colonx.name, Lexeme);
			exp->colonx.dict = pre;
			
			GetNextToken(in);
			if(!IsPostOperator()) return exp;
			return ParsePost(in, exp);
		} break;
		
		default:
			return pre;
	}
}

Expr* ParseUnary(FILE* in)
{
	switch(CurTok)
	{
		case '-': case '!':
		{
			int op = CurTok;
			
			GetNextToken(in);
			
			Expr* exp = CreateExpr(EXP_UNARY);
			exp->unaryx.op = op;
			exp->unaryx.expr = ParseExpr(in);
			
			return exp;
		} break;
		
		default:
			return ParsePost(in, ParseFactor(in));
	}
}

int GetTokenPrec()
{
	int prec = -1;
	switch(CurTok)
	{
		case '*': case '/': case '%': case '&': case '|': prec = 6; break;
		
		case '+': case '-': prec = 5; break;
	
		case '<': case '>': case TOK_LTE: case TOK_GTE:	prec = 4; break;
	
		case TOK_EQUALS: case TOK_NOTEQUAL: prec = 3; break;
		
		case TOK_AND: case TOK_OR: prec = 2; break;
			
		case TOK_CADD: case TOK_CSUB: case TOK_CMUL: case TOK_CDIV: case '=': prec = 1; break;
	}
	
	return prec;
}

Expr* ParseBinRhs(FILE* in, int exprPrec, Expr* lhs)
{
	while(1)
	{
		int prec = GetTokenPrec();
		
		if(prec < exprPrec)
			return lhs;

		int binOp = CurTok;

		GetNextToken(in);

		Expr* rhs = ParseUnary(in);
		int nextPrec = GetTokenPrec();
		
		if(prec < nextPrec)
			rhs = ParseBinRhs(in, prec + 1, rhs);

		Expr* newLhs = CreateExpr(EXP_BIN);
		
		newLhs->binx.lhs = lhs;
		newLhs->binx.rhs = rhs;
		newLhs->binx.op = binOp;
		
		lhs = newLhs;
	}
} 

Expr* ParseExpr(FILE* in)
{
	Expr* unary = ParseUnary(in);
	return ParseBinRhs(in, 0, unary);
}

void DebugExprList(Expr* head);
void DebugExpr(Expr* exp)
{
	switch(exp->type)
	{
		case EXP_NUMBER:
		{
			printf("%g", exp->constDecl->number);
		} break;
		
		case EXP_STRING:
		{
			printf("'%s'", exp->constDecl->string);
		} break;
		
		case EXP_IDENT:
		{
			printf("%s", exp->varx.varDecl->name);
		} break;
		
		case EXP_CALL:
		{
			DebugExpr(exp->callx.func);
			printf("(");
			for(int i = 0; i < exp->callx.numArgs; ++i)
			{
				DebugExpr(exp->callx.args[i]);
				if(i + 1 < exp->callx.numArgs)
					printf(",");
			}
			printf(")");
		} break;
		
		case EXP_VAR:
		{
			printf("var %s", exp->varx.varDecl->name);
		} break;
		
		case EXP_BIN:
		{
			printf("(");
			DebugExpr(exp->binx.lhs);
			switch(exp->binx.op)
			{
				case TOK_AND: printf("&&"); break;
				case TOK_OR: printf("||"); break;
				case TOK_GTE: printf(">="); break;
				case TOK_LTE: printf("<="); break;
				
				default:
					printf("%c", exp->binx.op);
			}
			DebugExpr(exp->binx.rhs);
			printf(")");
		} break;
		
		case EXP_PAREN:
		{
			printf("(");
			DebugExpr(exp->parenExpr);
			printf(")");
		} break;
		
		case EXP_WHILE:
		{
			printf("while ");
			DebugExpr(exp->whilex.cond);
			printf("\n");
			for(Expr* node = exp->whilex.bodyHead; node != NULL; node = node->next)
			{
				printf("\t");
				DebugExpr(node);
				printf("\n");
			}
			printf("end\n");
		} break;
		
		case EXP_FUNC:
		{
			printf("func %s(...)\n", exp->funcx.decl->name);
			DebugExprList(exp->funcx.bodyHead);
			printf("end\n");
		} break;
		
		case EXP_IF:
		{
			printf("if ");
			DebugExpr(exp->ifx.cond);
			printf("\n");
			
			for(Expr* node = exp->whilex.bodyHead; node != NULL; node = node->next)
			{
				printf("\t");
				DebugExpr(node);
				printf("\n");
			}
			
			printf("end\n");
		} break;
		
		case EXP_RETURN:
		{
			printf("return");
			
			if(exp->retExpr)
			{
				printf("(");
				DebugExpr(exp->retExpr);
				printf(")");
			}
			else
				printf(";");
		} break;
		
		case EXP_EXTERN:
		{
			printf("extern %s\n", exp->extDecl->name);
		} break;
	}
}

/*char IsIntrinsic(const char* name)
{
	if(strcmp(name, "len") == 0) return 1;
	if(strcmp(name, "write") == 0) return 1;
	if(strcmp(name, "read") == 0) return 1;
	if(strcmp(name, "push") == 0) return 1;
	if(strcmp(name, "pop") == 0) return 1;
	if(strcmp(name, "clear") == 0) return 1;
	if(strcmp(name, "call") == 0) return 1;
	if(strcmp(name, "array") == 0) return 1;
	if(strcmp(name, "dict") == 0) return 1;
	
	return 0;
}*/

void CompileExpr(Expr* exp);
char CompileIntrinsic(Expr* exp, const char* name)
{
	if(strcmp(name, "len") == 0)
	{
		if(exp->callx.numArgs != 1)
			ErrorExitE(exp, "Intrinsic 'len' only takes 1 argument\n");
		CompileExpr(exp->callx.args[0]);
		AppendCode(OP_LENGTH);
		return 1;
	}
	else if(strcmp(name, "write") == 0)
	{
		if(exp->callx.numArgs < 1)
			ErrorExitE(exp, "Intrinsic 'write' takes at least 1 argument\n");
		for(int i = exp->callx.numArgs - 1; i >= 0; --i)
		{
			CompileExpr(exp->callx.args[i]);
			AppendCode(OP_WRITE);
		}
		return 1;
	}
	else if(strcmp(name, "read") == 0)
	{
		if(exp->callx.numArgs != 0)
			ErrorExitE(exp, "Intrinsic 'read' takes no arguments\n");
		AppendCode(OP_READ);
		return 1;
	}
	else if(strcmp(name, "push") == 0)
	{
		if(exp->callx.numArgs != 2)
			ErrorExitE(exp, "Intrinsic 'push' only takes 2 arguments\n");
		CompileExpr(exp->callx.args[1]);
		CompileExpr(exp->callx.args[0]);
		AppendCode(OP_ARRAY_PUSH);
		return 1;
	}
	else if(strcmp(name, "pop") == 0)
	{
		if(exp->callx.numArgs != 1)
			ErrorExitE(exp, "Intrinsic 'push' only takes 1 argument\n");
		CompileExpr(exp->callx.args[0]);
		AppendCode(OP_ARRAY_POP);
		return 1;
	}
	else if(strcmp(name, "clear") == 0)
	{
		if(exp->callx.numArgs != 1)
			ErrorExitE(exp, "Intrinsic 'clear' only takes 1 argument\n");
		CompileExpr(exp->callx.args[0]);
		AppendCode(OP_ARRAY_CLEAR);
		return 1;
	}
	else if(strcmp(name, "array") == 0)
	{
		if(exp->callx.numArgs > 1)
			ErrorExitE(exp, "Intrinsic 'array' takes no more than 1 argument\n");
			
		if(exp->callx.numArgs == 1)
			CompileExpr(exp->callx.args[0]);
		else
		{
			AppendCode(OP_PUSH_NUMBER);
			AppendInt(RegisterNumber(0)->index);
		}
	
		AppendCode(OP_CREATE_ARRAY);
		return 1;
	}
	else if(strcmp(name, "dict") == 0)
	{
		if(exp->callx.numArgs != 0)
			ErrorExitE(exp, "Intrinsic 'dict' takes no arguments\n");
		AppendCode(OP_PUSH_DICT);
		return 1; 
	}
	else if(strcmp(name, "get_func_if_exists") == 0)
	{
		if(exp->callx.numArgs != 1)
			ErrorExitE(exp, "Intrinsic 'get_func_if_exists' takes 1 argument\n");
		
		FuncDecl* decl = ReferenceFunction(exp->callx.args[0]->varx.name);
		if(!decl)
			AppendCode(OP_PUSH_NULL);
		else
		{
			if(decl->hasEllipsis)
				ErrorExitE(exp->callx.args[0], "Attempted to get function pointer to variadic function '%s', which is not supported yet.\nPlease use the function directly", exp->varx.name);
			
			AppendCode(OP_PUSH_FUNC);
			AppendCode(decl->hasEllipsis);
			AppendCode(decl->isExtern);
			AppendCode(decl->numArgs);
			AppendInt(decl->index);
		}
		
		return 1;
	}
	else if(strcmp(name, "assert_func_exists") == 0)
	{
		if(exp->callx.numArgs != 2)
			ErrorExitE(exp, "Intrinsic 'assert_func_exists' only takes 2 arguments\n");
		
		if(exp->callx.args[0]->type != EXP_IDENT)
			ErrorExitE(exp->callx.args[0], "The first argument to intrinisic 'assert_func_exists' must be an identifier!\n");
			
		if(exp->callx.args[1]->type != EXP_STRING)
			ErrorExitE(exp->callx.args[1], "The second argument to intrinsic 'assert_func_exists' must be a string!\n");
		
		FuncDecl* decl = ReferenceFunction(exp->callx.args[0]->varx.name);
		if(!decl)
			ErrorExitE(exp, "%s\n", exp->callx.args[1]->constDecl->string);
		return 1;
	}
	else if(strcmp(name, "setvmdebug") == 0)
	{
		if(exp->callx.numArgs != 1)
			ErrorExitE(exp, "Intrinsic 'setvmdebug' only takes 1 argument\n");
		
		if(exp->callx.args[0]->type != EXP_NUMBER)
			ErrorExitE(exp->callx.args[0], "Argument to intrinsic 'setvmdebug' must be a number/boolean!\n");
		
		AppendCode(OP_SETVMDEBUG);
		AppendCode((Word)exp->callx.args[0]->constDecl->number);
		return 1;
	}
	else if(strcmp(name, "push_stack") == 0)
	{
		if(exp->callx.numArgs != 0)
			ErrorExitE(exp, "Intrinsic 'push_stack' takes no arguments\n");
		AppendCode(OP_PUSH_STACK);
		return 1;
	}
	else if(strcmp(name, "pop_stack") == 0)
	{
		if(exp->callx.numArgs != 0)
			ErrorExitE(exp, "Intrinsic 'push_stack' takes no arguments\n");
		AppendCode(OP_POP_STACK);
		return 1;
	}
	
	return 0;
}

void ResolveListSymbols(Expr* head);
void ResolveSymbols(Expr* exp)
{
	/*switch(exp->type)
	{
		case EXP_BIN:
		{
			ResolveSymbols(exp->binx.lhs);
			ResolveSymbols(exp->binx.rhs);
		} break;
	}*/
}

void ResolveListSymbols(Expr* head)
{
	Expr* node = head;
	while(node)
	{
		ResolveSymbols(node);
		node = node->next;
	}
}

typedef enum 
{
	PATCH_BREAK,
	PATCH_CONTINUE
} PatchType;

typedef struct _Patch
{
	struct _Patch* next;
	PatchType type;
	int loc;
} Patch;

Patch* Patches = NULL;

void AddPatch(PatchType type, int loc)
{
	Patch* p = malloc(sizeof(Patch));
	assert(p);
	
	p->type = type;
	p->loc = loc;
	
	p->next = Patches;
	Patches = p;
}

void ClearPatches()
{
	Patch* next;
	for(Patch* p = Patches; p != NULL; p = next)
	{
		next = p->next;
		free(p);
	}
	Patches = NULL;
}

void CompileExprList(Expr* head);
void CompileExpr(Expr* exp)
{
	// printf("compiling expression (%s:%i)\n", exp->file, exp->line);
	 
	ResolveSymbols(exp);
	
	switch(exp->type)
	{
		case EXP_NUMBER: case EXP_STRING:
		{
			AppendCode(exp->constDecl->type == CONST_NUM ? OP_PUSH_NUMBER : OP_PUSH_STRING);
			AppendInt(exp->constDecl->index);
		} break;
		
		case EXP_NULL:
		{
			AppendCode(OP_PUSH_NULL);
		} break;
		
		case EXP_UNARY:
		{
			switch(exp->unaryx.op)
			{
				case '-': CompileExpr(exp->unaryx.expr); AppendCode(OP_NEG); break;
				case '!': CompileExpr(exp->unaryx.expr); AppendCode(OP_LOGICAL_NOT); break;
			}
		} break;
		
		case EXP_ARRAY_LITERAL:
		{
			CompileExprList(exp->arrayx.head);
			AppendCode(OP_CREATE_ARRAY_BLOCK);
			AppendInt(exp->arrayx.length);
		} break;
		
		case EXP_IDENT:
		{	
			if(!exp->varx.varDecl)
				exp->varx.varDecl = ReferenceVariable(exp->varx.name);
			
			if(!exp->varx.varDecl)
			{
				FuncDecl* decl = ReferenceFunction(exp->varx.name);
				if(!decl)
					ErrorExitE(exp, "Attempted to reference undeclared identifier '%s'\n", exp->varx.name);
				
				if(decl->hasEllipsis)
					ErrorExitE(exp, "Attempted to get function pointer to variadic function '%s', which is not supported yet.\nPlease use the function directly", exp->varx.name);
				
				AppendCode(OP_PUSH_FUNC);
				AppendCode(decl->hasEllipsis);
				AppendCode(decl->isExtern);
				AppendCode(decl->numArgs);
				AppendInt(decl->index);
			}
			else
			{
				if(exp->varx.varDecl->isGlobal)
				{
					AppendCode(OP_GET);
					AppendInt(exp->varx.varDecl->index);
				}
				else
				{
					AppendCode(OP_GETLOCAL);
					AppendInt(exp->varx.varDecl->index);
				}
			}
		} break;
		
		case EXP_ARRAY_INDEX:
		{
			CompileExpr(exp->arrayIndex.indexExpr);
			CompileExpr(exp->arrayIndex.arrExpr);
			
			AppendCode(OP_GETINDEX);
		} break;
		
		case EXP_CALL:
		{
			if(exp->callx.func->type == EXP_IDENT)
			{
				FuncDecl* decl = ReferenceFunction(exp->callx.func->varx.name);
				if(decl)
				{	
					if(!decl->hasEllipsis)
					{
						for(int i = exp->callx.numArgs - 1; i >= 0; --i)
							CompileExpr(exp->callx.args[i]);
					}
					else
					{						
						for(int i = decl->numArgs; i < exp->callx.numArgs; ++i)
							CompileExpr(exp->callx.args[i]);
						
						AppendCode(OP_CREATE_ARRAY_BLOCK);
						AppendInt(exp->callx.numArgs - decl->numArgs);

						for(int i = decl->numArgs - 1; i >= 0; --i)
							CompileExpr(exp->callx.args[i]);
					}
					
					if(decl->isExtern)
					{
						AppendCode(OP_CALLF);
						AppendInt(decl->index);
					}
					else
					{	
						if(!decl->hasEllipsis)
						{
							if(exp->callx.numArgs != decl->numArgs)
								ErrorExitE(exp, "Attempted to pass %i arguments to function '%s' which takes %i arguments\n", exp->callx.numArgs, decl->name, decl->numArgs);
						}
						else
						{
							if(exp->callx.numArgs < decl->numArgs)
								ErrorExitE(exp, "Attempted to pass %i arguments to function '%s' which takes at least %i arguments\n", exp->callx.numArgs, decl->name, decl->numArgs);
						}
						
						AppendCode(OP_CALL);
						if(!decl->hasEllipsis) AppendCode(exp->callx.numArgs);
						else AppendCode(decl->numArgs + 1);
						AppendInt(decl->index);
					}					
				}
				else if(!CompileIntrinsic(exp, exp->callx.func->varx.name))
				{
					for(int i = exp->callx.numArgs - 1; i >= 0; --i)
						CompileExpr(exp->callx.args[i]);
						
					CompileExpr(exp->callx.func);
					
					AppendCode(OP_CALLP);
					AppendCode(exp->callx.numArgs);
				}
			}
			else if(exp->callx.func->type == EXP_COLON)
			{
				for(int i = exp->callx.numArgs - 1; i >= 0; --i)
					CompileExpr(exp->callx.args[i]);
				// first argument to function is dictionary
				CompileExpr(exp->callx.func->colonx.dict);
				
				// retrieve function from dictionary
				CompileExpr(exp->callx.func->colonx.dict);
				AppendCode(OP_DICT_GET);
				AppendInt(RegisterString(exp->callx.func->colonx.name)->index);
				
				AppendCode(OP_CALLP);
				AppendCode(exp->callx.numArgs + 1);
			}
			else
			{			
				for(int i = exp->callx.numArgs - 1; i >= 0; --i)
					CompileExpr(exp->callx.args[i]);
					
				CompileExpr(exp->callx.func);
				
				AppendCode(OP_CALLP);
				AppendCode(exp->callx.numArgs);
			}
		} break;
		
		case EXP_BIN:
		{
			if(exp->binx.op == '=')
			{	
				CompileExpr(exp->binx.rhs);
				
				if(exp->binx.lhs->type == EXP_VAR || exp->binx.lhs->type == EXP_IDENT)
				{
					if(!exp->binx.lhs->varx.varDecl)
					{
						exp->binx.lhs->varx.varDecl = ReferenceVariable(exp->binx.lhs->varx.name);
						if(!exp->binx.lhs->varx.varDecl)
							ErrorExitE(exp, "Attempted to assign to non-existent variable '%s'\n", exp->binx.lhs->varx.name);
					}
					
					if(exp->binx.lhs->varx.varDecl->isGlobal)
					{
						if(exp->binx.lhs->type == EXP_VAR) printf("Warning: assignment operations to global variables will not execute unless they are inside the entry point\n");
						AppendCode(OP_SET);
					}
					else AppendCode(OP_SETLOCAL);
				
					AppendInt(exp->binx.lhs->varx.varDecl->index);
				}
				else if(exp->binx.lhs->type == EXP_ARRAY_INDEX)
				{
					CompileExpr(exp->binx.lhs->arrayIndex.indexExpr);
					CompileExpr(exp->binx.lhs->arrayIndex.arrExpr);
					
					AppendCode(OP_SETINDEX);
				}
				else if(exp->binx.lhs->type == EXP_DOT)
				{
					if(strcmp(exp->binx.lhs->dotx.name, "pairs") == 0)
						ErrorExitE(exp, "Cannot assign dictionary entry 'pairs' to a value\n");
					
					CompileExpr(exp->binx.lhs->dotx.dict);
					AppendCode(OP_DICT_SET);
					AppendInt(RegisterString(exp->binx.lhs->dotx.name)->index);
				}
				else
					ErrorExitE(exp, "Left hand side of assignment operator '=' must be an assignable value (variable, array index, dictionary index) instead of %i\n", exp->binx.lhs->type);
			}
			else
			{
				CompileExpr(exp->binx.lhs);
				CompileExpr(exp->binx.rhs);
				
				switch(exp->binx.op)
				{
					case '+': AppendCode(OP_ADD); break;
					case '-': AppendCode(OP_SUB); break;
					case '*': AppendCode(OP_MUL); break;
					case '/': AppendCode(OP_DIV); break;
					case '%': AppendCode(OP_MOD); break;
					case '<': AppendCode(OP_LT); break;
					case '>': AppendCode(OP_GT); break;
					case '|': AppendCode(OP_OR); break;
					case '&': AppendCode(OP_AND); break;
					case TOK_EQUALS: AppendCode(OP_EQU); break;
					case TOK_LTE: AppendCode(OP_LTE); break;
					case TOK_GTE: AppendCode(OP_GTE); break;
					case TOK_NOTEQUAL: AppendCode(OP_NEQU); break;
					case TOK_AND: AppendCode(OP_LOGICAL_AND); break;
					case TOK_OR: AppendCode(OP_LOGICAL_OR); break;
					case TOK_LSHIFT: AppendCode(OP_SHL); break;
					case TOK_RSHIFT: AppendCode(OP_SHR); break;
					/*case TOK_CADD: AppendCode(OP_CADD); break;
					case TOK_CSUB: AppendCode(OP_CSUB); break;
					case TOK_CMUL: AppendCode(OP_CMUL); break;
					case TOK_CDIV: AppendCode(OP_CDIV); break;*/
					
					default:
						ErrorExitE(exp, "Unsupported binary operator %c (%i)\n", exp->binx.op, exp->binx.op);		
				}
			}
		} break;
		
		case EXP_PAREN:
		{
			CompileExpr(exp->parenExpr);
		} break;
		
		case EXP_WHILE:
		{
			int loopPc = CodeLength;
			CompileExpr(exp->whilex.cond);
			AppendCode(OP_GOTOZ);
			// emplace integer of exit location here
			int emplaceLoc = CodeLength;
			AllocatePatch(sizeof(int) / sizeof(Word));
			CompileExprList(exp->whilex.bodyHead);
			
			AppendCode(OP_GOTO);
			AppendInt(loopPc);
			
			int exitLoc = CodeLength;
			EmplaceInt(emplaceLoc, CodeLength);
			
			for(Patch* p = Patches; p != NULL; p = p->next)
			{
				if(p->type == PATCH_CONTINUE) EmplaceInt(p->loc, loopPc);
				else if(p->type == PATCH_BREAK) EmplaceInt(p->loc, exitLoc);
			}
			ClearPatches();
		} break;
		
		case EXP_FOR:
		{
			CompileExpr(exp->forx.init);
			int loopPc = CodeLength;
			
			CompileExpr(exp->forx.cond);
			AppendCode(OP_GOTOZ);
			int emplaceLoc = CodeLength;
			AllocatePatch(sizeof(int) / sizeof(Word));
			
			CompileExprList(exp->forx.bodyHead);
			
			int continueLoc = CodeLength;

			CompileExpr(exp->forx.iter);
			
			AppendCode(OP_GOTO);
			AppendInt(loopPc);
			
			int exitLoc = CodeLength;
			EmplaceInt(emplaceLoc, CodeLength);
		
			for(Patch* p = Patches; p != NULL; p = p->next)
			{
				if(p->type == PATCH_CONTINUE) EmplaceInt(p->loc, continueLoc);
				else if(p->type == PATCH_BREAK) EmplaceInt(p->loc, exitLoc);
			}
			ClearPatches();
		} break;
		
		case EXP_IF:
		{
			CompileExpr(exp->ifx.cond);
			AppendCode(OP_GOTOZ);
			int emplaceLoc = CodeLength;
			AllocatePatch(sizeof(int) / sizeof(Word));
			CompileExprList(exp->ifx.bodyHead);
			
			AppendCode(OP_GOTO);
			int exitEmplaceLoc = CodeLength;
			AllocatePatch(sizeof(int) / sizeof(Word));
			
			EmplaceInt(emplaceLoc, CodeLength);
			if(exp->ifx.alt)
			{
				if(exp->ifx.alt->type != EXP_IF)
					CompileExprList(exp->ifx.alt);
				else
					CompileExpr(exp->ifx.alt);
			}
			EmplaceInt(exitEmplaceLoc, CodeLength);
		} break;
		
		case EXP_FUNC:
		{
			AppendCode(OP_GOTO);
			int emplaceLoc = CodeLength;
			AllocatePatch(sizeof(int) / sizeof(Word));
			
			exp->funcx.decl->pc = CodeLength;
			
			if(strcmp(exp->funcx.decl->name, "_main") == 0) EntryPoint = CodeLength;
			for(int i = 0; i < exp->funcx.decl->numLocals; ++i)
				AppendCode(OP_PUSH_NULL);
			
			CompileExprList(exp->funcx.bodyHead);
			AppendCode(OP_RETURN);
			
			EmplaceInt(emplaceLoc, CodeLength);
		} break;
		
		case EXP_RETURN:
		{
			if(exp->retExpr)
			{
				CompileExpr(exp->retExpr);
				AppendCode(OP_RETURN_VALUE);
			}
			else
				AppendCode(OP_RETURN);
		} break;
		
		case EXP_DOT:
		{
			if(strcmp(exp->dotx.name, "pairs") == 0)
			{
				CompileExpr(exp->dotx.dict);
				AppendCode(OP_DICT_PAIRS);
			}
			else
			{
				CompileExpr(exp->dotx.dict);
				AppendCode(OP_DICT_GET);
				AppendInt(RegisterString(exp->dotx.name)->index);
			}
		} break;
		
		case EXP_COLON:
		{
			ErrorExitE(exp, "Attempted to use ':' to index non-function value inside dictionary; use '.' instead (or call the function you're indexing)\n");
		} break;
		
		case EXP_DICT_LITERAL:
		{
			Expr* node = exp->dictx.pairsHead;
			while(node)
			{
				if(node->type != EXP_BIN) ErrorExitE(exp, "Non-binary expression in dictionary literal (Expected something = something_else)\n");
				if(node->binx.op != '=') ErrorExitE(exp, "Invalid dictionary literal binary operator '%c'\n", node->binx.op);
				if(node->binx.lhs->type != EXP_IDENT) ErrorExitE(exp, "Invalid lhs in dictionary literal (Expected identifier)\n");
				
				if(strcmp(node->binx.lhs->varx.name, "pairs") == 0) ErrorExitE(exp, "Attempted to assign value to reserved key 'pairs' in dictionary literal\n");
				
				CompileExpr(node->binx.rhs);
				AppendCode(OP_PUSH_STRING);
				AppendInt(RegisterString(node->binx.lhs->varx.name)->index);
				
				node = node->next;
			}
			AppendCode(OP_CREATE_DICT_BLOCK);
			AppendInt(exp->dictx.length);
		} break;
		
		case EXP_CONTINUE:
		{
			AppendCode(OP_GOTO);
			AddPatch(PATCH_CONTINUE, CodeLength);
			AllocatePatch(sizeof(int) / sizeof(Word));
		} break;
		
		case EXP_BREAK:
		{
			AppendCode(OP_GOTO);
			AddPatch(PATCH_BREAK, CodeLength);
			AllocatePatch(sizeof(int) / sizeof(Word));
		} break;
		
		case EXP_LINKED_BINARY_CODE:
		{
			for(int i = 0; i < exp->code.numFunctions; ++i)
			{
				exp->code.toBeRetargeted[i]->pc += CodeLength;
				if(strcmp(exp->code.toBeRetargeted[i]->name, "_main") == 0) EntryPoint = exp->code.toBeRetargeted[i]->pc;
			}
			
			AppendCode(OP_HALT);
			for(int i = 0; i < exp->code.length; ++i)
				AppendCode(exp->code.bytes[i]);
				
			free(exp->code.toBeRetargeted);
		} break;
		
		default:
		{
			// NO CODE GENERATION NECESSARY
		} break;
	}
}

void CompileExprList(Expr* head)
{
	for(Expr* node = head; node != NULL; node = node->next)
		CompileExpr(node);
}

void DebugExprList(Expr* head)
{
	for(Expr* node = head; node != NULL; node = node->next)
		DebugExpr(node);
	printf("\n");
}


/* BINARY FORMAT:
entry point as integer

program length (in words) as integer
program code (must be no longer than program length specified previously)

number of global variables as integer
names of global variables as string lengths followed by characters

number of functions as integer
function entry points as integers
number of arguments to each function as integers
function names [string length followed by string as chars]

number of external functions referenced as integer
string length followed by string as chars (names of external functions as referenced in the code)

number of number constants as integer
number constants as doubles

number of string constants
string length followed by string as chars

whether the program has code metadata (i.e line numbers and file names for errors) (represented by char)
if so (length of each of the arrays below must be the same as program length):
line numbers mapping to pcs
file names as indices into string table (integers)
*/

char** ReadStringArrayFromBinaryFile(FILE* in, int amount)
{
	char** strings = malloc(sizeof(char*) * amount);
	assert(strings);
	
	for(int i = 0; i < amount; ++i)
	{
		int length;
		fread(&length, sizeof(int), 1, in);
		char* str = malloc(length + 1);
		fread(str, sizeof(char), length, in);
		str[length] = '\0';
		strings[i] = str;
	}
	
	return strings;
}

void FreeStringArray(char** array, int length)
{
	for(int i = 0; i < length; ++i)
		free(array[i]);
	free(array);
}

Expr* ParseBinaryFile(FILE* bin, const char* fileName)
{
	Expr* exp = CreateExpr(EXP_LINKED_BINARY_CODE);
	
	int entryPoint;
	fread(&entryPoint, sizeof(int), 1, bin);
	
	int programLength;
	fread(&programLength, sizeof(int), 1, bin);
	
	Word* program = malloc(sizeof(Word) * programLength);
	fread(program, sizeof(Word), programLength, bin);
	
	int numGlobals;
	fread(&numGlobals, sizeof(int), 1, bin);
	
	int* globalIndexTable = NULL;
	if(numGlobals > 0)
	{
		// this table is for retargeting global accesses in the binary file to the binary file produced in this execution of the compiler
		globalIndexTable = malloc(sizeof(int) * numGlobals);
		assert(globalIndexTable);
		
		char** globalNames = ReadStringArrayFromBinaryFile(bin, numGlobals);
		for(int i = 0; i < numGlobals; ++i)
		{
			VarDecl* decl = RegisterVariable(globalNames[i]);
			globalIndexTable[i] = decl->index;
		}
		FreeStringArray(globalNames, numGlobals);
	}
	
	int numFunctions;
	fread(&numFunctions, sizeof(int), 1, bin);
	
	exp->code.numFunctions = numFunctions;
	
	int* functionIndexTable = NULL;
	if(numFunctions > 0)
	{
		functionIndexTable = malloc(sizeof(int) * numFunctions);
		assert(functionIndexTable);
		
		int* functionPcs = malloc(sizeof(int) * numFunctions);
		assert(functionPcs);
		
		fread(functionPcs, sizeof(int), numFunctions, bin);
		
		int* functionNumArgs = malloc(sizeof(int) * numFunctions);
		assert(functionNumArgs);
		
		fread(functionNumArgs, sizeof(int), numFunctions, bin);
		
		char** functionNames = ReadStringArrayFromBinaryFile(bin, numFunctions);
		
		exp->code.toBeRetargeted = malloc(sizeof(FuncDecl*) * numFunctions);
		for(int i = 0; i < numFunctions; ++i)
		{
			FuncDecl* decl = DeclareFunction(functionNames[i]);
			decl->pc = functionPcs[i];
			decl->numArgs = functionNumArgs[i];
			exp->code.toBeRetargeted[i] = decl;
			functionIndexTable[i] = decl->index;
		}
		FreeStringArray(functionNames, numFunctions);
		free(functionPcs);
		free(functionNumArgs);
	}
	
	int numExterns;
	fread(&numExterns, sizeof(int), 1, bin);
	
	int* externIndexTable = NULL;
	if(numExterns > 0)
	{
		externIndexTable = malloc(sizeof(int) * numExterns);
		assert(externIndexTable);
		
		char** externNames = ReadStringArrayFromBinaryFile(bin, numExterns);
		for(int i = 0; i < numExterns; ++i)
			externIndexTable[i] = RegisterExtern(externNames[i])->index;
		FreeStringArray(externNames, numExterns);
	}
	
	
	int numNumberConstants;
	fread(&numNumberConstants, sizeof(int), 1, bin);
	
	int* numberIndexTable = NULL;
	
	if(numNumberConstants > 0)
	{
		numberIndexTable = malloc(sizeof(int) * numNumberConstants);
		assert(numberIndexTable);
		
		double* numberConstants = malloc(sizeof(double) * numNumberConstants);
		assert(numberConstants);
		
		fread(numberConstants, sizeof(double), numNumberConstants, bin);
		
		for(int i = 0; i < numNumberConstants; ++i)
			numberIndexTable[i] = RegisterNumber(numberConstants[i])->index;
		
		free(numberConstants);
	}
	
	int numStringConstants;
	int* stringIndexTable = NULL;
	
	char** stringConstants = NULL;
	if(numStringConstants > 0)
	{
		stringIndexTable = malloc(sizeof(int) * numStringConstants);
		assert(stringIndexTable);
		
		stringConstants = ReadStringArrayFromBinaryFile(bin, numStringConstants);
		for(int i = 0; i < numStringConstants; ++i)
			stringIndexTable[i] = RegisterString(stringConstants[i])->index;
		FreeStringArray(stringConstants);
	}
	
	for(int i = 0; i < programLength; ++i)
	{
		switch(program[i])
		{
			case OP_PUSH_NULL: break;
			case OP_PUSH_NUMBER: i += sizeof(int) / sizeof(Word); break;
			case OP_PUSH_STRING: i += sizeof(int) / sizeof(Word); break;
			case OP_CREATE_ARRAY: break;
			case OP_CREATE_ARRAY_BLOCK: i += sizeof(int) / sizeof(Word); break;
			case OP_PUSH_FUNC: i += 3 + sizeof(int) / sizeof(Word); break;
			case OP_PUSH_DICT: break;
			case OP_CREATE_DICT_BLOCK: i+= sizeof(int) / sizeof(Word); break;
			
			// stores current stack size
			case OP_PUSH_STACK: break;
			// returns to previously stored stack size
			case OP_POP_STACK: break;
			
			case OP_LENGTH: break;
			case OP_ARRAY_PUSH: break;
			case OP_ARRAY_POP: break;
			case OP_ARRAY_CLEAR: break;
			
			// fast dictionary operations
			case OP_DICT_SET: i += sizeof(int) / sizeof(Word); break;
			case OP_DICT_GET: i += sizeof(int) / sizeof(Word); break;
			case OP_DICT_PAIRS: break;
			
			case OP_ADD:
			case OP_SUB:
			case OP_MUL:
			case OP_DIV:
			case OP_MOD:
			case OP_OR:
			case OP_AND:
			case OP_LT:
			case OP_LTE:
			case OP_GT:
			case OP_GTE:
			case OP_EQU:
			case OP_NEQU:
			case OP_NEG:
			case OP_LOGICAL_NOT:
			case OP_LOGICAL_AND:
			case OP_LOGICAL_OR:
			case OP_SHL:
			case OP_SHR: break;
			 
			case OP_SETINDEX: break;
			case OP_GETINDEX: break;
			 
			case OP_SET:
			case OP_GET:
			{
				++i;
				int* indexLocation = (int*)(&program[i]);
				int previousIndex = *indexLocation;
				*indexLocation = globalIndexTable[previousIndex];
				i += sizeof(int) / sizeof(Word); break;
			} break; 
			
			case OP_WRITE: break;
			case OP_READ: break;
			 
			case OP_GOTO: i += sizeof(int) / sizeof(Word); break;
			case OP_GOTOZ: i += sizeof(int) / sizeof(Word); break;
			 
			case OP_CALL:
			{
				i += 2;
				int* indexLocation = (int*)(&program[i]);
				int previousIndex = *indexLocation;
				*indexLocation = functionIndexTable[previousIndex];
			} break;
			case OP_CALLP: i += 1; break;
			case OP_RETURN: break;
			case OP_RETURN_VALUE: break;
			 
			case OP_CALLF:
			{
				i += 1;
				int* indexLocation = (int*)(&program[i]);
				int previousIndex = *indexLocation;
				*indexLocation = externIndexTable[previousIndex];
			} break;
			 
			case OP_GETLOCAL:
			case OP_SETLOCAL: i += sizeof(int) / sizeof(Word); break;
			 
			case OP_HALT: break;
			
			case OP_SETVMDEBUG: i += 1; break;
		}
	}
	
	if(globalIndexTable) free(globalIndexTable);
	if(functionIndexTable) free(functionIndexTable);
	if(externIndexTable) free(externIndexTable);
	
	
	exp->code.bytes = program;
	exp->code.length = programLength;
	exp->line = -1;
	exp->file = fileName;
}

const char* StandardSourceSearchPath = "C:\\Mint\\src\\";
const char* StandardLibSearchPath = "C:\\Mint\\lib\\";

int main(int argc, char* argv[])
{
	if(argc == 1)
		ErrorExit("no input files detected!\n");
	
	const char* outPath = NULL;

	Expr* exprHead = NULL;
	Expr* exprCurrent = NULL;

	for(int i = 1; i < argc; ++i)
	{
		if(strcmp(argv[i], "-o") == 0)
		{
			outPath = argv[++i];
		}
		else if(strcmp(argv[i], "-l") == 0)
		{		
			FILE* in = fopen(argv[++i], "rb");
			if(!in)
			{
				char buf[256];
				strcpy(buf, StandardLibSearchPath);
				strcat(buf, argv[i]);
				in = fopen(buf, "rb");
				if(!in)
					ErrorExit("Cannot open file '%s' for reading\n", argv[i]);
			}
			
			if(!exprHead)
			{
				exprHead = ParseBinaryFile(in, argv[i]);
				exprCurrent = exprHead;
			}
			else
			{
				exprCurrent->next = ParseBinaryFile(in, argv[i]);
				exprCurrent = exprCurrent->next;
			}
			
			fclose(in);
		}
		else
		{
			FILE* in = fopen(argv[i], "r");
			if(!in)
			{
				char buf[256];
				strcpy(buf, StandardSourceSearchPath);
				strcat(buf, argv[i]);
				in = fopen(buf, "r");
				
				if(!in)
					ErrorExit("Cannot open file '%s' for reading\n", argv[i]);
			}
			LineNumber = 1;
			FileName = argv[i];
			
			GetNextToken(in);
			
			while(CurTok != TOK_EOF)
			{
				if(!exprHead)
				{
					exprHead = ParseExpr(in);
					exprCurrent = exprHead;
				}
				else
				{
					exprCurrent->next = ParseExpr(in);
					exprCurrent = exprCurrent->next;
				}
			}
			
			ResetLex = 1;
			fclose(in);
		}
	}					
	
	CompileExprList(exprHead);
	_AppendCode(OP_HALT, LineNumber, RegisterString(FileName)->index);
	
	FILE* out;
	
	if(!outPath)
		out = fopen("out.mb", "wb");
	else
		out = fopen(outPath, "wb");
	
	OutputCode(out);
	
	fclose(out);
	
	return 0;
}
