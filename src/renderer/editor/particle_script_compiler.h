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

	struct Emitter {
		Emitter(IAllocator& allocator)
			: m_outputs(allocator)
			, m_update(allocator)
			, m_emit(allocator)
			, m_output(allocator)
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
		i32 m_register_allocator = 0;
		u32 m_init_emit_count = 0;
		float m_emit_per_second = 0;
	};
	
	ParticleScriptCompiler(StringView content, const Path& path, IAllocator& allocator)
		: m_allocator(allocator)
		, m_content(content)
		, m_emitters(allocator)
		, m_constants(allocator)
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

	void expectNotEOF() {
		skipWhitespaces();
		if (m_content.size() == 0) error(m_content, "Unexpected end of file");
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

    struct Function {
        InstructionType instruction;
        bool returns_value;
    };

	Function getFunction(StringView name) {
		if (equalStrings(name, "cos")) return { InstructionType::COS, true };
		else if (equalStrings(name, "sin")) return { InstructionType::SIN, true };
		else if (equalStrings(name, "kill")) return { InstructionType::KILL, false };
		else if (equalStrings(name, "random")) return { InstructionType::RAND, true };
		else if (equalStrings(name, "curve")) return { InstructionType::GRADIENT, true };
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

    enum class SystemValue {
        TIME_DELTA,
        TOTAL_TIME,
        
        NONE,
    };

	SystemValue getSystemValue(StringView name) {
		if (equalStrings(name, "time_delta")) return SystemValue::TIME_DELTA;
		if (equalStrings(name, "total_time")) return SystemValue::TOTAL_TIME;
		return SystemValue::NONE;
	}

	i32 getOutputIndex(const Emitter& emitter, StringView name) {
		for (const Variable& var : emitter.m_outputs) {
			if (equalStrings(var.name, name)) return i32(&var - emitter.m_outputs.begin());
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

	bool tryReadSemicolon() {
		skipWhitespaces();
		if (m_content.size() == 0) return false;
		if (*m_content.begin != ';') return false;

		++m_content.begin;
		return true;
	}

	StringView readOperator() {
		expectNotEOF();
		switch (*m_content.begin) {
			case '(':
			case '<':
			case '>':
			case '+':
			case '-':
			case '*':
			case '//':
			case '%': break;
			default: 
				error(m_content, "Expected an operator");
				return {};
		}
		
		StringView res;
		res.begin = m_content.begin;
		res.end = res.begin + 1;
		++m_content.begin;
		return res;
	}

	DataStream allocRegister(Emitter& emitter) {
		DataStream res;
		res.type = DataStream::REGISTER;
		res.index = emitter.m_register_allocator;
		++emitter.m_register_allocator;
		return res;
	}

	DataStream compileExpression(Emitter& emitter, OutputMemoryStream& compiled, bool is_function_arg, u32 sub) {
		expectNotEOF();

		DataStream op1;
		
		if (peekChar() == '{') {
			++m_content.begin;
			op1.type = DataStream::LITERAL;
			op1.value = readNumber();
			for (u32 i = 1; i <= sub; ++i) {
				expect(",");
				op1.value = readNumber();
			}
			for (;;) {
				StringView w = readWord();
				if (w.size() == 0) {
					error(w, "Unexpected end of file");
					break;
				}
				if (equalStrings(w, "}")) break;
				if (!equalStrings(w, ",")) error(w, "Expected ,");
				readNumber();
			}
		}
		else if (isNumeric(*m_content.begin)) {
			op1.type = DataStream::LITERAL;
			op1.value = readNumber(); 
		}
		else if (*m_content.begin == '-') {
			op1.type = DataStream::LITERAL;
			if (m_content.begin < m_content.end - 1 && isNumeric(m_content.begin[1])) {
                op1.value = readNumber(); 
            }
            else {
                StringView cname = readIdentifier();
                Constant* c = getConstant(cname);
                if (!c) error (cname, "Unknown constant");
                else {
                    op1.value = -c->value[sub];
                }
            }
		}
        else {
            StringView ident = readIdentifier();
            Constant* c = getConstant(ident);
            if (c) {
    			op1.type = DataStream::LITERAL;
                op1.value = c->value[sub];
            }
            else {
                SystemValue system_value = getSystemValue(ident);
                if (system_value != SystemValue::NONE) {
                    op1.type = DataStream::CONST;
                    op1.index = (u8)system_value;
                } 
                else {
                    i32 var_index = getVariableIndex(emitter, ident);
                    if (var_index >= 0) {
                        const Variable& var = emitter.m_vars[var_index];
                        op1.type = DataStream::CHANNEL;
                        if (peekChar() != '.') {
							op1.index = var.getOffsetSub(sub);
						}
						else {
							++m_content.begin;
							switch(peekChar()) {
								case 'r': case 'x': op1.index = var.getOffsetSub(0); break;
								case 'g': case 'y': op1.index = var.getOffsetSub(1); break;
								case 'b': case 'z': op1.index = var.getOffsetSub(2); break;
								case 'a': case 'w': op1.index = var.getOffsetSub(3); break;
								default: error(m_content, "Invalid subscript"); --m_content.begin; break;
							}
							++m_content.begin;
						}
                    }
                    else {
                        Function fn = getFunction(ident);
                        if (fn.instruction != InstructionType::END) {
                            if (!fn.returns_value) error(ident, "Function does not return a value");
                            expect("(");
                            if (fn.instruction == InstructionType::RAND) {
                                op1 = allocRegister(emitter);
                                compiled.write(fn.instruction);
                                compiled.write(op1);
                                compiled.write(readNumber());
                                expect(",");
                                compiled.write(readNumber());
                            }
                            else if (fn.instruction == InstructionType::GRADIENT) {
                                DataStream arg0 = compileExpression(emitter, compiled, true, sub);
                                op1 = allocRegister(emitter);
                                compiled.write(fn.instruction);
                                compiled.write(op1);
                                compiled.write(arg0);
                                u32 num_keyframes = 2;
                                expect(",");
                                float keys[8];
                                float values[8];
                                keys[0] = readNumber();
                                expect(",");
                                values[0] = readNumberOrSub(sub);

                                expect(",");
                                keys[1] = readNumber();
                                expect(",");
                                values[1] = readNumberOrSub(sub);

                                for (;;) {
                                    if (peekChar() != ',') break;
                                    if (num_keyframes == lengthOf(keys)) {
                                        error(m_content, "Too many arguments");
                                        break;
                                    } 
                                    expect(",");
                                    keys[num_keyframes] = readNumberOrSub(sub);
                                    expect(",");
                                    values[num_keyframes] = readNumberOrSub(sub);
                                    ++num_keyframes;
                                }
                                compiled.write(num_keyframes);
                                compiled.write(keys, sizeof(keys[0]) * num_keyframes);
                                compiled.write(values, sizeof(values[0]) * num_keyframes);
                                // TODO collapse if all values are the same
                            }
                            else {
                                DataStream arg_val = compileExpression(emitter, compiled, true, sub);
                                op1 = allocRegister(emitter);
                                compiled.write(fn.instruction);
                                compiled.write(op1);
                                compiled.write(arg_val);
                            }
                            expect(")");
                        }
                        else {
                            error(ident, "Unknown identifier");
                        }
                    }
                }
            }
		}
		skipWhitespaces();
		if (tryReadSemicolon()) {
			return op1;
		}
		if (is_function_arg && (peekChar() == ')' || peekChar() == ',')) {
			return op1;
		}

		StringView oper = readOperator();
		if (oper.size() == 0) return op1;

		DataStream op2 = compileExpression(emitter, compiled, is_function_arg, sub);

		switch (*oper.begin) {
			case '>': compiled.write(InstructionType::GT); break;
			case '<': compiled.write(InstructionType::LT); break;
			case '+': compiled.write(InstructionType::ADD); break;
			case '-': compiled.write(InstructionType::SUB); break;
			case '*': compiled.write(InstructionType::MUL); break;
			case '/': compiled.write(InstructionType::DIV); break;
			case '%': compiled.write(InstructionType::MOD); break;
		}

		DataStream res = allocRegister(emitter);
		compiled.write(res);
		compiled.write(op1);
		compiled.write(op2);
		return res;
	}

	void compileAssignment(Emitter& emitter, OutputMemoryStream& compiled, Array<Variable>& vars, u32 var_index, DataStream::Type stream_type) {
		i32 offset = vars[var_index].offset;
		switch (vars[var_index].type) {
			case Type::FLOAT: {
				expect("=");
				DataStream expr_result = compileExpression(emitter, compiled, false, 0);
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
				const bool is_float4 = vars[var_index].type == Type::FLOAT4;
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
					DataStream expr_result = compileExpression(emitter, compiled, false, 0);
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
						DataStream expr_result = compileExpression(emitter, compiled, false, i);
						DataStream dst;
						dst.type = stream_type;
						dst.index = offset + i;
						compiled.write(InstructionType::MOV);
						compiled.write(dst);
						compiled.write(expr_result);
						if (i < (is_float4 ? 3 : 2)) m_content = content;
					}
				}
				break;
			}
		}	
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
				compileAssignment(emitter, compiled, emitter.m_vars, var_index, DataStream::CHANNEL);
			}
			else {
				i32 out_index = getOutputIndex(emitter, word);
				if (out_index >= 0) {
					compileAssignment(emitter, compiled, emitter.m_outputs, out_index, DataStream::OUT);
				}
				else {
					Function fn = getFunction(word);
					if (fn.instruction != InstructionType::END) {
						if (fn.returns_value) error(word, "Unexpected function call");
                        expect("(");
						DataStream arg_val = compileExpression(emitter, compiled, true, 0);
						expect(")");
						expect(";");
						DataStream dst = allocRegister(emitter);
						compiled.write(fn.instruction);
						compiled.write(arg_val);
					}
					else {
						error(word, "Syntax error");
					}
				}
			}
		}
		compiled.write(InstructionType::END);
		
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
			else if (equalStrings(word, "fn")) compileFunction(emitter);
			else if (equalStrings(word, "emit_per_second")) emitter.m_emit_per_second = readNumber();
			else if (equalStrings(word, "init_emit_count")) emitter.m_init_emit_count = (u32)readNumber();
			else error(word, "Unknown identifier");
		}
		
	}

	void parseConst() {
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
			if (equalStrings(word, "const")) parseConst();
			else if (equalStrings(word, "emitter")) compileEmitter();
			else if (!equalStrings(word, "\n")) error(word, "Syntax error");
		}

		ParticleSystemResource::Header header;
		output.write(header);

		ParticleSystemResource::Flags flags = ParticleSystemResource::Flags::NONE;
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
			output.write(emitter.m_register_allocator);
			output.write(getCount(emitter.m_outputs));
			output.write(emitter.m_init_emit_count);
			output.write(emitter.m_emit_per_second);
			output.write(/*getCount(emitter->m_emit_inputs)*/u32(0));
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
	
	StringView m_content;
    const char* m_text_begin;
	bool m_is_error = false;
};

}