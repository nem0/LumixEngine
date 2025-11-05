#pragma once

#include "core/log.h"
#include "core/string.h"
#include "renderer/particle_system.h"

namespace Lumix {

struct ParticleScriptCompiler {
	using InstructionType = ParticleSystemResource::InstructionType;
	using DataStream = ParticleSystemResource::DataStream;

    enum class Type {
        FLOAT,
        FLOAT3,
        FLOAT4
    };

    struct Constant {
        StringView name;
        Type type;
        float value[4];
    };

	struct Variable {
		StringView name;
		Type type;
		i32 offset = 0;

        i32 getOffsetSub(u32 sub) const {
            switch (type) {
                case Type::FLOAT: return offset;
                case Type::FLOAT3: return offset + minimum(sub, 2);
                case Type::FLOAT4: return offset + minimum(sub, 3);
            }
            ASSERT(false);
            return offset;
        }
	};

	struct Function {
        InstructionType instruction;
        bool returns_value;
    };

    enum class SystemValue {
        TIME_DELTA,
        TOTAL_TIME,
		EMIT_INDEX,
        
        NONE,
    };

	enum class Operators : char {
		ADD = '+',
		SUB = '-',
		DIV = '/',
		MUL = '*',
		LT = '<',
		GT = '>',
	};

	struct ExpressionStackElement {
		enum Type {
			NONE,
			LITERAL,
			OPERATOR,
			FUNCTION,
			VARIABLE,
			SYSTEM_VALUE,
			REGISTER,
		};

		Type type;
		float literal_value;
		Operators operator_char;
		InstructionType function;
		i32 variable_index;
		i32 register_offset;
		SystemValue system_value;
		u8 sub;
		u32 parenthesis_depth = 0xffFFffFF;
	};

	struct Emitter {
		Emitter(IAllocator& allocator)
			: m_outputs(allocator)
			, m_update(allocator)
			, m_emit(allocator)
			, m_output(allocator)
			, m_inputs(allocator)
			, m_vars(allocator)
		{}

		StringView m_name;
		Path m_material;
		Path m_mesh;
		OutputMemoryStream m_update;
		OutputMemoryStream m_emit;
		OutputMemoryStream m_output;
		Array<Variable> m_vars;
		Array<Variable> m_outputs;
		Array<Variable> m_inputs;
		u32 m_num_used_registers = 0;
		u32 m_register_allocator = 0;
		u32 m_init_emit_count = 0;
		float m_emit_per_second = 0;
		u32 m_max_ribbons = 0;
		u32 m_max_ribbon_length = 0;
		u32 m_init_ribbons_count = 0;
	};
	
	ParticleScriptCompiler(StringView content, const Path& path, IAllocator& allocator)
		: m_allocator(allocator)
		, m_content(content)
		, m_emitters(allocator)
		, m_constants(allocator)
		, m_stack(allocator)
		, m_postfix(allocator)
		, m_path(path)
        , m_text_begin(content.begin)
	{}

	void skipWhitespaces() {
		while (m_content.begin != m_content.end && isWhitespace(*m_content.begin)) ++m_content.begin;
		if (m_content.begin < m_content.end - 1
			&& m_content.begin[0] == '/'
			&& m_content.begin[1] == '/') 
		{
			m_content.begin += 2;
			while (m_content.begin != m_content.end && *m_content.begin != '\n') {
				++m_content.begin;
			}
			skipWhitespaces();
		}
	}

	bool expectNotEOF() {
		skipWhitespaces();
		if (m_content.size() == 0) {
			error(m_content, "Unexpected end of file");
			return false;
		}
		return true;
	}

	StringView tryReadWord() {
		StringView res = m_content;
		res.end = res.begin;
		if (res.end != m_content.end) {
			++res.end;
			if (!isNumeric(*res.begin) && !isLetter(*res.begin) && *res.begin != '_') {
				m_content.begin = res.end;
				return res;
			}
		}
		while (res.end != m_content.end && (isNumeric(*res.end) || isLetter(*res.end) || *res.end == '_')) ++res.end;
		m_content.begin = res.end;
		return res;
	}

	StringView readWord() {
		expectNotEOF();
		StringView w = tryReadWord();
		if (w.size() == 0) error(w, "Expected a word"); 
		return w;
	}

