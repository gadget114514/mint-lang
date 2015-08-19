#include "lang.h"

const char* HintStrings[] = {
	"number",
	"string",
	"array",
	"dict",
	"function",
	"native",
	"void",
	"dynamic",
	"usertype"
};

TypeHint* UserTypes = NULL;

TypeHint* CreateTypeHint(Hint hint)
{
	TypeHint* type = calloc(1, sizeof(TypeHint));
	assert(type);
	type->hint = hint;

	return type;
}

TypeHint* GetBroadTypeHint(Hint type)
{
	switch(type)
	{
		case NUMBER: return CreateTypeHint(NUMBER);
		
		case STRING: 
		{
			TypeHint* type = CreateTypeHint(STRING);
			type->subType = GetBroadTypeHint(NUMBER);
			return type;
		} break;
		
		case VOID: return CreateTypeHint(VOID);
		
		case DYNAMIC: return CreateTypeHint(DYNAMIC);
		
		case FUNC:
		{ 
			TypeHint* type = CreateTypeHint(FUNC);
			type->func.numArgs = -1;
			return type;
		} break;
		case DICT:
		{
			TypeHint* type = CreateTypeHint(DICT);
			type->subType = GetBroadTypeHint(DYNAMIC);
			return type;
		} break;
		
		case ARRAY: return CreateTypeHint(ARRAY);
		
		case NATIVE: return CreateTypeHint(NATIVE);
	
		default:
			ErrorExit("(INTERNAL) Invalid argument to GetBroadTypeHint\n");
			break;
	}
	
	return NULL;
}

// TODO: this is weird for function types
const char* HintString(const TypeHint* type)
{
	// TODO: maybe this should take a string buffer
	if(type) 
	{
		// NOTE: this would recurse until the char buffer overflows if the dynamic type was handled normally
		if(type->hint == DYNAMIC)
			return HintStrings[DYNAMIC];
		else
		{
			if(type->hint != USERTYPE)
			{
				if(type->hint != FUNC)
				{
					if(!type->subType)
						return HintStrings[type->hint];
					else
					{
						char buf[256];
						sprintf(buf, "%s-%s", HintStrings[type->hint], HintString(type->subType)); 
						
						char* str = malloc(strlen(buf) + 1);
						assert(str);
						strcpy(str, buf);
						return str;
					}
				}
				else
				{
					char buf[512];
					char* pbuf = buf;

					if(type->func.numArgs >= 0)
					{
						// TODO: check for buffer overflows pls
						pbuf += sprintf(pbuf, "function(");
						for(int i = 0; i < type->func.numArgs; ++i)
						{
							if(i + 1 < type->func.numArgs)
								pbuf += sprintf(pbuf, "%s,", HintString(type->func.args[i]));
							else
								pbuf += sprintf(pbuf, "%s", HintString(type->func.args[i]));
						}

						if(type->func.ret)
							pbuf += sprintf(pbuf, ")-%s", HintString(type->func.ret));
						else
							pbuf += sprintf(pbuf, ")");
					}
					else
						strcpy(buf, "function");
					char* str = malloc(strlen(buf) + 1);
					assert(str);
					strcpy(str, buf);
					return str;
				}
			}
			else
				return type->user.name;
		}
	}
	return "unknown";
}

char CompareTypes(const TypeHint* a, const TypeHint* b)
{
	if(!a || !b) return 1;
	
	if(a->hint == VOID || b->hint == VOID) return a->hint == b->hint;
	
	if(a->hint == DYNAMIC || b->hint == DYNAMIC) return 1;
	if(a->hint == USERTYPE || b->hint == USERTYPE)
	{
		if(a->hint == b->hint) 
			return strcmp(a->user.name, b->user.name) == 0;
		return 0;
	}
	if(a->hint == FUNC || b->hint == FUNC)
	{
		if(a->hint == b->hint)
		{
			if(a->func.numArgs < 0 || b->func.numArgs < 0)
				return CompareTypes(a->func.ret, b->func.ret);

			if(a->func.numArgs == b->func.numArgs)
			{
				for(int i = 0; i < a->func.numArgs; ++i)
				{
					if(!CompareTypes(a->func.args[i], b->func.args[i]))
						return 0;
				}

				return CompareTypes(a->func.ret, b->func.ret);
			}
			return 0;
		}
		return 0;
	}

	return (a->hint == b->hint) && CompareTypes(a->subType, b->subType);
}

