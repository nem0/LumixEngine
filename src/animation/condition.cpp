#include "condition.h"
#include "controller.h"
#include "engine/crt.h"
#include "engine/math.h"
#include "nodes.h"


namespace Lumix
{


namespace Anim
{


enum class Types : u8
{
	FLOAT,
	BOOL,
	U32,

	NONE
};


namespace Instruction
{
	enum Type : u8
	{
		PUSH_BOOL,
		PUSH_FLOAT,
		PUSH_U32,
		ADD_FLOAT,
		MUL_FLOAT,
		DIV_FLOAT,
		RET_FLOAT,
		RET_BOOL,
		SUB_FLOAT,
		UNARY_MINUS,
		CALL,
		FLOAT_LT,
		FLOAT_GT,
		INT_EQ,
		INT_NEQ,
		AND,
		OR,
		NOT,
		INPUT_FLOAT,
		INPUT_U32,
		INPUT_BOOL
	};
}


static const struct
{
	const char* name;
	Types ret_type;
	Types args[9];

	int arity() const
	{
		for (int i = 0; i < sizeof(args) / sizeof(args[0]); ++i)
		{
			if (args[i] == Types::NONE) return i;
		}
		return 0;
	}

	bool checkArgTypes(const Types* stack, int idx) const
	{
		for (int i = 0; i < arity(); ++i)
		{
			if (args[i] != stack[idx - i - 1]) return false;
		}
		return true;
	}

} FUNCTIONS[] = {
	{"sin", Types::FLOAT, {Types::FLOAT, Types::NONE}},
	{"cos", Types::FLOAT, {Types::FLOAT, Types::NONE}},
	{"eq", Types::BOOL, {Types::FLOAT, Types::FLOAT, Types::FLOAT, Types::NONE}},
	{"time", Types::FLOAT, {Types::NONE}},
	{"length", Types::FLOAT, {Types::NONE}},
	{"finishing", Types::BOOL, {Types::NONE}}};


struct ExpressionCompiler
{
public:
	struct Token
	{
		enum Type
		{
			EMPTY,
			NUMBER,
			OPERATOR,
			IDENTIFIER,
			LEFT_PARENTHESIS,
			RIGHT_PARENTHESIS,
			COMMA,
		};
		Type type;

		enum Operator
		{
			ADD,
			MULTIPLY,
			DIVIDE,
			SUBTRACT,
			UNARY_MINUS,
			LESS_THAN,
			GREATER_THAN,
			AND,
			OR,
			NOT,
			NOT_EQUAL,
			EQUAL
		};

		int offset;
		int size;

		float number;
		Operator oper;
	};

	public:
		int tokenize(const char* src, const Span<Token>& tokens);
		int compile(const char* src, const Token* tokens, int token_count, u8* byte_code, int max_size, InputDecl& decl);
		int toPostfix(const char* src, const Token* input, Token* output, int count);
		Condition::Error getError() const { return m_compile_time_error; }


private:
	static int getPriority(const Token& token);


	static bool isIdentifierChar(char c)
	{
		return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
	}


	static u16 getFunctionIdx(const char* src, const ExpressionCompiler::Token& token)
	{
		if (token.size == 0) return 0xffFF;
		int i = 0;
		for(auto& fn : FUNCTIONS)
		{
			if(strncmp(src + token.offset, fn.name, token.size) == 0) return i;
			++i;
		}
		return 0xffFF;
	}


	static bool getFloatConstValue(const char* src, const ExpressionCompiler::Token& token, float& value)
	{
		static const struct { const char* name; float value; } CONSTS[] =
		{
			{ "PI", 3.14159265358979323846f }
		};
		for (const auto& i : CONSTS)
		{
			if (stringLength(i.name) == token.size && equalStrings(i.name, src + token.offset))
			{
				value = i.value;
				return true;
			}
		}
		return false;
	}


	static bool getBoolConstValue(const char* src, const ExpressionCompiler::Token& token, bool& value)
	{
		if (token.size == 5 && strncmp("false", src + token.offset, token.size) == 0)
		{
			value = false;
			return true;
		}
		if (token.size == 4 && strncmp("true", src + token.offset, token.size) == 0)
		{
			value = true;
			return true;
		}
		return false;
	}


private:
	Condition::Error m_compile_time_error;
	int m_compile_time_offset;
};


struct ExpressionVM
{
public:
	static const int STACK_SIZE = 50;