    i32 getLine(StringView location) {
        ASSERT(location.begin <= m_content.end);
        const char* c = m_text_begin;
        i32 line = 1;
        while(c < location.begin) {
            if (*c == '\n') ++line;
            ++c;
        }
        return line;
    }

	void error(StringView location, const char* error_msg) {
		if(!m_is_error) logError(m_path, "(", getLine(location), "): ", error_msg);
		m_is_error = true;
	}

	void error(StringView location, const char* error_msg, const char* error_msg2) {
		if(!m_is_error) logError(m_path, "(", getLine(location), "): ", error_msg2);
		m_is_error = true;
	}

	void expect(const char* v) {
		StringView w = readWord();
		if (!equalStrings(w, v)) error(w, "Expected ", v);
	}

	StringView readIdentifier() {
		expectNotEOF();
		StringView res = m_content;
		res.end = res.begin;
		if (!isLetter(*res.begin) && *res.begin != '_') error(res, "Identifier must start with a letter or _"); 
		++res.end;
		while (res.end != m_content.end && (isNumeric(*res.end) || isLetter(*res.end) || *res.end == '_')) {
			 ++res.end;
		}
		m_content.begin = res.end;
		return res;
	}

	i32 getEmitterIndex(StringView name) {
		for (const Emitter& emitter : m_emitters) {
			if (equalStrings(emitter.m_name, name)) return i32(&emitter - m_emitters.begin());
		}
		return -1;
	}

	Function getFunction(StringView name) {
		if (equalStrings(name, "cos")) return { InstructionType::COS, true };
		else if (equalStrings(name, "sin")) return { InstructionType::SIN, true };
		else if (equalStrings(name, "kill")) return { InstructionType::KILL, false };
		else if (equalStrings(name, "random")) return { InstructionType::RAND, true };
		else if (equalStrings(name, "curve")) return { InstructionType::GRADIENT, true };
		else if (equalStrings(name, "emit")) return { InstructionType::EMIT, false };
		else if (equalStrings(name, "mesh")) return { InstructionType::MESH, true };
		else if (equalStrings(name, "noise")) return { InstructionType::NOISE, true };
		else return { InstructionType::END };
	}

    Constant* getConstant(StringView name) {
        for (Constant& c : m_constants) {
            if (equalStrings(c.name, name)) return &c;
        }
        return nullptr;
    }

	i32 getVariableIndex(const Emitter& emitter, StringView name) {
		for (const Variable& var : emitter.m_vars) {
			if (equalStrings(var.name, name)) return i32(&var - emitter.m_vars.begin());
		}
		return -1;
	}

	SystemValue getSystemValue(StringView name) {
		if (equalStrings(name, "time_delta")) return SystemValue::TIME_DELTA;
		if (equalStrings(name, "total_time")) return SystemValue::TOTAL_TIME;
		// TODO error if emit_index is used outside emit
		if (equalStrings(name, "emit_index")) return SystemValue::EMIT_INDEX;
		return SystemValue::NONE;
	}

	i32 getOutputIndex(const Emitter& emitter, StringView name) {
		for (const Variable& var : emitter.m_outputs) {
			if (equalStrings(var.name, name)) return i32(&var - emitter.m_outputs.begin());
		}
		return -1;
	}

	i32 getInputIndex(const Emitter& emitter, StringView name) {
		for (const Variable& var : emitter.m_inputs) {
			if (equalStrings(var.name, name)) return i32(&var - emitter.m_inputs.begin());
		}
		return -1;
	}

	float readNumberOrSub(u32 sub) {
		expectNotEOF();
        if (peekChar() != '{') return readNumber();
        ++m_content.begin;
        skipWhitespaces();
        float vals[4] = {};
        vals[0] = readNumber();
        expect(",");
        vals[1] = readNumber();
        if (peekChar() == '}') {
            if (sub > 1) error (m_content, "Expected ,");
        }
        else {
            expect(",");
            vals[2] = readNumber();
            if (peekChar() == '}') {
                if (sub > 2) error (m_content, "Expected ,");
            }
            else {
                expect(",");
                vals[3] = readNumber();
            }
        }
        expect("}");

        return vals[sub];
    }