TypeHint* GetTypeHintFromString(const char* n)
{
	if(strcmp(n, "number") == 0) return GetBroadTypeHint(NUMBER);
	else if(strcmp(n, "string") == 0) return GetBroadTypeHint(STRING);
	else if(strcmp(n, "array") == 0) return GetBroadTypeHint(ARRAY);
	else if(strcmp(n, "dict") == 0) return GetBroadTypeHint(DICT);
	else if(strcmp(n, "function") == 0) return GetBroadTypeHint(FUNC);
	else if(strcmp(n, "native") == 0) return GetBroadTypeHint(NATIVE);
	else if(strcmp(n, "dynamic") == 0) return GetBroadTypeHint(DYNAMIC);
	else if(strcmp(n, "void") == 0) return GetBroadTypeHint(VOID);
	else ErrorExit("Invalid type hint '%s'\n", Lexeme);
	
	return NULL;
}

TypeHint* RegisterUserType(const char* name)
{
	for(TypeHint* type = UserTypes; type; type = type->next)
	{
		if(strcmp(type->user.name, name) == 0)
			return type;
	}

	TypeHint* type = CreateTypeHint(USERTYPE);
	strcpy(type->user.name, name);

	type->user.numElements = 0;

	type->next = UserTypes;
	UserTypes = type;

	return type;
}

TypeHint* GetUserTypeElement(const TypeHint* type, const char* name)
{
	assert(type);
	assert(type->hint == USERTYPE);

	for(int i = 0; i < type->user.numElements; ++i)
	{
		if(strcmp(type->user.names[i], name) == 0)
			return type->user.elements[i];
	}

	return NULL;
}

int GetUserTypeElementIndex(const TypeHint* type, const char* name)
{
	assert(type);
	assert(type->hint == USERTYPE);

	for(int i = 0; i < type->user.numElements; ++i)
	{
		if(strcmp(type->user.names[i], name) == 0)
			return i;
	}

	return -1;
}

int GetUserTypeNumElements(const TypeHint* type)
{
	if(!type) return 0;

	assert(type->hint == USERTYPE);
	return type->user.numElements;
}

TypeHint* ParseTypeHint(FILE* in)
{
	// multi-type hint is bad
	/*if(CurTok == '(')
	{
		GetNextToken(in);
		
		TypeHint* types = NULL;
		TypeHint* currentType = NULL;
		
		while(CurTok != ')')
		{
			if(CurTok != TOK_IDENT)
				ErrorExit("Expected identifier in type declaration\n");
			
			TypeHint* type = CreateTypeHint(GetTypeHintFromString(Lexeme)->hint);
			
			if(!types)
			{
				types = type;
				currentType = type;
			}
			else
			{
				currentType->next = type;
				currentType = type;
			}
			
			GetNextToken(in);
			
			if(CurTok == ',') GetNextToken(in);
			else if(CurTok != ')') ErrorExit("Expected ')' or ',' in type declaration list\n");
		}
		
		GetNextToken(in);
		return types;
	}
	else 
	*/
	if(CurTok == TOK_IDENT)
	{
		TypeHint* userType = NULL;
		TypeHint* node = UserTypes;
		while(node)
		{
			if(strcmp(node->user.name, Lexeme) == 0)
			{
				userType = node;
				break;
			}
			node = node->next;
		}

		if(!userType)
		{
			TypeHint* type = GetTypeHintFromString(Lexeme);
			GetNextToken(in);

			if(CurTok == '(')
			{
				if(type->hint == FUNC)
				{
					// NOTE: set to 0 because it's -1 (indeterminate amount of arguments) by default
					type->func.numArgs = 0;

					GetNextToken(in);

					while(CurTok != ')')
					{
						TypeHint* arg = ParseTypeHint(in);
						
						if(type->func.numArgs >= MAX_ARGS)
							ErrorExit("Exceeded maximum number of arguments %d in type declaration\n", MAX_ARGS);

						type->func.args[type->func.numArgs++] = arg;

						if(CurTok == ',') GetNextToken(in);
						else if(CurTok != ')') ErrorExit("Expected ')' or ',' in type declaration list\n");
					}
					
					GetNextToken(in);
				}
				else
					ErrorExit("Attempted to declare argument types for non-function type\n");
			}
			
			if(CurTok == '-')
			{
				GetNextToken(in);
				TypeHint* subType = ParseTypeHint(in);
				type->subType = subType;
			}
			return type;
		}
		GetNextToken(in);
		return userType;
	}
	else
		ErrorExit("Expected type\n");
	
	return NULL;
}