	struct ReturnValue
	{
		ReturnValue()
		{
			type = Types::NONE;
		}

		explicit ReturnValue(float f)
		{
			f_value = f;
			type = Types::FLOAT;
		}

		explicit ReturnValue(bool b)
		{
			b_value = b;
			type = Types::BOOL;
		}

		Types type;
		union
		{
			float f_value;
			bool b_value;
		};
	};

public:
	ReturnValue evaluate(const u8* code, const RuntimeContext& rc);

private:
	void callFunction(u16 idx, const RuntimeContext& rc);

	template<typename T>
	T& pop()
	{
		m_stack_pointer -= sizeof(T);
		return *(T*)(m_stack + m_stack_pointer);
	}


	template <typename T>
	const u8* pushStackConst(const u8* cp)
	{
		*(T*)(m_stack + m_stack_pointer) = *(T*)cp;
		m_stack_pointer += sizeof(T);
		return cp + sizeof(T);
	}


	template <typename T>
	void push(T value)
	{
		*(T*)(m_stack + m_stack_pointer) = value;
		m_stack_pointer += sizeof(T);
	}

private:
	u8 m_stack[STACK_SIZE];
	int m_stack_pointer;
};


ExpressionVM::ReturnValue ExpressionVM::evaluate(const u8* code, const RuntimeContext& rc)
{
	m_stack_pointer = 0;
	const u8* cp = code;
	for (;;)
	{
		u8 type = *cp;
		++cp;
		switch (type)
		{
			case Instruction::CALL:
				callFunction(*(u16*)cp, rc);
				cp += sizeof(u16);
				break;
			case Instruction::INPUT_FLOAT:
				push<float>(*(float*)(rc.inputs.begin() + *(int*)cp));
				cp += sizeof(int);
				break;
			case Instruction::INPUT_BOOL:
				push<bool>(*(bool*)(rc.inputs.begin() + *(int*)cp));
				cp += sizeof(int);
				break;
			case Instruction::INPUT_U32:
				push<u32>(*(u32*)(rc.inputs.begin() + *(int*)cp));
				cp += sizeof(u32);
				break;
			case Instruction::RET_FLOAT: return ExpressionVM::ReturnValue(pop<float>());
			case Instruction::RET_BOOL: return ExpressionVM::ReturnValue(pop<bool>());
			case Instruction::ADD_FLOAT: push<float>(pop<float>() + pop<float>()); break;
			case Instruction::SUB_FLOAT: push<float>(-pop<float>() + pop<float>()); break;
			case Instruction::PUSH_BOOL: cp = pushStackConst<bool>(cp); break;
			case Instruction::PUSH_FLOAT: cp = pushStackConst<float>(cp); break;
			case Instruction::PUSH_U32: cp = pushStackConst<u32>(cp); break;
			case Instruction::FLOAT_LT: push<bool>(pop<float>() > pop<float>()); break;
			case Instruction::FLOAT_GT: push<bool>(pop<float>() < pop<float>()); break;
			case Instruction::INT_EQ: push<bool>(pop<int>() == pop<int>()); break;
			case Instruction::INT_NEQ: push<bool>(pop<int>() != pop<int>()); break;
			case Instruction::MUL_FLOAT: push<float>(pop<float>() * pop<float>()); break;
			case Instruction::DIV_FLOAT:
			{
				float f = pop<float>();
				push<float>(pop<float>() / f);
			}
			break;
			case Instruction::UNARY_MINUS: push<float>(-pop<float>()); break;
			case Instruction::OR:
			{
				bool b1 = pop<bool>();
				bool b2 = pop<bool>();
				push<bool>(b1 || b2);
			}
			break;
			case Instruction::AND:
			{
				bool b1 = pop<bool>();
				bool b2 = pop<bool>();
				push<bool>(b1 && b2);
			}
			break;
			case Instruction::NOT: push<bool>(!pop<bool>()); break;
			default: ASSERT(false); break;
		}
	}
}


int ExpressionCompiler::toPostfix(const char* src, const Token* input, Token* output, int count)
{
	Token func_stack[64];
	int func_stack_idx = 0;
	Token* out = output;
	int out_token_count = count;
	for(int i = 0; i < count; ++i)
	{
		const Token& token = input[i];
		if(token.type == Token::NUMBER)
		{
			*out = token;
			++out;
		}
		else if (token.type == Token::LEFT_PARENTHESIS)
		{
			if(i > 0 && input[i - 1].type == Token::NUMBER) {
				m_compile_time_error = Condition::Error::UNEXPECTED_CHAR;
				m_compile_time_offset = token.offset;
				return -1;
			}
			--out_token_count;
			func_stack[func_stack_idx] = token;
			++func_stack_idx;
		}
		else if (token.type == Token::RIGHT_PARENTHESIS)
		{
			if(i > 0 && input[i - 1].type == Token::COMMA) {
				m_compile_time_error = Condition::Error::UNEXPECTED_CHAR;
				m_compile_time_offset = token.offset;
				return -1;
			}

			--out_token_count;
			while (func_stack_idx > 0 && func_stack[func_stack_idx - 1].type != Token::LEFT_PARENTHESIS)
			{
				--func_stack_idx;
				*out = func_stack[func_stack_idx];
				++out;
			}

			if (func_stack_idx > 0)
			{
				--func_stack_idx;
			}
			else
			{
				m_compile_time_error = Condition::Error::MISSING_LEFT_PARENTHESIS;
				m_compile_time_offset = token.offset;
				return -1;
			}
		}
		else if (token.type == Token::COMMA)
		{
			if(i > 0 && (input[i - 1].type == Token::COMMA || input[i - 1].type == Token::LEFT_PARENTHESIS)) {
				m_compile_time_error = Condition::Error::UNEXPECTED_CHAR;
				m_compile_time_offset = token.offset;
				return -1;
			}
			--out_token_count;
			while (func_stack_idx > 0 && func_stack[func_stack_idx - 1].type != Token::LEFT_PARENTHESIS)
			{
				--func_stack_idx;
				*out = func_stack[func_stack_idx];
				++out;
			}

			if (func_stack_idx == 0) {
				m_compile_time_error = Condition::Error::UNEXPECTED_CHAR;
				m_compile_time_offset = token.offset;
				return -1;
			}
		}
		else
		{
			const int prio = getPriority(token);
			if (getFunctionIdx(src, token) != 0xffFF && (i >= count - 1 || input[i + 1].type != Token::LEFT_PARENTHESIS)) {
				m_compile_time_error = Condition::Error::MISSING_LEFT_PARENTHESIS;
				m_compile_time_offset = token.offset;
				return -1;
			}
			while(func_stack_idx > 0 && getPriority(func_stack[func_stack_idx - 1]) > prio)
			{
				--func_stack_idx;
				*out = func_stack[func_stack_idx];
				++out;
			}

			func_stack[func_stack_idx] = token;
			++func_stack_idx;
		}
	}

	for(int i = func_stack_idx - 1; i >= 0; --i)
	{
		if(func_stack[i].type == Token::LEFT_PARENTHESIS)
		{
			m_compile_time_error = Condition::Error::MISSING_RIGHT_PARENTHESIS;
			m_compile_time_offset = func_stack[i].offset;
			return -1;
		}
		*out = func_stack[i];
		++out;
	}

	return out_token_count;
}


void ExpressionVM::callFunction(u16 idx, const RuntimeContext& rc)
{
	switch(idx)
	{
		case 0: push<float>(sinf(pop<float>())); break;
		case 1: push<float>(cosf(pop<float>())); break;
		case 2: {
			const float a = pop<float>();
			const float b = pop<float>();
			const float epsilon = pop<float>();
			ASSERT(epsilon >= 0);
			const bool res = a-b > -epsilon && a-b < epsilon;
			push<bool>(res); 
			break;
		}
		// TODO
		//case 3: push<float>(rc.current->getTime()); break;
		//case 4: push<float>(rc.current->getLength()); break;
		//case 5: push<bool>(rc.current->getTime() > rc.current->getLength() - rc.edge->length); break;
		default: ASSERT(false); break;
	}
}


static const struct
{
	ExpressionCompiler::Token::Operator op;
	Types ret_type;
	Instruction::Type instr;
	Types args[9];
	int priority;

	int arity() const
	{
		for (int i = 0; i < sizeof(args) / sizeof(args[0]); ++i)
		{
			if (args[i] == Types::NONE) return i;
		}
		return 0;
	}

	bool checkArgTypes(const Types* stack, int idx) const
	{
		for (int i = 0; i < arity(); ++i)
		{
			if (args[i] != stack[idx - i - 1]) return false;
		}
		return true;
	}
} OPERATOR_FUNCTIONS[] = {
	{ExpressionCompiler::Token::ADD,
		Types::FLOAT,
		Instruction::ADD_FLOAT,
		{Types::FLOAT, Types::FLOAT, Types::NONE},
		3},
	{ExpressionCompiler::Token::MULTIPLY,
		Types::FLOAT,
		Instruction::MUL_FLOAT,
		{Types::FLOAT, Types::FLOAT, Types::NONE},
		4},
	{ExpressionCompiler::Token::DIVIDE,
		Types::FLOAT,
		Instruction::DIV_FLOAT,
		{Types::FLOAT, Types::FLOAT, Types::NONE},
		4},
	{ExpressionCompiler::Token::SUBTRACT,
		Types::FLOAT,
		Instruction::SUB_FLOAT,
		{Types::FLOAT, Types::FLOAT, Types::NONE},
		3},
	{ExpressionCompiler::Token::UNARY_MINUS,
		Types::FLOAT,
		Instruction::UNARY_MINUS,
		{Types::FLOAT, Types::NONE},
		5},
	{ExpressionCompiler::Token::LESS_THAN,
		Types::BOOL,
		Instruction::FLOAT_LT,
		{Types::FLOAT, Types::FLOAT, Types::NONE},
		2},
	{ExpressionCompiler::Token::EQUAL,
		Types::BOOL,
		Instruction::INT_EQ,
		{Types::U32, Types::U32, Types::NONE},
		2},
	{ExpressionCompiler::Token::NOT_EQUAL,
		Types::BOOL,
		Instruction::INT_NEQ,
		{Types::U32, Types::U32, Types::NONE},
		2},
	{ExpressionCompiler::Token::GREATER_THAN,
		Types::BOOL,
		Instruction::FLOAT_GT,
		{Types::FLOAT, Types::FLOAT, Types::NONE},
		2},
	{ExpressionCompiler::Token::AND,
		Types::BOOL,
		Instruction::AND,
		{Types::BOOL, Types::BOOL, Types::NONE},
		1},
	{ExpressionCompiler::Token::OR,
		Types::BOOL,
		Instruction::OR,
		{Types::BOOL, Types::BOOL, Types::NONE},
		0},
	{ExpressionCompiler::Token::NOT,
		Types::BOOL,
		Instruction::NOT,
		{Types::BOOL, Types::NONE},
		3}
};


int ExpressionCompiler::getPriority(const Token& token)
{
	if(token.type == Token::IDENTIFIER) return 6;
	if (token.type == Token::LEFT_PARENTHESIS) return -1;
	if (token.type != Token::OPERATOR) ASSERT(false);
	
	for (auto& i : OPERATOR_FUNCTIONS)
	{
		if (i.op == token.oper) return i.priority;
	}
	return -1;
}


int ExpressionCompiler::compile(const char* src,
	const Token* tokens,
	int token_count,
	u8* byte_code,
	int max_size,
	InputDecl& decl)
{
	ASSERT(max_size >= 2 + sizeof(bool));
	u8* out = byte_code;
	if (token_count == 0)
	{
		*out = Instruction::PUSH_BOOL;
		++out;
		*(bool*)out = true;
		out += sizeof(bool);
		*out = Instruction::RET_BOOL;
		++out;
		return int(out - byte_code);
	}

	Types type_stack[50];
	int type_stack_idx = 0;
	for (int i = 0; i < token_count; ++i)
	{
		auto& token = tokens[i];

		switch(token.type)
		{
			case Token::NUMBER:
				if (max_size - (out - byte_code) < sizeof(float) + 1)
				{
					m_compile_time_error = Condition::Error::OUT_OF_MEMORY;
					return -1;
				}
				*out = Instruction::PUSH_FLOAT;
				type_stack[type_stack_idx] = Types::FLOAT;
				++type_stack_idx;
				++out;
				*(float*)out = token.number;
				out += sizeof(float);
				break;
			case Token::OPERATOR:
				for (auto& fn : OPERATOR_FUNCTIONS)
				{
					if (token.oper != fn.op) continue;

					if (type_stack_idx < fn.arity())
					{
						m_compile_time_error = Condition::Error::NOT_ENOUGH_PARAMETERS;
						m_compile_time_offset = token.offset;
						return -1;
					}
					if (!fn.checkArgTypes(type_stack, type_stack_idx))
					{
						m_compile_time_error = Condition::Error::INCORRECT_TYPE_ARGS;
						m_compile_time_offset = token.offset;
						return -1;
					}
					type_stack_idx -= fn.arity();
					type_stack[type_stack_idx] = fn.ret_type;
					++type_stack_idx;
					*out = fn.instr;
					++out;
					break;
				}
				break;
			case Token::IDENTIFIER:
				{
					u16 func_idx = getFunctionIdx(src, token);
					if(func_idx != 0xffFF)
					{
						auto& fn = FUNCTIONS[func_idx];
						if (type_stack_idx < fn.arity())
						{
							m_compile_time_error = Condition::Error::NOT_ENOUGH_PARAMETERS;
							m_compile_time_offset = token.offset;
							return -1;
						}

						if (!fn.checkArgTypes(type_stack, type_stack_idx))
						{
							m_compile_time_error = Condition::Error::INCORRECT_TYPE_ARGS;
							m_compile_time_offset = token.offset;
							return -1;
						}

						type_stack_idx -= fn.arity();
						type_stack[type_stack_idx] = fn.ret_type;
						++type_stack_idx;
						*out = Instruction::CALL;
						++out;
						*(u16*)out = func_idx;
						out += sizeof(u16);
					}
					else
					{
						int input_idx = decl.getInputIdx(src + token.offset, token.size);
						int const_idx = decl.getConstantIdx(src + token.offset, token.size);
						if (input_idx >= 0)
						{
							auto& input = decl.inputs[input_idx];
							switch (input.type)
							{
								case InputDecl::FLOAT:
									*out = Instruction::INPUT_FLOAT;
									type_stack[type_stack_idx] = Types::FLOAT;
									++type_stack_idx;
									++out;
									*(int*)out = input.offset;
									out += sizeof(int);
									break;
								case InputDecl::U32:
									*out = Instruction::INPUT_U32;
									type_stack[type_stack_idx] = Types::U32;
									++type_stack_idx;
									++out;
									*(int*)out = input.offset;
									out += sizeof(int);
									break;
								case InputDecl::BOOL:
									*out = Instruction::INPUT_BOOL;
									type_stack[type_stack_idx] = Types::BOOL;
									++type_stack_idx;
									++out;
									*(int*)out = input.offset;
									out += sizeof(int);
									break;
								default: ASSERT(false); break;
							}
						}
						else if (const_idx >= 0)
						{
							auto& constant = decl.constants[const_idx];
							switch (constant.type)
							{
								case InputDecl::FLOAT:
									*out = Instruction::PUSH_FLOAT;
									type_stack[type_stack_idx] = Types::FLOAT;
									++type_stack_idx;
									++out;
									*(float*)out = constant.f_value;
									out += sizeof(float);
									break;
								case InputDecl::U32:
									*out = Instruction::PUSH_U32;
									type_stack[type_stack_idx] = Types::U32;
									++type_stack_idx;
									++out;
									*(int*)out = constant.i_value;
									out += sizeof(int);
									break;
								default: ASSERT(false); break;
							}
						}
						else
						{
							float float_const_value;
							if (!getFloatConstValue(src, token, float_const_value))
							{
								bool bool_const_value;
								if (!getBoolConstValue(src, token, bool_const_value))
								{
									m_compile_time_error = Condition::Error::UNKNOWN_IDENTIFIER;
									m_compile_time_offset = token.offset;
									return -1;
								}
								*out = Instruction::PUSH_BOOL;
								type_stack[type_stack_idx] = Types::BOOL;
								++type_stack_idx;
								++out;
								*(bool*)out = bool_const_value;
								out += sizeof(bool);
							}
							*out = Instruction::PUSH_FLOAT;
							type_stack[type_stack_idx] = Types::FLOAT;
							++type_stack_idx;
							++out;
							*(float*)out = float_const_value;
							out += sizeof(float);
						}
					}
				}
				break;
			default:
				ASSERT(false);
				break;
		}
	}
	if (max_size - (out - byte_code) < 1)
	{
		m_compile_time_error = Condition::Error::OUT_OF_MEMORY;
		return -1;
	}
	if (type_stack_idx < 1)
	{
		m_compile_time_error = Condition::Error::NO_RETURN_VALUE;
		return -1;
	}
	else if (type_stack_idx > 1)
	{
		m_compile_time_error = Condition::Error::UNKNOWN_ERROR;
		return -1;
	}
	switch(type_stack[type_stack_idx - 1])
	{
		case Types::FLOAT: *out = Instruction::RET_FLOAT; break;
		case Types::BOOL: *out = Instruction::RET_BOOL; break;
		default: ASSERT(false); break;
	}
	++out;
	return int(out - byte_code);
}


int ExpressionCompiler::tokenize(const char* src, const Span<Token>& tokens)
{
	static const struct { const char* c; bool binary; ExpressionCompiler::Token::Operator op; } OPERATORS[] = {
		{"<>", true, ExpressionCompiler::Token::NOT_EQUAL },
		{"=", true, ExpressionCompiler::Token::EQUAL },
		{"*", true, ExpressionCompiler::Token::MULTIPLY},
		{"+", true, ExpressionCompiler::Token::ADD},
		{"/", true, ExpressionCompiler::Token::DIVIDE},
		{"<", true, ExpressionCompiler::Token::LESS_THAN},
		{">", true, ExpressionCompiler::Token::GREATER_THAN},
		{"and", true, ExpressionCompiler::Token::AND},
		{"or", true, ExpressionCompiler::Token::OR},
		{ "not", false, ExpressionCompiler::Token::NOT}
	};

	m_compile_time_error = Condition::Error::NONE;
	const char* c = src;
	u32 token_count = 0;
	bool binary = false;
	while (*c) {
		ExpressionCompiler::Token token = { Token::EMPTY, int(c - src) };

		for (auto& i : OPERATORS) {
			if (strncmp(c, i.c, strlen(i.c)) != 0) continue;
			if (i.binary && !binary) {
				m_compile_time_error = Condition::Error::MISSING_BINARY_OPERAND;
				m_compile_time_offset = token.offset;
				return -1;
			}

			token.type = Token::OPERATOR;
			token.oper = i.op;
			binary = false;
			c += strlen(i.c) - 1;
			break;
		}

		if (token.type == Token::EMPTY) {
			switch (*c) {
				case ' ':
				case '\n':
				case '\t': ++c; continue;
				case '-':
					token.type = Token::OPERATOR;
					token.oper = binary ? Token::SUBTRACT : Token::UNARY_MINUS;
					binary = false;
					break;
			}
		}

		if (token.type == Token::EMPTY) {
			if (isIdentifierChar(*c)) {
				token.offset = int(c - src);
				++c;
				token.type = Token::IDENTIFIER;
				binary = true;
				while (isIdentifierChar(*c)) ++c;
				token.size = int(c - src) - token.offset;
				--c;
			}
			else if (*c == '(') {
				binary = false;
				token.type = Token::LEFT_PARENTHESIS;
			}
			else if (*c == ')') {
				token.type = Token::RIGHT_PARENTHESIS;
				binary = true;
			}
			else if (*c == ',') {
				token.type = Token::COMMA;
				binary = false;
			}
			else if (*c >= '0' && *c <= '9') {
				token.type = Token::NUMBER;
				char* out;
				token.number = strtof(c, &out);
				c = out - 1;
				binary = true;
			}
			else {
				m_compile_time_error = Condition::Error::UNEXPECTED_CHAR;
				m_compile_time_offset = token.offset;
				return -1;
			}
		}
		if (token.type != Token::EMPTY) {//-V547
			if(token_count < tokens.length()) {
				tokens[token_count] = token;
			}
			else {
				m_compile_time_error = Condition::Error::OUT_OF_MEMORY;
				return -1;
			}
			++token_count;
		}
		++c;
	}
	return token_count;
}


const char* Condition::errorToString(Error error)
{
	switch (error)
	{
		case Error::NONE: return "None";
		case Error::UNKNOWN_IDENTIFIER: return "Unknown identifier";
		case Error::MISSING_LEFT_PARENTHESIS: return "Missing left parenthesis";
		case Error::MISSING_RIGHT_PARENTHESIS: return "Missing right parenthesis";
		case Error::UNEXPECTED_CHAR: return "Unexpected char";
		case Error::OUT_OF_MEMORY: return "Out of memory";
		case Error::MISSING_BINARY_OPERAND: return "Missing binary operand";
		case Error::NOT_ENOUGH_PARAMETERS: return "Not enough parameters";
		case Error::INCORRECT_TYPE_ARGS: return "Incorrect type args";
		case Error::NO_RETURN_VALUE: return "No return value";
		case Error::UNKNOWN_ERROR: return "Unknown error";
		default: ASSERT(false); return "Undefined error";
	}
}


Condition::Condition(IAllocator& allocator)
	: bytecode(allocator)
{}


bool Condition::eval(const RuntimeContext& rc) const
{
	if(bytecode.size() == 0) return true;
	ExpressionVM vm;
	auto ret = vm.evaluate(&bytecode[0], rc);
	return ret.b_value;
}


void Condition::compile(const char* expression, InputDecl& decl)
{
	ExpressionCompiler compiler;
	ExpressionCompiler::Token tokens[128];
	ExpressionCompiler::Token postfix_tokens[128];
	int tokens_count = compiler.tokenize(expression, Span(tokens));
	if (tokens_count < 0)
	{
		compile("1 < 0", decl);
		error = compiler.getError();
		return;
	}
	tokens_count = compiler.toPostfix(expression, tokens, postfix_tokens, tokens_count);
	if (tokens_count < 0)
	{
		compile("1 < 0", decl);
		error = compiler.getError();
		return;
	}
	bytecode.resize(128);
	int size = compiler.compile(expression, postfix_tokens, tokens_count, &bytecode[0], bytecode.size(), decl);
	if (size < 0)
	{
		compile("1 < 0", decl);
		error = compiler.getError();
		return;
	}
	bytecode.resize(size);
	error = Condition::Error::NONE;
}


void InputDecl::removeInput(u32 index)
{
	inputs[index].type = EMPTY;
	--inputs_count;
	recalculateOffsets();
}


void InputDecl::removeConstant(int index)
{
	constants[index].type = EMPTY;
	--constants_count;
}


int InputDecl::addInput()
{
	ASSERT(inputs_count < lengthOf(inputs));
		
	for (u32 i = 0; i < lengthOf(inputs); ++i)
	{
		if (inputs[i].type == EMPTY)
		{
			inputs[i].name = "";
			inputs[i].type = BOOL;
			++inputs_count;
			recalculateOffsets();
			return i;
		}
	}
	return -1;
}


int InputDecl::addConstant()
{
	ASSERT(constants_count < lengthOf(constants));

	for (u32 i = 0; i < lengthOf(constants); ++i)
	{
		if (constants[i].type == EMPTY)
		{
			constants[i].name = "";
			constants[i].type = BOOL;
			++constants_count;
			return i;
		}
	}
	return -1;
}


int InputDecl::getSize(Type type)
{
	switch (type)
	{
		case FLOAT: return sizeof(float);
		case U32: return sizeof(u32);
		case BOOL: return sizeof(bool);
		default: ASSERT(false); return 1;
	}
}


void InputDecl::recalculateOffsets()
{
	if (inputs_count == 0) return;
	int last_offset = 0;
	for(auto& input : inputs)
	{ 
		if (input.type == EMPTY) continue;
		input.offset = last_offset;
		last_offset += getSize(input.type);
	}
}


int InputDecl::getInputIdx(const char* name, int size) const
{
	for (u32 i = 0; i < lengthOf(inputs); ++i)
	{
		if (inputs[i].type == Type::EMPTY) continue;
		if (strncmp(inputs[i].name, name, size) == 0) return i;
	}
	return -1;
}


int InputDecl::getConstantIdx(const char* name, int size) const
{
	for (u32 i = 0; i < lengthOf(constants); ++i)
	{
		if (constants[i].type != Type::EMPTY && strncmp(constants[i].name, name, size) == 0) return i;
	}
	return -1;
}

} // namespace Anim


} // namespace Lumix