	float readNumber() {
		expectNotEOF();
		
        bool is_negative = false;
		if (*m_content.begin == '-') {
            is_negative = true;
            ++m_content.begin;
            if (m_content.size() == 0) {
                error(m_content, "Expected a number");
                return 0;
            }
        }
        if (!isNumeric(*m_content.begin)) {
            StringView ident = readIdentifier();
            Constant* c = getConstant(ident);
            if (c) {
                return c->value[0] * (is_negative ? -1 : 1);
            }
            else {
                error(ident, "Expected a number");
            }
            return 0;
        }
		
		StringView s;
		s.begin = m_content.begin;
		s.end = s.begin + 1;
		while (s.end != m_content.end && isNumeric(*s.end)) ++s.end;
		if (s.end == m_content.end || *s.end != '.') {
			m_content.begin = s.end;
			float v;
			fromCString(s, v);
            if (is_negative) v = -v;
	 		return v;
		}
		++s.end;
		while (s.end != m_content.end && isNumeric(*s.end)) ++s.end;
		m_content.begin = s.end;
		float v;
		fromCString(s, v);
        if (is_negative) v = -v;
		return v;
	}

	char peekChar() {
		if (m_content.size() == 0) {
			error(m_content, "Unexpected end of file");
			return 0;
		}
		return *m_content.begin;
	}

	static void deallocRegister(Emitter& emitter, const DataStream& stream) {
		if (stream.type == DataStream::REGISTER) {
			emitter.m_register_allocator = emitter.m_register_allocator & ~(1 << stream.index);
		}
	}

	DataStream allocRegister(Emitter& emitter) {
		DataStream res;
		res.type = DataStream::REGISTER;
		for (u32 i = 0; i < 32; ++i) {
			if ((emitter.m_register_allocator & (1 << i)) == 0) {
				res.index = i;
				emitter.m_num_used_registers = maximum(emitter.m_num_used_registers, i + 1);
				emitter.m_register_allocator |= 1 << i;
				return res;
			}
		}
		error(m_content, "Run out of allocators");
		return res;
	}
	
	bool isOperator(char c) { 
		switch ((Operators)c) {
			case Operators::ADD: case Operators::SUB:
			case Operators::MUL: case Operators::DIV:
			case Operators::LT: case Operators::GT:
				return true;
		}
		return false;
	}