TypeHint* InferTypeFromExpr(Expr* exp)
{
	switch(exp->type)
	{
		// null initialized variables should be inferred upon later access
		case EXP_NULL: return NULL; break;
		
		case EXP_NUMBER: return GetBroadTypeHint(NUMBER); break;
		
		case EXP_STRING: return GetBroadTypeHint(STRING); break;
		
		case EXP_TYPE_CAST: return exp->castx.newType; break;

		case EXP_INST: return exp->instType; break;

		case EXP_IDENT:
		{
			if(!exp->varx.varDecl)
				exp->varx.varDecl = ReferenceVariable(exp->varx.name);
			
			if(exp->varx.varDecl)
				return exp->varx.varDecl->type;
			else
			{
				FuncDecl* decl = ReferenceFunction(exp->varx.name);
				
				if(decl)
				{
					if(!decl->type)
						ResolveTypesExprList(decl->bodyHead);
					return decl->type;
				}
				else
					return NULL;
			}
		} break;
		
		case EXP_CALL:
		{
			/*if(exp->callx.func->type == EXP_IDENT)
			{
				FuncDecl* decl = ReferenceFunction(exp->callx.func->varx.name);
				if(decl)
					return decl->returnType;
				else
				{
					VarDecl* decl = exp->callx.func->varx.varDecl ? exp->callx.func->varx.varDecl : ReferenceVariable(exp->callx.func->varx.name);
					if(decl && decl->type)
					{
						if(decl->type->hint == FUNC)
							return decl->type->subType;
					}
				}
			}
			
			return GetBroadTypeHint(DYNAMIC);*/
			const TypeHint* inf = InferTypeFromExpr(exp->callx.func);
			if(inf && inf->hint == FUNC)
				return inf->func.ret;
			else
				return GetBroadTypeHint(DYNAMIC);
		} break;
		
		case EXP_VAR:
		{
			return exp->varx.varDecl->type;
		} break;
		
		case EXP_BIN:
		{
			const TypeHint* a = InferTypeFromExpr(exp->binx.lhs);
			const TypeHint* b = InferTypeFromExpr(exp->binx.rhs);
			
			if(exp->binx.op != '=')
			{
				if(CompareTypes(a, b))
				{
					if(a && b)
					{
						if(a->hint == NUMBER && b->hint == NUMBER)
							return GetBroadTypeHint(NUMBER);
						else
							return GetBroadTypeHint(DYNAMIC);
					}
				}
				else if(a && a->hint == DICT) // possibly overloaded operation
					return GetBroadTypeHint(DYNAMIC);
			}
			else
				return GetBroadTypeHint(VOID);
		} break;
		
		case EXP_PAREN:
		{
			return InferTypeFromExpr(exp->parenExpr);
		} break;
		
		case EXP_DICT_LITERAL:
		{
			return GetBroadTypeHint(DICT);
		} break;
		
		case EXP_ARRAY_LITERAL:
		{
			if(exp->arrayx.length > 0)
			{
				// attempt to infer the subtype of the array
				Expr* node = exp->arrayx.head;
				TypeHint* inf = InferTypeFromExpr(node);
				node = node->next;
				
				while(node)
				{
					// if the array literal contains values which evaluate to more than one type, then it is a dynamic array
					const TypeHint* curInf = InferTypeFromExpr(node);
					if(!CompareTypes(curInf, inf)) 
					{
						TypeHint* type = CreateTypeHint(ARRAY);
						type->subType = GetBroadTypeHint(DYNAMIC);
						return type;
					}
					node = node->next;
				}
				
				TypeHint* type = CreateTypeHint(ARRAY);
				type->subType = inf;
				
				return type;
			}
			
			return GetBroadTypeHint(ARRAY);
		} break;
		
		case EXP_ARRAY_INDEX:
		{
			const TypeHint* inf = InferTypeFromExpr(exp->arrayIndex.arrExpr);
			if(inf && inf->subType)
				return inf->subType;
			return GetBroadTypeHint(DYNAMIC);
			
			/*if(exp->arrayIndex.arrExpr->type == EXP_IDENT)
			{
				Expr* id = exp->arrayIndex.arrExpr;
				if(id->varx.varDecl)
				{
					if(id->varx.varDecl->type)
					{
						if(id->varx.varDecl->type->subType)
							return id->varx.varDecl->type->subType;
					}
				}
			}
			
			return GetBroadTypeHint(DYNAMIC);*/
		} break;
		
		case EXP_DOT:
		{
			const TypeHint* type = InferTypeFromExpr(exp->dotx.dict);
			if(type ? type->hint == USERTYPE : 0)
				return GetUserTypeElement(type, exp->dotx.name);
			return GetBroadTypeHint(DYNAMIC);
		} break;

		case EXP_COLON:
		{
			const TypeHint* type = InferTypeFromExpr(exp->colonx.dict);
			if(type ? type->hint == USERTYPE : 0)
				return GetUserTypeElement(type, exp->colonx.name);
			return GetBroadTypeHint(DYNAMIC);
		} break;
		
		case EXP_FUNC:
		{
			return exp->funcx.decl->type;
		} break;

		case EXP_LAMBDA:
		{	
			return exp->lamx.decl->type;
		} break;

		case EXP_IF:
		{
			Expr* node = exp->ifx.bodyHead;
			TypeHint* inf = NULL;
			while(node)
			{
				if(!node->next)
					inf = InferTypeFromExpr(node);
				node = node->next;
			}

			if(exp->ifx.alt->type == EXP_IF)
			{
				TypeHint* newInf = InferTypeFromExpr(exp->ifx.alt);
				if(!CompareTypes(newInf, inf)) return GetBroadTypeHint(DYNAMIC);
			}
			else
			{
				node = exp->ifx.alt;
				TypeHint* newInf = NULL;
				
				while(node)
				{
					if(!node->next)
						newInf = InferTypeFromExpr(node);
					node = node->next;
				}

				if(!CompareTypes(newInf, inf)) return GetBroadTypeHint(DYNAMIC);
			}

			return inf;
		} break;

		case EXP_FOR:
		{
			Expr* node = exp->forx.bodyHead;
			TypeHint* inf = NULL;
			while(node)
			{
				if(!node->next)
					inf = InferTypeFromExpr(node);
				node = node->next;
			}
			TypeHint* super = GetBroadTypeHint(ARRAY);
			super->subType = inf;
			return super;
		} break;
		
		default:
			break;
	}
	
	return NULL;
}

