#include "lang.h"

const char* FileName = "unknown";
int LineNumber = 0;

size_t LexemeCapacity = 0;
size_t LexemeLength = 0;
char* Lexeme = NULL;
int CurTok = 0;
char ResetLex = 0;

void AppendLexChar(int c)
{
	if(!LexemeCapacity)
	{
		LexemeCapacity = 8;
		Lexeme = malloc(LexemeCapacity);
		assert(Lexeme);
	}
	
	if(LexemeLength + 1 >= LexemeCapacity)
	{
		LexemeCapacity *= 2;
		char* newLex = realloc(Lexeme, LexemeCapacity);
		assert(newLex);
		Lexeme = newLex;
	}
	
	Lexeme[LexemeLength++] = c;
}

void ClearLexeme()
{
	LexemeLength = 0;
}

int GetToken(FILE* in)
{
	static int last = ' ';
	
	if(ResetLex)
	{
		last = ' ';
		ResetLex = 0;
		ClearLexeme();
	}
	
	while(isspace(last))
	{
		if(last == '\n') ++LineNumber;
		last = getc(in);
	}
	
	if(isalpha(last) || last == '_')
	{
		ClearLexeme();
		while(isalnum(last) || last == '_')
		{
			AppendLexChar(last);
			last = getc(in);
		}
		AppendLexChar('\0');
		
		if (strcmp(Lexeme, "var") == 0) return TOK_VAR;
		if (strcmp(Lexeme, "while") == 0) return TOK_WHILE;
		if (strcmp(Lexeme, "end") == 0) return TOK_END;
		if (strcmp(Lexeme, "func") == 0) return TOK_FUNC;
		if (strcmp(Lexeme, "if") == 0) return TOK_IF;
		if (strcmp(Lexeme, "return") == 0) return TOK_RETURN;
		if (strcmp(Lexeme, "extern") == 0) return TOK_EXTERN;
		if (strcmp(Lexeme, "true") == 0) return TOK_TRUE;
		if (strcmp(Lexeme, "false") == 0) return TOK_FALSE;
		if (strcmp(Lexeme, "for") == 0) return TOK_FOR;
		if (strcmp(Lexeme, "else") == 0) return TOK_ELSE;
		if (strcmp(Lexeme, "elif") == 0) return TOK_ELIF;
		if (strcmp(Lexeme, "null") == 0) return TOK_NULL;
		if (strcmp(Lexeme, "inline") == 0) return TOK_INLINE;
		if (strcmp(Lexeme, "lam") == 0) return TOK_LAMBDA;
		if (strcmp(Lexeme, "continue") == 0) return TOK_CONTINUE;
		if (strcmp(Lexeme, "break") == 0) return TOK_BREAK;
		if (strcmp(Lexeme, "as") == 0) return TOK_AS;
		if (strcmp(Lexeme, "do") == 0) return TOK_DO;
		if (strcmp(Lexeme, "then") == 0) return TOK_THEN;
		if (strcmp(Lexeme, "struct") == 0) return TOK_TYPE;
		if (strcmp(Lexeme, "as") == 0) return TOK_HAS;
		if (strcmp(Lexeme, "macro") == 0) return TOK_MACRO;
		if (strcmp(Lexeme, "operator") == 0) return TOK_OPERATOR;
		if (strcmp(Lexeme, "this_function") == 0)
		{
			if(CurFunc && CurFunc->what != DECL_LAMBDA)
			{
				if(LexemeCapacity < MAX_ID_NAME_LENGTH)
				{
					LexemeCapacity = MAX_ID_NAME_LENGTH;
					void* newLexeme = realloc(Lexeme, LexemeCapacity);
					assert(newLexeme);
					Lexeme = newLexeme;
				}
				
				strcpy(Lexeme, CurFunc->name);
			}
		}

		return TOK_IDENT;
	}
		
	if(isdigit(last))
	{	
		ClearLexeme();
		
		if(last == '0')
		{
			last = getc(in);
			if(last == 'x')
			{
				last = getc(in);
				while(isxdigit(last))
				{
					AppendLexChar(last);
					last = getc(in);
				}
				AppendLexChar('\0');
				
				return TOK_HEXNUM;
			}
			else
			{	
				ungetc(last, in);
				ungetc('0', in);
				last = '0';
			}
		}
		
		while(isdigit(last) || last == '.')
		{
			AppendLexChar(last);
			last = getc(in);
		}
		AppendLexChar('\0');
		
		return TOK_NUMBER;
	}
	
	if(last == '"')
	{
		ClearLexeme();
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
			
			AppendLexChar(last);
			last = getc(in);
		}
		AppendLexChar('\0');
		last = getc(in);
		
		return TOK_STRING;
	}
	
	if(last == '\'')
	{
		last = getc(in);
		ClearLexeme();
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
		
		// assure minimum capacity to store integer
		if(LexemeCapacity < 32)
		{
			LexemeCapacity = 32;
			char* newLex = realloc(Lexeme, LexemeCapacity);
			assert(newLex);
			Lexeme = newLex;
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
		if(last != '.')
			return TOK_CAT;
		last = getc(in);
		return TOK_ELLIPSIS;
	}
	
	return lastChar;
}