	DataStream compilePostfix(Emitter& emitter, OutputMemoryStream& compiled) {
		ExpressionStackElement el = m_postfix.last();
		m_postfix.pop();
		
		if (el.type == ExpressionStackElement::LITERAL) {
			DataStream res;
			res.type = DataStream::LITERAL;
			res.value = el.literal_value;
			return res;
		}

		if (el.type == ExpressionStackElement::SYSTEM_VALUE) {
			DataStream res;
			res.type = DataStream::CONST;
			res.index = (u8)el.system_value;
			return res;
		}

		if (el.type == ExpressionStackElement::VARIABLE) {
			DataStream res;
			res.type = DataStream::CHANNEL;
			res.index = emitter.m_vars[el.variable_index].getOffsetSub(el.sub);
			return res;
		}

		if (el.type == ExpressionStackElement::REGISTER) {
			DataStream res;
			res.type = DataStream::REGISTER;
			res.index = el.register_offset;
			return res;
		}

		if (el.type == ExpressionStackElement::OPERATOR) {
			if (m_postfix.size() < 2) {
				// TODO error location
				error(m_content, "Operator must have left and right side");
				return {};
			}
			DataStream op2 = compilePostfix(emitter, compiled);
			DataStream op1 = compilePostfix(emitter, compiled);
			switch (el.operator_char) {
				case Operators::ADD: compiled.write(InstructionType::ADD); break;
				case Operators::SUB: compiled.write(InstructionType::SUB); break;
				case Operators::MUL: compiled.write(InstructionType::MUL); break;
				case Operators::DIV: compiled.write(InstructionType::DIV); break;
				case Operators::LT: compiled.write(InstructionType::LT); break;
				case Operators::GT: compiled.write(InstructionType::GT); break;
			}

			DataStream res = allocRegister(emitter);
			compiled.write(res);
			compiled.write(op1);
			compiled.write(op2);

			deallocRegister(emitter, op1);
			deallocRegister(emitter, op2);
			return res;
		}

		if (el.type == ExpressionStackElement::FUNCTION) {
			if (el.function == InstructionType::RAND) {
				DataStream res = allocRegister(emitter);
				compiled.write(el.function);
				compiled.write(res);
				if (m_postfix.size() < 2) {
					// TODO error location
					error(m_content, "rand expects 2 arguments");
					return {};
				}
				ExpressionStackElement arg1 = m_postfix.last();
				m_postfix.pop();
				ExpressionStackElement arg0 = m_postfix.last();
				m_postfix.pop();
				if (arg0.type != ExpressionStackElement::LITERAL || arg0.type != ExpressionStackElement::LITERAL) {
					// TODO error location
					error(m_content, "rand expects 2 literals");
					return {};
				}

				compiled.write(arg0.literal_value);
				compiled.write(arg1.literal_value);
				return res;
			}

			if (el.function == InstructionType::MESH) {
				DataStream res = allocRegister(emitter);
				if (m_mesh_stream.type == DataStream::NONE) {
					m_mesh_stream = allocRegister(emitter);
				}
				compiled.write(el.function);
				compiled.write(res);
				compiled.write(m_mesh_stream);
				compiled.write(el.sub);
				return res;
			}
			
			if (el.function == InstructionType::GRADIENT) {
				DataStream res = allocRegister(emitter);
				compiled.write(el.function);
				compiled.write(res);

				float keys[8];
				float values[8];
				u32 num_frames = 0;
				DataStream t;
				for (;;) {
					DataStream stream = compilePostfix(emitter, compiled);
					if (stream.type != DataStream::LITERAL) {
						t = stream;
						break;
					}
					keys[num_frames] = stream.value;

					stream = compilePostfix(emitter, compiled);
					if (stream.type != DataStream::LITERAL) {
						// TODO error location
						error(m_content, "Literal expected");
						return {};
					}
					values[num_frames] = stream.value;
					++num_frames;
				}

				compiled.write(t);
				deallocRegister(emitter, t);
				compiled.write(num_frames);
				compiled.write(keys, sizeof(keys[0]) * num_frames);
				compiled.write(values, sizeof(values[0]) * num_frames);
				return res;
			}

			DataStream op1 = compilePostfix(emitter, compiled);
			DataStream res = allocRegister(emitter);
			compiled.write(el.function);
			compiled.write(res);
			compiled.write(op1);
			deallocRegister(emitter, op1);

			return res;
		}

		ASSERT(false);
		return {};
	}

	u32 getPriority(const ExpressionStackElement& el) {
		u32 prio = el.parenthesis_depth * 10;
		switch (el.type) {
			case ExpressionStackElement::FUNCTION: prio += 9; break;
			case ExpressionStackElement::OPERATOR: 
				switch(el.operator_char) {
					case Operators::GT: case Operators::LT: prio += 0; break;
					case Operators::ADD: case Operators::SUB: prio += 1; break;
					case Operators::MUL: case Operators::DIV: prio += 2; break;
				}
				break;
			default: ASSERT(false);
		}
		return prio;
	}

	void pushStack(const ExpressionStackElement& el) {
		u32 priority = getPriority(el);
		while (!m_stack.empty()) {
			if (getPriority(m_stack.last()) < priority) break;

			m_postfix.push(m_stack.last());
			m_stack.pop();
		}
		m_stack.push(el);
	}