// attempts to narrow down the types of variables by analyzing their usage in binary expression (and detects false assignments, etc)
void ResolveTypeFromAssignment(Expr* exp)
{
	if(exp->binx.lhs->type == EXP_VAR)
	{
		if(!exp->binx.lhs->varx.varDecl->type)
			exp->binx.lhs->varx.varDecl->type = InferTypeFromExpr(exp->binx.rhs);
		else 
		{
			TypeHint* inf = InferTypeFromExpr(exp->binx.rhs);
			
			//Warn("var types: %s, %s\n", HintString(exp->binx.lhs->varx.varDecl->type), HintString(inf));
			if(!CompareTypes(inf, exp->binx.lhs->varx.varDecl->type))
				WarnE(exp, "Initializing variable of type '%s' with value of type '%s'\n", HintString(exp->binx.lhs->varx.varDecl->type), HintString(inf));
		}
	}
	else if(exp->binx.lhs->type == EXP_IDENT)
	{
		if(exp->binx.lhs->varx.varDecl)
		{
			TypeHint* inf = InferTypeFromExpr(exp->binx.rhs);
			if(!exp->binx.lhs->varx.varDecl->type)
				exp->binx.lhs->varx.varDecl->type = inf;
			else if(!CompareTypes(inf, exp->binx.lhs->varx.varDecl->type))
				WarnE(exp, "Assigning a '%s' to a variable which was previously assigned to value of type '%s'\n", HintString(inf), HintString(exp->binx.lhs->varx.varDecl->type));
		}
	}
	else if(exp->binx.lhs->type == EXP_ARRAY_INDEX)
	{
		Expr* arr = exp->binx.lhs->arrayIndex.arrExpr;
		if(arr->type == EXP_IDENT)
		{
			TypeHint* type = InferTypeFromExpr(exp->binx.rhs);
			
			if(arr->varx.varDecl)
			{
				if(arr->varx.varDecl->type)
				{
					if(!arr->varx.varDecl->type->subType)
						arr->varx.varDecl->type->subType = type;
					else
					{
						if(!CompareTypes(arr->varx.varDecl->type->subType, type))
							WarnE(exp, "Assigning a '%s' to an index of type '%s'\n", HintString(type), HintString(arr->varx.varDecl->type->subType));
					}
				}
				else
				{
					arr->varx.varDecl->type = CreateTypeHint(ARRAY);
					arr->varx.varDecl->type->subType = type;
				}
			} 
		}
	}
}