	DataStream compileExpression(Emitter& emitter, OutputMemoryStream& compiled, u32 sub, char expression_end = ';') {
		m_stack.clear();
		m_postfix.clear();

		const char* expression_start = m_content.begin;
		bool can_be_unary = true;
		// convert tokens to postfix notation and then compile
		u32 parenthesis_depth = 0;
		for (;;) {
			expectNotEOF();

			const char peeked = peekChar();
			if (peeked == expression_end) {
				++m_content.begin;
				while (!m_stack.empty()) {
					m_postfix.push(m_stack.last());
					m_stack.pop();
				}
				if (m_postfix.empty()) {
					error(expression_start, "Empty expression");
					return {};
				}
				return compilePostfix(emitter, compiled);
			}

			if (peeked == ')') {
				if (parenthesis_depth == 0) {
					error(m_content, "Unexpected )");
					return {};
				}
				++m_content.begin;
				--parenthesis_depth;
				can_be_unary = false;
				continue;
			}

			if (peeked == '(') {
				can_be_unary = true;
				++parenthesis_depth;
				++m_content.begin;
				continue;
			}

			if (peeked == ',') {
				if (parenthesis_depth == 0) {
					error(m_content, "Unexpected ,");
					return {};
				}
				can_be_unary = true;
				++m_content.begin;
				continue;
			}

			if (peeked == '{') {
				ExpressionStackElement& el = m_postfix.emplace();
				el.type = ExpressionStackElement::LITERAL;
				el.literal_value = readNumberOrSub(sub);
				el.parenthesis_depth = parenthesis_depth;
				can_be_unary = true;
				continue;
			}			

			if (isNumeric(peeked)) {
				ExpressionStackElement& el = m_postfix.emplace();
				el.type = ExpressionStackElement::LITERAL;
				el.literal_value = readNumber();
				el.parenthesis_depth = parenthesis_depth;
				can_be_unary = false;
				continue;
			}

			if (isOperator(peeked)) {
				if (can_be_unary && peeked == '-') {
					can_be_unary = false;
					++m_content.begin;
					if (!expectNotEOF()) return {};
					if (isNumeric(peekChar())) {
						float v = -readNumber();
					
						ExpressionStackElement& el = m_postfix.emplace();
						el.type = ExpressionStackElement::LITERAL;
						el.literal_value = v;
						continue;
					}

					StringView iden = readIdentifier();
					Constant* c = getConstant(iden);
					if (c) {
						ExpressionStackElement& el = m_postfix.emplace();
						el.type = ExpressionStackElement::LITERAL;
						el.literal_value = -c->value[sub];
						continue;
					}

					error(iden, "Only literal or const is supported after unary -");
					return {};
				}
				++m_content.begin;
				ExpressionStackElement el;
				el.type = ExpressionStackElement::OPERATOR;
				el.operator_char = (Operators)peeked;
				el.parenthesis_depth = parenthesis_depth;
				pushStack(el);
				can_be_unary = true;
				continue;
			}
			
			StringView ident = readIdentifier();
			if (Constant* c = getConstant(ident)) {
				ExpressionStackElement& el = m_postfix.emplace();
				el.type = ExpressionStackElement::LITERAL;
				el.literal_value = c->value[sub];
				el.parenthesis_depth = parenthesis_depth;
				can_be_unary = false;
				continue;
			}

			Function fn = getFunction(ident);
			if (fn.instruction != InstructionType::END) {
				ExpressionStackElement el;
				el.type = ExpressionStackElement::FUNCTION;
				el.function = fn.instruction;
				el.parenthesis_depth = parenthesis_depth;
				el.sub = sub;
				pushStack(el);
				expect("(");
				++parenthesis_depth;
				can_be_unary = true;
				continue;
			}

			i32 input_index = getInputIndex(emitter, ident);
			if (input_index >= 0) {
				ExpressionStackElement& el = m_postfix.emplace();
				el.type = ExpressionStackElement::REGISTER;
				el.register_offset = emitter.m_inputs[input_index].getOffsetSub(sub);
				el.parenthesis_depth = parenthesis_depth;
				can_be_unary = false;
				continue;
			}

			i32 var_index = getVariableIndex(emitter, ident);
			if (var_index >= 0) {
				ExpressionStackElement& el = m_postfix.emplace();
				el.type = ExpressionStackElement::VARIABLE;
				el.variable_index = var_index;
				el.parenthesis_depth = parenthesis_depth;
				can_be_unary = false;
			
				if (peekChar() != '.') {
					el.sub = sub;
					continue;
				}

				++m_content.begin;
				switch(peekChar()) {
					case 'x': case 'r': el.sub = 0; break;
					case 'y': case 'g': el.sub = 1; break;
					case 'z': case 'b': el.sub = 2; break;
					case 'w': case 'a': el.sub = 3; break;
					default: error(m_content, "Unknown subscript"); --m_content.begin; break;
				}
				++m_content.begin;
				continue;
			}
				
            SystemValue system_value = getSystemValue(ident);
            if (system_value != SystemValue::NONE) {
				ExpressionStackElement& el = m_postfix.emplace();
				el.type = ExpressionStackElement::SYSTEM_VALUE;
				el.system_value = system_value;
				el.parenthesis_depth = parenthesis_depth;
				can_be_unary = false;
				continue;
			}

			error(ident, "Unknown identifier");
			return {};
		}
		return {};
	}

	void compileAssignment(Emitter& emitter, OutputMemoryStream& compiled, Variable& variable, DataStream::Type stream_type) {
		i32 offset = variable.offset;
		switch (variable.type) {
			case Type::FLOAT: {
				expect("=");
				DataStream expr_result = compileExpression(emitter, compiled, 0);
				DataStream dst;
				dst.type = stream_type;
				dst.index = offset;
				compiled.write(InstructionType::MOV);
				compiled.write(dst);
				compiled.write(expr_result);
				break;
			}
			case Type::FLOAT4:
			case Type::FLOAT3: {
				const bool is_float4 = variable.type == Type::FLOAT4;
				if (peekChar() == '.') {
					++m_content.begin;
					StringView sub = readWord();
					if (equalStrings(sub, "y")) offset += 1; 
					else if (equalStrings(sub, "z")) offset += 2; 
					else if (equalStrings(sub, "g")) offset += 1; 
					else if (equalStrings(sub, "b")) offset += 2; 
					else if (is_float4) {
						if (equalStrings(sub, "w")) offset += 3; 
						else if (equalStrings(sub, "a")) offset += 3; 
					} 

					expect("=");
					DataStream expr_result = compileExpression(emitter, compiled, 0);
					DataStream dst;
					dst.type = stream_type;
					dst.index = offset;
					compiled.write(InstructionType::MOV);
					compiled.write(dst);
					compiled.write(expr_result);
				}
				else {
					expect("=");
					for (i32 i = 0; i < (is_float4 ? 4 : 3); ++i) {
						StringView content = m_content;
						DataStream expr_result = compileExpression(emitter, compiled, i);
						DataStream dst;
						dst.type = stream_type;
						dst.index = offset + i;
						compiled.write(InstructionType::MOV);
						compiled.write(dst);
						compiled.write(expr_result);
						deallocRegister(emitter, expr_result);
						if (i < (is_float4 ? 3 : 2)) m_content = content;
					}
				}
				break;
			}
		}
		emitter.m_register_allocator = 0;
	}

	bool compileEmitBlock(Emitter& emitter, OutputMemoryStream& compiled, Emitter& emitted) {
		for (;;) {
			StringView word = readWord();
			if (equalStrings(word, "}")) break;

			i32 out_index = getInputIndex(emitted, word);
			if (out_index >= 0) {
				compileAssignment(emitter, compiled, emitted.m_inputs[out_index], DataStream::OUT);
				continue;
			}

			error(word, "Syntax error");
			return false;
		}
		compiled.write(InstructionType::END);
		return true;
	}

	void compileFunction(Emitter& emitter) {
		StringView fn_name = readIdentifier();
		expect("{");
		
		OutputMemoryStream& compiled = [&]() -> OutputMemoryStream& {
			if (equalStrings(fn_name, "update")) return emitter.m_update;
			if (equalStrings(fn_name, "emit")) return emitter.m_emit;
			if (equalStrings(fn_name, "output")) return emitter.m_output;
			error(m_content, "Unknown function");
			return emitter.m_output;
		}();

		for (;;) {
			StringView word = readWord();
			if (equalStrings(word, "}")) break;
			i32 var_index = getVariableIndex(emitter, word);
			if (var_index >= 0) {
				compileAssignment(emitter, compiled, emitter.m_vars[var_index], DataStream::CHANNEL);
				continue;
			}

			i32 out_index = getOutputIndex(emitter, word);
			if (out_index >= 0) {
				compileAssignment(emitter, compiled, emitter.m_outputs[out_index], DataStream::OUT);
				continue;
			}

			Function fn = getFunction(word);
			if (fn.instruction != InstructionType::END) {
				if (fn.returns_value) error(word, "Unexpected function call");
						
				if (fn.instruction == InstructionType::EMIT) {
					expect("(");
					StringView ident = readIdentifier();
					i32 emitter_index = getEmitterIndex(ident);
					if (emitter_index < 0) {
						error(ident, "Unknown emitter");
						return;
					}
					expect(",");
					
					DataStream arg_val = compileExpression(emitter, compiled, 0, ')');
					compiled.write(fn.instruction);
					compiled.write(arg_val);
					compiled.write(emitter_index);
					
					expectNotEOF();
					if (peekChar() == ';') {
						compiled.write(InstructionType::END);
						++m_content.begin;
						continue;
					}
					if (peekChar() != '{') {
						error(m_content, "Expected ; or {");
						return;
					}
					++m_content.begin;
					if (!compileEmitBlock(emitter, compiled, m_emitters[emitter_index])) return;
					continue;
				}

				DataStream arg_val = compileExpression(emitter, compiled, 0);
				compiled.write(fn.instruction);
				compiled.write(arg_val);
				continue;
			}

			error(word, "Syntax error");
			return;
		}
		compiled.write(InstructionType::END);
		// TODO optimize the bytecode
		// e.g. there are unnecessary movs
	}