void ResolveTypes(Expr* exp)
{
	switch(exp->type)
	{
		case EXP_BIN:
		{
			ResolveTypes(exp->binx.lhs);
			ResolveTypes(exp->binx.rhs);
			
			if(exp->binx.op == '=')
				ResolveTypeFromAssignment(exp);
			else
			{
				const TypeHint* a = InferTypeFromExpr(exp->binx.lhs);
				const TypeHint* b = InferTypeFromExpr(exp->binx.rhs);
				
				if(a && b)
				{
					if(!CompareTypes(a, b) && a->hint != DICT)
						WarnE(exp, "Invalid binary operation between '%s' and '%s'\n", HintString(a), HintString(b));
				}
			}
		} break;
		
		case EXP_PAREN:
		{
			ResolveTypes(exp->parenExpr);
		} break;
		
		case EXP_WHILE:
		{
			ResolveTypes(exp->whilex.cond);
			ResolveTypesExprList(exp->whilex.bodyHead);
		} break;
		
		case EXP_FUNC:
		{
			ResolveTypesExprList(exp->funcx.bodyHead);
		} break;
		
		case EXP_IF:
		{
			ResolveTypes(exp->ifx.cond);
			ResolveTypesExprList(exp->ifx.bodyHead);
			
			if(exp->ifx.alt)
			{
				if(exp->ifx.alt->type != EXP_IF)
					ResolveTypesExprList(exp->ifx.alt);
				else
					ResolveTypes(exp->ifx.alt);
			}
		} break;
		
		case EXP_ARRAY_LITERAL:
		{
			ResolveTypesExprList(exp->arrayx.head);
		} break;
		
		case EXP_RETURN:
		{
			FuncDecl* decl = exp->retx.decl;
			
			if(exp->retx.exp)
			{ 
				TypeHint* inf = InferTypeFromExpr(exp->retx.exp);
					
				if(decl->type->func.ret)
				{
					if(CompareTypes(decl->type->func.ret, GetBroadTypeHint(VOID)))
						WarnE(exp, "Attempting to return a value in a function which was hinted to return '%s'\n", HintString(decl->type->func.ret));
					else if(!CompareTypes(decl->type->func.ret, inf))
						WarnE(exp, "Return value type '%s' does not match with function '%s' designated return type '%s'\n", HintString(inf), decl->name, HintString(decl->type->func.ret));
				}
				else
					decl->type->func.ret = inf;
			}
		} break;
		
		case EXP_UNARY:
		{
			ResolveTypes(exp->unaryx.expr);
			const TypeHint* inf = InferTypeFromExpr(exp->unaryx.expr);
			
			if(!CompareTypes(GetBroadTypeHint(NUMBER), inf))
				WarnE(exp, "Applying unary operator to non-numerical value of type '%s'\n", HintString(inf));
		} break;
		
		case EXP_FOR:
		{
			ResolveTypes(exp->forx.init);
			ResolveTypes(exp->forx.cond);
			ResolveTypes(exp->forx.iter);
			ResolveTypesExprList(exp->forx.bodyHead);
		} break;
		
		case EXP_DOT:
		{
			TypeHint* inf = InferTypeFromExpr(exp->dotx.dict);
			/*if(exp->dotx.dict->type == EXP_IDENT)
			{
				if(!exp->dotx.dict->varx.varDecl)
					exp->dotx.dict->varx.varDecl = ReferenceVariable(exp->dotx.dict->varx.name);
				
				if(exp->dotx.dict->varx.varDecl)
				{
					if(!CompareTypes(exp->dotx.dict->varx.varDecl->type, GetBroadTypeHint(DICT)))
						WarnE(exp, "Attempted to use dot '.' operator on value of type '%s'\n", HintString(exp->dotx.dict->varx.varDecl->type));
				}
			}*/
			if(inf)
			{
				if(inf->hint != USERTYPE)
				{
					if(!CompareTypes(inf, GetBroadTypeHint(DICT)))
						WarnE(exp, "Attempted to use dot '.' operator on value of type '%s'\n", HintString(inf));
				} // NOTE: otherwise, it's the compilers problem (SHOULD IT BE THO?)
			}
			else
				ResolveTypes(exp->dotx.dict);
		} break;
		
		case EXP_COLON:
		{
			TypeHint* inf = InferTypeFromExpr(exp->dotx.dict);
			/*if(exp->colonx.dict->type == EXP_IDENT)
			{
				if(!exp->colonx.dict->varx.varDecl)
					exp->colonx.dict->varx.varDecl = ReferenceVariable(exp->colonx.dict->varx.name);
					
				if(exp->colonx.dict->varx.varDecl)
				{
					if(!CompareTypes(exp->colonx.dict->varx.varDecl->type, GetBroadTypeHint(DICT)))
						WarnE(exp, "Attempted to use dot '.' operator on value of type '%s'\n", HintString(exp->colonx.dict->varx.varDecl->type));
				}
			}*/
			if(inf)
			{
				if(inf->hint != USERTYPE)
				{
					if(!CompareTypes(inf, GetBroadTypeHint(DICT)))
						WarnE(exp, "Attempted to use dot '.' operator on value of type '%s'\n", HintString(inf));
				} // NOTE: see previous note (SHOULD IT BE THO?)
			}
			else
				ResolveTypes(exp->colonx.dict);
		} break;
		
		case EXP_CALL:
		{
			for(int i = 0; i < exp->callx.numArgs; ++i)
				ResolveTypes(exp->callx.args[i]);
				
			TypeHint* inf = InferTypeFromExpr(exp->callx.func);
			/*if(exp->callx.func->type == EXP_IDENT)
			{
				if(!exp->callx.func->varx.varDecl)
						exp->callx.func->varx.varDecl = ReferenceVariable(exp->callx.func->varx.name);
					
				if(exp->callx.func->varx.varDecl)
				{
					if(!CompareTypes(exp->callx.func->varx.varDecl->type, GetBroadTypeHint(FUNC)) &&
						!CompareTypes(exp->callx.func->varx.varDecl->type, GetBroadTypeHint(DICT)))
						WarnE(exp, "Attempted to call variable of type '%s'\n", HintString(exp->callx.func->varx.varDecl->type));
				}
			}*/
			if(inf)
			{
				if(!CompareTypes(inf, GetBroadTypeHint(FUNC)) &&
					!CompareTypes(inf, GetBroadTypeHint(DICT)))
					WarnE(exp, "Attempted to call variable of type '%s'\n", HintString(inf));

				if(inf->hint == FUNC && !inf->func.ret)
				{
					if(exp->callx.func->type == EXP_IDENT)
					{
						FuncDecl* decl = ReferenceFunction(exp->callx.func->varx.name);
						if(decl)
							ResolveTypesExprList(decl->bodyHead);
					}
				}
			}
			else
				ResolveTypes(exp->callx.func);
		} break;
		
		case EXP_ARRAY_INDEX:
		{
			TypeHint* inf = InferTypeFromExpr(exp->arrayIndex.arrExpr);
			/*if(exp->arrayIndex.arrExpr->type == EXP_IDENT)
			{
				if(!exp->arrayIndex.arrExpr->varx.varDecl)
						exp->arrayIndex.arrExpr->varx.varDecl = ReferenceVariable(exp->arrayIndex.arrExpr->varx.name);
					
				if(exp->arrayIndex.arrExpr->varx.varDecl)
				{
					if(!CompareTypes(exp->arrayIndex.arrExpr->varx.varDecl->type, GetBroadTypeHint(ARRAY)) &&
						!CompareTypes(exp->arrayIndex.arrExpr->varx.varDecl->type, GetBroadTypeHint(DICT)) &&
						!CompareTypes(exp->arrayIndex.arrExpr->varx.varDecl->type, GetBroadTypeHint(STRING)))
						WarnE(exp, "Attempted to index variable of type '%s'\n", HintString(exp->arrayIndex.arrExpr->varx.varDecl->type));
				}
				else if
			}*/
			if(inf)
			{
				if(!CompareTypes(inf, GetBroadTypeHint(ARRAY)) &&
			   	   !CompareTypes(inf, GetBroadTypeHint(DICT)) &&
			   	   !CompareTypes(inf, GetBroadTypeHint(STRING)))
					WarnE(exp, "Attempted to index variable of type '%s'\n", HintString(inf));
			}
			else
				ResolveTypes(exp->arrayIndex.arrExpr);
		} break;

		case EXP_LAMBDA:
		{
			ResolveTypesExprList(exp->lamx.bodyHead);
		} break;
		
		default:	// no type information can/needs to be resolved for these expression types
			break;
	}
}

void ResolveTypesExprList(Expr* head)
{
	Expr* node = head;
	while(node)
	{
		ResolveTypes(node);
		node = node->next;
	}
}