	Type readType() {
		StringView type = readIdentifier();
        Type res = Type::FLOAT;
		if (equalStrings(type, "float")) res = Type::FLOAT;
		else if (equalStrings(type, "float3")) res = Type::FLOAT3;
		else if (equalStrings(type, "float4")) res = Type::FLOAT4;
		else error(type, "Unknown type");
		return res;
	}

	void compileVar(Emitter& emitter) {
		u32 offset = 0;
		if (!emitter.m_vars.empty()) {
			const Variable& var = emitter.m_vars.last();
			offset = var.offset;
			switch (var.type) {
				case Type::FLOAT: ++offset; break;
				case Type::FLOAT3: offset += 3; break;
				case Type::FLOAT4: offset += 4; break;
			}
		}
		Variable& var = emitter.m_vars.emplace();
		var.name = readIdentifier();
		expect(":");
		var.type = readType();
		var.offset = offset;
	}

	void compileEmitter() {
		StringView ident = readIdentifier();
		Emitter& emitter = m_emitters.emplace(m_allocator);
		emitter.m_name = ident;
		expect("{");

		for (;;) {
			StringView word = readWord();
			if (equalStrings(word, "}")) break;
			else if (equalStrings(word, "material")) compileMaterial(emitter);
			else if (equalStrings(word, "mesh")) compileMesh(emitter);
			else if (equalStrings(word, "var")) compileVar(emitter);
			else if (equalStrings(word, "out")) compileOutput(emitter);
			else if (equalStrings(word, "in")) compileInput(emitter);
			else if (equalStrings(word, "fn")) compileFunction(emitter);
			else if (equalStrings(word, "emit_per_second")) emitter.m_emit_per_second = readNumber();
			else if (equalStrings(word, "init_emit_count")) emitter.m_init_emit_count = (u32)readNumber();
			else if (equalStrings(word, "max_ribbons")) emitter.m_max_ribbons = (u32)readNumber();
			else if (equalStrings(word, "max_ribbon_length")) emitter.m_max_ribbon_length = (u32)readNumber();
			else if (equalStrings(word, "init_ribbons_count")) emitter.m_init_ribbons_count = (u32)readNumber();
			else error(word, "Unknown identifier");
		}
		
		if (emitter.m_max_ribbons > 0 && emitter.m_max_ribbon_length == 0) {
			error(m_content, "max_ribbon_length must be > 0 if max_ribbons is > 0");
		}
	}

	void compileConst() {
		StringView name = readIdentifier();
        expect("=");
        Constant& c = m_constants.emplace();
        c.name = name;
        
        if (peekChar() != '{') {
            float value = readNumber();
            c.value[0] = value;
            c.value[1] = value;
            c.value[2] = value;
            c.value[3] = value;
            c.type = Type::FLOAT;
        }
        else {
            ASSERT(false);
            // TODO
        }
        expect(";");
	}

	void compileOutput(Emitter& emitter) {
		u32 offset = 0;
		if (!emitter.m_outputs.empty()) {
			const Variable& var = emitter.m_outputs.last();
			offset = var.offset;
			switch (var.type) {
				case Type::FLOAT: ++offset; break;
				case Type::FLOAT3: offset += 3; break;
				case Type::FLOAT4: offset += 4; break;
			}
		}
		Variable& var = emitter.m_outputs.emplace();
		var.name = readIdentifier();
		expect(":");
		var.type = readType();
		var.offset = offset;
	}

	void compileInput(Emitter& emitter) {
		u32 offset = 0;
		if (!emitter.m_inputs.empty()) {
			const Variable& var = emitter.m_inputs.last();
			offset = var.offset;
			switch (var.type) {
				case Type::FLOAT: ++offset; break;
				case Type::FLOAT3: offset += 3; break;
				case Type::FLOAT4: offset += 4; break;
			}
		}
		Variable& var = emitter.m_inputs.emplace();
		var.name = readIdentifier();
		expect(":");
		var.type = readType();
		var.offset = offset;
	}

	void compileMaterial(Emitter& emitter) {
		expectNotEOF();
		expect("\"");
		StringView path = m_content;
		path.end = path.begin;
		while (path.end != m_content.end && *path.end != '"') ++path.end;
		m_content.begin = path.end;
		expect("\"");
		emitter.m_material = path;
	}

	void compileMesh(Emitter& emitter) {
		expectNotEOF();
		expect("\"");
		StringView path = m_content;
		path.end = path.begin;
		while (path.end != m_content.end && *path.end != '"') ++path.end;
		m_content.begin = path.end;
		expect("\"");
		emitter.m_mesh = path;
	}

	bool compile(OutputMemoryStream& output) {
		for (;;) {
			StringView word = tryReadWord();
			if (word.size() == 0) break;
			if (equalStrings(word, "const")) compileConst();
			else if (equalStrings(word, "emitter")) compileEmitter();
			else if (equalStrings(word, "world_space")) m_is_world_space = true;
			else if (word.size() != 1 || !isWhitespace(word[0])) error(word, "Syntax error");
		}

		ParticleSystemResource::Header header;
		output.write(header);

		ParticleSystemResource::Flags flags = m_is_world_space ? ParticleSystemResource::Flags::WORLD_SPACE : ParticleSystemResource::Flags::NONE;
		output.write(flags);
		
		output.write(m_emitters.size());

		for (const Emitter& emitter : m_emitters) {
			gpu::VertexDecl decl(gpu::PrimitiveType::TRIANGLE_STRIP);
			fillVertexDecl(emitter, decl);
			output.write(decl);
			output.writeString(emitter.m_material);
			output.writeString(emitter.m_mesh);
			const u32 count = u32(emitter.m_update.size() + emitter.m_emit.size() + emitter.m_output.size());
			output.write(count);
			output.write(emitter.m_update.data(), emitter.m_update.size());
			output.write(emitter.m_emit.data(), emitter.m_emit.size());
			output.write(emitter.m_output.data(), emitter.m_output.size());
			output.write((u32)emitter.m_update.size());
			output.write(u32(emitter.m_update.size() + emitter.m_emit.size()));

			auto getCount = [&](const auto& x){
				u32 c = 0;
				for (const auto& i : x) {
					switch (i.type) {
						case Type::FLOAT4: c+= 4; break;
						case Type::FLOAT3: c+= 3; break;
						case Type::FLOAT: c+= 1; break;
					}
				}
				return c;
			};

			output.write(getCount(emitter.m_vars));
			output.write(emitter.m_num_used_registers);
			output.write(getCount(emitter.m_outputs));
			output.write(emitter.m_init_emit_count);
			output.write(emitter.m_emit_per_second);
			output.write(getCount(emitter.m_inputs));
			output.write(emitter.m_max_ribbons);
			output.write(emitter.m_max_ribbon_length);
			output.write(emitter.m_init_ribbons_count);
		}
		
		return !m_is_error;
	}

	void fillVertexDecl(const Emitter& emitter, gpu::VertexDecl& decl) {
		u32 offset = 0;
		for (const Variable& o : emitter.m_outputs) {
			switch (o.type) {
				case Type::FLOAT: 
					decl.addAttribute(offset, 1, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);
					offset += sizeof(float);
					break;
				case Type::FLOAT3: 
					decl.addAttribute(offset, 3, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);
					offset += sizeof(Vec3);
					break;
				case Type::FLOAT4: 
					decl.addAttribute(offset, 4, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);
					offset += sizeof(Vec4);
					break;
			}
		}
	}

	IAllocator& m_allocator;
	Path m_path;
	Array<Emitter> m_emitters;
	Array<Constant> m_constants;
	StackArray<ExpressionStackElement, 16> m_stack;
	StackArray<ExpressionStackElement, 16> m_postfix;
	DataStream m_mesh_stream;
	
	StringView m_content;
    const char* m_text_begin;
	bool m_is_error = false;
	bool m_is_world_space = false;
};

}