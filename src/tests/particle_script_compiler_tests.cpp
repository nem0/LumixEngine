#include "core/log.h"
#include "core/string.h"
#include "core/stream.h"
#include "core/path.h"
#include "core/hash_map.h"
#include "engine/file_system.h"
#include "renderer/editor/particle_script_compiler.h"
#include "renderer/particle_system.h"
#include "tests/common.h"

using namespace Lumix;

namespace {

// FileSystem implementation for testing that supports in-memory file storage
// Allows testing import functionality by providing file content from a hash map
struct MemoryFileSystem : FileSystem {
	HashMap<Path, const char*> m_files;

	MemoryFileSystem() : m_files(getGlobalAllocator()) {}

	bool saveContentSync(const struct Path& file, Span<const u8> content) override { return true; }
	bool getContentSync(const struct Path& file, struct OutputMemoryStream& content) override {
		auto iter = m_files.find(file);
		if (iter == m_files.end()) return false;
		content.write(iter.value(), strlen(iter.value()));
		return true;
	}
	const char* getEngineDataDir() override { return ""; }
	u64 getLastModified(StringView) override { return 0; }
	bool copyFile(StringView, StringView) override { return false; }
	bool moveFile(StringView, StringView) override { return false; }
	bool deleteFile(StringView) override { return false; }
	bool fileExists(StringView path) override { return m_files.find(Path(path)) != m_files.end(); }
	bool dirExists(StringView) override { return false; }
	FileIterator* createFileIterator(StringView) override { return nullptr; }
	bool open(StringView, os::InputFile&) override { return false; }
	bool open(StringView, os::OutputFile&) override { return false; }
	void mount(StringView, StringView) override {}
	Path getFullPath(StringView path) const override { return Path(path); }
	void processCallbacks() override {}
	bool hasWork() override { return false; }
	AsyncHandle getContent(const Path&, const ContentCallback&) override { return AsyncHandle::invalid(); }
	void cancel(AsyncHandle) override {}
};

// Test helper that exposes internal compiler functionality for testing
// Provides methods to test expression parsing, constant folding, and inspect compilation results
struct TestableCompiler : ParticleScriptCompiler {
	TestableCompiler() 
		: ParticleScriptCompiler(m_filesystem, getGlobalAllocator())
	{}
	
	void initTokenizer(StringView code) {
		m_tokenizer.m_current = code.begin;
		m_tokenizer.m_document = code;
		m_tokenizer.m_current_token = m_tokenizer.nextToken();
	}
	
	const Constant* findConstant(StringView name) const {
		for (const Constant& c : m_constants) {
			if (equalStrings(c.name, name)) return &c;
		}
		return nullptr;
	}
	
	const Emitter* getEmitter(u32 index) const {
		if (index >= (u32)m_emitters.size()) return nullptr;
		return &m_emitters[index];
	}

	MemoryFileSystem m_filesystem;
};

// Helper struct for running compiled particle scripts
struct ParticleScriptRunner {
	TestableCompiler compiler;
	OutputMemoryStream instructions;
	u32 emit_offset = 0;
	u32 output_offset = 0;
	u32 channels_count = 0;
	u32 num_vars = 0;
	u32 num_update_registers = 0;
	u32 num_emit_registers = 0;
	u32 num_output_registers = 0;
	u32 num_update_instructions = 0;
	u32 num_emit_instructions = 0;
	u32 num_output_instructions = 0;
	float channel_data[16][16] = {};
	ParticleSystem::Channel channels[16] = {};
	float system_values[16] = {};
	float registers_storage[16] = {};
	float* registers[16] = {};
	float output_memory[16] = {};

	ParticleScriptRunner() : instructions(getGlobalAllocator()) {
		for (u32 i = 0; i < 16; ++i) {
			registers[i] = &registers_storage[i];
		}
		system_values[(u8)ParticleSystemValues::TIME_DELTA] = 0.016f;
		system_values[(u8)ParticleSystemValues::TOTAL_TIME] = 0.0f;
	}

	void registerImport(const char* path, const char* src) {
		compiler.m_filesystem.m_files.insert(Path(path), src);
	}

	bool compile(const char* code) {
		OutputMemoryStream compiled(getGlobalAllocator());
		if (!compiler.compile(Path("test.pat"), code, compiled)) return false;

		InputMemoryStream blob(compiled.data(), compiled.size());
		ParticleSystemResource::Header header;
		blob.read(header);
		if (header.magic != ParticleSystemResource::Header::MAGIC) return false;

		u32 emitter_count;
		blob.read(emitter_count);
		if (emitter_count != 1) return false;

		gpu::VertexDecl decl(gpu::PrimitiveType::TRIANGLE_STRIP);
		blob.read(decl);
		blob.readString(); // material
		blob.readString(); // mesh

		u32 instructions_size;
		blob.read(instructions_size);
		instructions.resize(instructions_size);
		blob.read(instructions.getMutableData(), instructions_size);

		blob.read(emit_offset);
		blob.read(output_offset);
		blob.read(channels_count);

		blob.read(num_vars);
		blob.read(num_update_registers);
		blob.read(num_emit_registers);
		blob.read(num_output_registers);
		blob.read(num_update_instructions);
		blob.read(num_emit_instructions);
		blob.read(num_output_instructions);

		for (u32 i = 0; i < channels_count; ++i) {
			channels[i].data = channel_data[i];
		}
		return true;
	}

	void runEmit(Span<const float> emit_inputs = {}) {
		for (u32 i = 0; i < emit_inputs.length() && i < 16; ++i) {
			registers_storage[i] = emit_inputs[i];
		}
		ParticleSystem::RunningContext ctx;
		ctx.channels = channels;
		ctx.system_values = system_values;
		ctx.globals = nullptr;
		ctx.output_memory = output_memory;
		ctx.particle_idx = 0;
		ctx.register_access_idx = 0;
		ctx.is_ribbon = false;
		for (u32 i = 0; i < 16; ++i) ctx.registers[i] = registers[i];
		ctx.instructions = InputMemoryStream(
			(const u8*)instructions.data() + emit_offset,
			instructions.size() - emit_offset
		);
		ParticleSystem::run(ctx, getGlobalAllocator());
	}

	void runUpdate() {
		ParticleSystem::RunningContext ctx;
		ctx.channels = channels;
		ctx.system_values = system_values;
		ctx.globals = nullptr;
		ctx.output_memory = output_memory;
		ctx.particle_idx = 0;
		ctx.register_access_idx = 0;
		ctx.is_ribbon = false;
		for (u32 i = 0; i < 16; ++i) ctx.registers[i] = registers[i];
		ctx.instructions = InputMemoryStream(
			(const u8*)instructions.data(),
			emit_offset
		);
		ParticleSystem::run(ctx, getGlobalAllocator());
	}

	void runOutput() {
		ParticleSystem::RunningContext ctx;
		ctx.channels = channels;
		ctx.system_values = system_values;
		ctx.globals = nullptr;
		ctx.output_memory = output_memory;
		ctx.particle_idx = 0;
		ctx.register_access_idx = 0;
		ctx.is_ribbon = false;
		for (u32 i = 0; i < 16; ++i) ctx.registers[i] = registers[i];
		ctx.instructions = InputMemoryStream(
			(const u8*)instructions.data() + output_offset,
			instructions.size() - output_offset
		);
		ParticleSystem::run(ctx, getGlobalAllocator());
	}

	float getChannel(u32 channel, u32 particle = 0) const { return channel_data[channel][particle]; }
	float getOutput(u32 index) const { return output_memory[index]; }
};

bool testCompileTimeEval(const char* src, float value) {
	StaticString<256> code(R"(
		const C = )", src,  R"(;
		emitter test {
			material "particles/particle.mat"
		}
	)");

	TestableCompiler compiler;
	OutputMemoryStream compiled(getGlobalAllocator());
	if (!compiler.compile(Path("const_eval.pat"), code, compiled)) return false;

	const ParticleScriptCompiler::Constant* C = compiler.findConstant("C");
	if (!C) return false;
	if (C->type != ParticleScriptCompiler::ValueType::FLOAT) return false;

	return fabsf(C->value[0] - value) < 0.001f;
}

// Test constant declarations with literal values and expressions
bool testCompileTimeEval() {
	ASSERT_TRUE(testCompileTimeEval("2 + 3", 5.0f), "2 + 3 should be folded to 5");
	ASSERT_TRUE(testCompileTimeEval("10 - 3", 7.0f), "10 - 3 should be folded to 7");
	ASSERT_TRUE(testCompileTimeEval("4 * 5", 20.0f), "4 * 5 should be folded to 20");
	ASSERT_TRUE(testCompileTimeEval("20 / 4", 5.0f), "20 / 4 should be folded to 5");
	ASSERT_TRUE(testCompileTimeEval("10 % 3", 1.0f), "10 % 3 should be folded to 1");
	ASSERT_TRUE(testCompileTimeEval("2 + 3 * 4", 14.0f), "2 + 3 * 4 should be folded to 14");
	ASSERT_TRUE(testCompileTimeEval("(2 + 3) * 4", 20.0f), "(2 + 3) * 4 should be folded to 20");
	ASSERT_TRUE(testCompileTimeEval("10 - 2 - 3", 5.0f), "10 - 2 - 3 should be folded to 5");
	ASSERT_TRUE(testCompileTimeEval("100 / 5 / 2", 10.0f), "100 / 5 / 2 should be folded to 10");
	ASSERT_TRUE(testCompileTimeEval("-5 + 3", -2.0f), "-5 + 3 should be folded to -2");
	ASSERT_TRUE(testCompileTimeEval("-(2 + 3)", -5.0f), "-(2 + 3) should be folded to -5");
	ASSERT_TRUE(testCompileTimeEval("2 * 3 + 4 * 5", 26.0f), "2 * 3 + 4 * 5 should be folded to 26");
	ASSERT_TRUE(testCompileTimeEval("sqrt(16)", 4.0f), "sqrt(16) should be folded to 4");
	ASSERT_TRUE(testCompileTimeEval("sqrt(25)", 5.0f), "sqrt(25) should be folded to 5");
	ASSERT_TRUE(testCompileTimeEval("sqrt(4) + sqrt(9)", 5.0f), "sqrt(4) + sqrt(9) should be folded to 5");
	ASSERT_TRUE(testCompileTimeEval("sin(0)", 0.0f), "sin(0) should be folded to 0");
	ASSERT_TRUE(testCompileTimeEval("cos(0)", 1.0f), "cos(0) should be folded to 1");
	ASSERT_TRUE(testCompileTimeEval("min(3, 7)", 3.0f), "min(3, 7) should be folded to 3");
	ASSERT_TRUE(testCompileTimeEval("max(3, 7)", 7.0f), "max(3, 7) should be folded to 7");
	ASSERT_TRUE(testCompileTimeEval("min(5, 2) + max(1, 4)", 6.0f), "min(5, 2) + max(1, 4) should be folded to 6");
	ASSERT_TRUE(testCompileTimeEval("2.5 + 3.5", 6.0f), "2.5 + 3.5 should be folded to 6");
	ASSERT_TRUE(testCompileTimeEval("10.5 - 3.2", 7.3f), "10.5 - 3.2 should be folded to 7.3");
	ASSERT_TRUE(testCompileTimeEval("2.5 * 4.0", 10.0f), "2.5 * 4.0 should be folded to 10");
	ASSERT_TRUE(testCompileTimeEval("7.5 / 2.5", 3.0f), "7.5 / 2.5 should be folded to 3");
	ASSERT_TRUE(testCompileTimeEval("0.5 + 0.25 * 4.0", 1.5f), "0.5 + 0.25 * 4.0 should be folded to 1.5");
	ASSERT_TRUE(testCompileTimeEval("-3.14 + 1.14", -2.0f), "-3.14 + 1.14 should be folded to -2");
	ASSERT_TRUE(testCompileTimeEval("sqrt(max(16, 9))", 4.0f), "sqrt(max(16, 9)) should be folded to 4");
	ASSERT_TRUE(testCompileTimeEval("2 * sqrt(4) + 3", 7.0f), "2 * sqrt(4) + 3 should be folded to 7");
	ASSERT_TRUE(testCompileTimeEval("sin(cos(0))", 0.8414709848f), "sin(cos(0)) should be folded to sin(1)");
	ASSERT_TRUE(testCompileTimeEval("max(min(5, 3), 2)", 3.0f), "max(min(5, 3), 2) should be folded to 3");
	ASSERT_TRUE(testCompileTimeEval("sqrt(9) * sqrt(4)", 6.0f), "sqrt(9) * sqrt(4) should be folded to 6");
	return true;
}

// Test constant declarations using other constants
bool testCompileTimeConstUsingConst() {
	const char* code = R"(
		const C = 2;
		const A = 5;
		const B = max(A, C) + 3;
		emitter test {
			material "particles/particle.mat"
		}
	)";

	TestableCompiler compiler;
	OutputMemoryStream compiled(getGlobalAllocator());
	if (!compiler.compile(Path("const_eval_multi.pat"), code, compiled)) return false;

	const ParticleScriptCompiler::Constant* B = compiler.findConstant("B");
	if (!B) return false;
	if (B->type != ParticleScriptCompiler::ValueType::FLOAT) return false;

	return fabsf(B->value[0] - 8.f) < 0.001f;
}

// Test constants that call user-defined functions
bool testCompileTimeConstUsingUserFunction() {
	const char* code = R"(
		fn add(a, b) {
			result = a + b;
		}

		fn multiply(x, y) {
			let tmp = x;
			let tmp2 = tmp;
			result = tmp2 * y;
		}

		fn make_vec(x, y, z) {
			result = {x, y, z};
		}

		const C = add(3, 4);
		const D = multiply(C, 2);
		const V = make_vec(1, 2, 3);
		emitter test {
			material "particles/particle.mat"
		}
	)";

	TestableCompiler compiler;
	OutputMemoryStream compiled(getGlobalAllocator());
	ASSERT_TRUE(compiler.compile(Path("const_eval_user_func.pat"), code, compiled), "Compilation should succeed");

	const ParticleScriptCompiler::Constant* C = compiler.findConstant("C");
	ASSERT_TRUE(C != nullptr, "C should be present");
	ASSERT_TRUE(C->type == ParticleScriptCompiler::ValueType::FLOAT, "C should be float");
	ASSERT_TRUE(fabsf(C->value[0] - 7.f) < 0.001f, "C should be 7");

	const ParticleScriptCompiler::Constant* D = compiler.findConstant("D");
	ASSERT_TRUE(D != nullptr, "D should be present");
	ASSERT_TRUE(D->type == ParticleScriptCompiler::ValueType::FLOAT, "D should be float");
	ASSERT_TRUE(fabsf(D->value[0] - 14.f) < 0.001f, "D should be 14");

	const ParticleScriptCompiler::Constant* V = compiler.findConstant("V");
	ASSERT_TRUE(V != nullptr, "V should be present");
	ASSERT_TRUE(V->type == ParticleScriptCompiler::ValueType::FLOAT3, "V should be float3");
	ASSERT_TRUE(fabsf(V->value[0] - 1.f) < 0.001f, "V.x should be 1");
	ASSERT_TRUE(fabsf(V->value[1] - 2.f) < 0.001f, "V.y should be 2");
	ASSERT_TRUE(fabsf(V->value[2] - 3.f) < 0.001f, "V.z should be 3");

	return true;
}

// Test compile-time constant initialized with user-defined function containing if conditional
bool testCompileTimeConstWithUserFunctionIf() {
	const char* code = R"(
		fn func_with_if(x) {
			if x > 5 {
				result = x * 2;
			} else {
				result = x + 1;
			}
		}

		const C = func_with_if(10);  // 10 > 5, so 10 * 2 = 20
		const D = func_with_if(3);   // 3 > 5 is false, so 3 + 1 = 4
		emitter test {
			material "particles/particle.mat"
		}
	)";

	TestableCompiler compiler;
	OutputMemoryStream compiled(getGlobalAllocator());
	if (!compiler.compile(Path("const_eval_user_func_if.pat"), code, compiled)) return false;

	const ParticleScriptCompiler::Constant* C = compiler.findConstant("C");
	if (!C) return false;
	if (C->type != ParticleScriptCompiler::ValueType::FLOAT) return false;
	if (fabsf(C->value[0] - 20.f) >= 0.001f) return false;

	const ParticleScriptCompiler::Constant* D = compiler.findConstant("D");
	if (!D) return false;
	if (D->type != ParticleScriptCompiler::ValueType::FLOAT) return false;
	return fabsf(D->value[0] - 4.f) < 0.001f;
}

// Test compile-time constant with floatN types
bool testCompileTimeConstFloatN() {
	const char* code = R"(
		const A = 2;
		const B = {1, A, A + 1};
		const C = {2, 2, 2 * 3, 7} + {2, 3, 0, 0};
		const NEG_VEC = -{1, 2, 3};
		emitter test {
			material "particles/particle.mat"
			init_emit_count 1

			var v : float4

			fn emit() {
				v = C;
			}
		}
	)";

	ParticleScriptRunner runner;
	ASSERT_TRUE(runner.compile(code), "Compilation should succeed");
	
	const ParticleScriptCompiler::Constant* B = runner.compiler.findConstant("B");
	ASSERT_TRUE(B != nullptr, "B should be present");
	ASSERT_TRUE(B->type == ParticleScriptCompiler::ValueType::FLOAT3, "B should be float3");
	ASSERT_TRUE(fabsf(B->value[0] - 1.f) < 0.001f, "B.x should be 1");
	ASSERT_TRUE(fabsf(B->value[1] - 2.f) < 0.001f, "B.y should be 2");
	ASSERT_TRUE(fabsf(B->value[2] - 3.f) < 0.001f, "B.z should be 3");

	const ParticleScriptCompiler::Constant* C = runner.compiler.findConstant("C");
	ASSERT_TRUE(C != nullptr, "C should be present");
	ASSERT_TRUE(C->type == ParticleScriptCompiler::ValueType::FLOAT4, "C should be float4");
	ASSERT_TRUE(fabsf(C->value[0] - 4.f) < 0.001f, "C.x should be 4");
	ASSERT_TRUE(fabsf(C->value[1] - 5.f) < 0.001f, "C.y should be 5");
	ASSERT_TRUE(fabsf(C->value[2] - 6.f) < 0.001f, "C.z should be 6");
	ASSERT_TRUE(fabsf(C->value[3] - 7.f) < 0.001f, "C.w should be 7");

	const ParticleScriptCompiler::Constant* NEG_VEC = runner.compiler.findConstant("NEG_VEC");
	ASSERT_TRUE(NEG_VEC != nullptr, "NEG_VEC should be present");
	ASSERT_TRUE(NEG_VEC->type == ParticleScriptCompiler::ValueType::FLOAT3, "NEG_VEC should be float3");
	ASSERT_TRUE(fabsf(NEG_VEC->value[0] - (-1.f)) < 0.001f, "NEG_VEC.x should be -1");
	ASSERT_TRUE(fabsf(NEG_VEC->value[1] - (-2.f)) < 0.001f, "NEG_VEC.y should be -2");
	ASSERT_TRUE(fabsf(NEG_VEC->value[2] - (-3.f)) < 0.001f, "NEG_VEC.z should be -3");

	runner.runEmit();

	ASSERT_TRUE(fabsf(runner.getChannel(0) - 4.0f) < 0.001f, "v should be 4 after emit");
	ASSERT_TRUE(fabsf(runner.getChannel(1) - 5.0f) < 0.001f, "v should be 5 after emit");
	ASSERT_TRUE(fabsf(runner.getChannel(2) - 6.0f) < 0.001f, "v should be 6 after emit");
	ASSERT_TRUE(fabsf(runner.getChannel(3) - 7.0f) < 0.001f, "v should be 7 after emit");

	return true;
}

// Test emitter with input, output, and var variables
bool testCompileEmitterVariables() {
	const char* emitter_code = R"(
		emitter test {
			material "particles/particle.mat"
			init_emit_count 50

			in in_position : float3
			in in_velocity : float3
			in in_color : float3

			out i_position : float3
			out i_scale : float
			out i_color : float4
			out i_rotation : float

			var position : float3
			var velocity : float3
			var lifetime : float
			var age : float
			var color : float3
	
			fn output() {
				i_position = position;
				i_scale = 0.5 * (1 - age / lifetime);
				i_color.rgb = color.rgb;
				i_color.a = 1 - age / lifetime;
				i_rotation = age * 2;
			}
	
			fn emit() {
				position = in_position;
				velocity = in_velocity;
				color = in_color;
				lifetime = 2;
				age = 0;
			}
	
			fn update() {
				age = age + time_delta;
				position = position + velocity * time_delta;
				if age > lifetime {
					kill();
				}
			}
		}
	)";
	
	TestableCompiler compiler;
	OutputMemoryStream output(getGlobalAllocator());
	
	bool success = compiler.compile(Path("test.pat"), emitter_code, output);
	ASSERT_TRUE(success, "Emitter with input/output/var compilation should succeed");
	ASSERT_TRUE(output.size() > 0, "Output should contain compiled data");
	
	const auto* emitter = compiler.getEmitter(0);
	ASSERT_TRUE(emitter != nullptr, "Emitter should be compiled");
	
	// Verify input variables
	ASSERT_TRUE(emitter->m_inputs.size() == 3, "Should have 3 input variables");
	ASSERT_TRUE(equalStrings(emitter->m_inputs[0].name, "in_position"), "First input should be in_position");
	ASSERT_TRUE(emitter->m_inputs[0].type == ParticleScriptCompiler::ValueType::FLOAT3, "in_position should be float3");
	ASSERT_TRUE(equalStrings(emitter->m_inputs[1].name, "in_velocity"), "Second input should be in_velocity");
	ASSERT_TRUE(emitter->m_inputs[1].type == ParticleScriptCompiler::ValueType::FLOAT3, "in_velocity should be float3");
	ASSERT_TRUE(equalStrings(emitter->m_inputs[2].name, "in_color"), "Third input should be in_color");
	ASSERT_TRUE(emitter->m_inputs[2].type == ParticleScriptCompiler::ValueType::FLOAT3, "in_color should be float3");
	
	// Verify output variables
	ASSERT_TRUE(emitter->m_outputs.size() == 4, "Should have 4 output variables");
	ASSERT_TRUE(equalStrings(emitter->m_outputs[0].name, "i_position"), "First output should be i_position");
	ASSERT_TRUE(emitter->m_outputs[0].type == ParticleScriptCompiler::ValueType::FLOAT3, "i_position should be float3");
	ASSERT_TRUE(equalStrings(emitter->m_outputs[1].name, "i_scale"), "Second output should be i_scale");
	ASSERT_TRUE(emitter->m_outputs[1].type == ParticleScriptCompiler::ValueType::FLOAT, "i_scale should be float");
	ASSERT_TRUE(equalStrings(emitter->m_outputs[2].name, "i_color"), "Third output should be i_color");
	ASSERT_TRUE(emitter->m_outputs[2].type == ParticleScriptCompiler::ValueType::FLOAT4, "i_color should be float4");
	ASSERT_TRUE(equalStrings(emitter->m_outputs[3].name, "i_rotation"), "Fourth output should be i_rotation");
	ASSERT_TRUE(emitter->m_outputs[3].type == ParticleScriptCompiler::ValueType::FLOAT, "i_rotation should be float");
	
	// Verify var variables
	ASSERT_TRUE(emitter->m_vars.size() == 5, "Should have 5 var variables");
	ASSERT_TRUE(equalStrings(emitter->m_vars[0].name, "position"), "First var should be position");
	ASSERT_TRUE(emitter->m_vars[0].type == ParticleScriptCompiler::ValueType::FLOAT3, "position should be float3");
	ASSERT_TRUE(equalStrings(emitter->m_vars[1].name, "velocity"), "Second var should be velocity");
	ASSERT_TRUE(emitter->m_vars[1].type == ParticleScriptCompiler::ValueType::FLOAT3, "velocity should be float3");
	ASSERT_TRUE(equalStrings(emitter->m_vars[2].name, "lifetime"), "Third var should be lifetime");
	ASSERT_TRUE(emitter->m_vars[2].type == ParticleScriptCompiler::ValueType::FLOAT, "lifetime should be float");
	ASSERT_TRUE(equalStrings(emitter->m_vars[3].name, "age"), "Fourth var should be age");
	ASSERT_TRUE(emitter->m_vars[3].type == ParticleScriptCompiler::ValueType::FLOAT, "age should be float");
	
	return true;
}

// Test compound types (float3, float4) and member access
bool testCompileCompounds() {
	const char* emitter_code = R"(
        const SCALE = 2;

        emitter test {
            material "particles/particle.mat"
            init_emit_count 10

            out i_position : float3
            out i_color : float4

            var pos : float3
            var col : float4
            var vel : float3
            
            fn output() {
                i_position = pos;
                i_color = col;
            }
            
            fn emit() {
                pos = {1, 2, 3};
                pos.x = 5;
                pos.y = pos.x + 1;
                pos.z = pos.x + pos.y;
                
                col = {0.5, 0.5, 0.5, 1};
                col.r = 1;
                col.g = 0;
                col.b = col.r * 0.5;
                col.a = col.r - col.b;
                col.rgb = {0.2, 0.4, 0.6};
                
                vel = {1 + 2, 3 * 4, sqrt(16)};
                pos = {pos.x * SCALE, pos.y + vel.x, pos.z - vel.z};
                col = {col.r * 0.5, col.g + 0.1, col.b * 2, 1 - col.a};
                vel = {sin(0), cos(0), min(1, 2)};
                
                let tmp : float3 = {pos.x + vel.x, pos.y * 2, pos.z / 2};
                pos = tmp;
            }
            
            fn update() {
                pos.x = pos.x + time_delta;
                vel = {vel.x * 0.99, vel.y - 1, vel.z + 0.5};
                pos = pos + vel * time_delta;
            }
        }
	)";
	
	TestableCompiler compiler;
	OutputMemoryStream output(getGlobalAllocator());
	
	bool success = compiler.compile(Path("test.pat"), emitter_code, output);
	ASSERT_TRUE(success, "Compilation with compound types should succeed");
	ASSERT_TRUE(output.size() > 0, "Output should contain compiled data");
	
	const auto* emitter = compiler.getEmitter(0);
	ASSERT_TRUE(emitter != nullptr, "Emitter should be compiled");
	
	// Verify compound variable types
	ASSERT_TRUE(emitter->m_vars.size() == 3, "Should have 3 var variables");
	ASSERT_TRUE(equalStrings(emitter->m_vars[0].name, "pos"), "First var should be pos");
	ASSERT_TRUE(emitter->m_vars[0].type == ParticleScriptCompiler::ValueType::FLOAT3, "pos should be float3");
	ASSERT_TRUE(equalStrings(emitter->m_vars[1].name, "col"), "Second var should be col");
	ASSERT_TRUE(emitter->m_vars[1].type == ParticleScriptCompiler::ValueType::FLOAT4, "col should be float4");
	ASSERT_TRUE(equalStrings(emitter->m_vars[2].name, "vel"), "Third var should be vel");
	ASSERT_TRUE(emitter->m_vars[2].type == ParticleScriptCompiler::ValueType::FLOAT3, "vel should be float3");
	
	return true;
}

// Test compiling and running a particle script via ParticleSystem::run
bool testExecution() {
	const char* code = R"(
		emitter test {
			material "particles/particle.mat"
			init_emit_count 1

			out i_value : float
			out i_pos : float3
			out i_flag : float

			var value : float
			var pos : float3
			var flag : float

			fn emit() {
				value = 42;
				pos = {1, 2, 3};
				flag = 0;
				
				// Test if: value > 40, so flag should become 1
				if value > 40 {
					flag = 1;
				}
			}

			fn update() {
				value = value + 10;
				pos.x = pos.x + 1;
				
				// Test nested conditionals
				if value > 50 {
					if pos.x > 1 {
						flag = flag + 10;
					}
				}
				
				// Test less-than
				if value < 100 {
					flag = flag + 100;
				}
			}

			fn output() {
				i_value = value;
				i_pos = pos;
				i_flag = flag;
			}
		}
	)";

	ParticleScriptRunner runner;
	ASSERT_TRUE(runner.compile(code), "Compilation should succeed");

	runner.runEmit();

	// After emit: value=42, pos={1,2,3}, flag=1 (from if value > 40)
	ASSERT_TRUE(fabsf(runner.getChannel(0) - 42.0f) < 0.001f, "value should be 42 after emit");
	ASSERT_TRUE(fabsf(runner.getChannel(1) - 1.0f) < 0.001f, "pos.x should be 1 after emit");
	ASSERT_TRUE(fabsf(runner.getChannel(2) - 2.0f) < 0.001f, "pos.y should be 2 after emit");
	ASSERT_TRUE(fabsf(runner.getChannel(3) - 3.0f) < 0.001f, "pos.z should be 3 after emit");
	ASSERT_TRUE(fabsf(runner.getChannel(4) - 1.0f) < 0.001f, "flag should be 1 after emit (if true branch)");

	runner.runUpdate();

	// After update: value=52, pos.x=2, flag=111 (1 + 10 from nested if + 100 from value < 100)
	ASSERT_TRUE(fabsf(runner.getChannel(0) - 52.0f) < 0.001f, "value should be 52 after update");
	ASSERT_TRUE(fabsf(runner.getChannel(1) - 2.0f) < 0.001f, "pos.x should be 2 after update");
	ASSERT_TRUE(fabsf(runner.getChannel(4) - 111.0f) < 0.001f, "flag should be 111 after update (nested conditionals)");

	runner.runOutput();

	// Check output memory: i_value=52, i_pos={2,2,3}, i_flag=111
	ASSERT_TRUE(fabsf(runner.getOutput(0) - 52.0f) < 0.001f, "i_value should be 52");
	ASSERT_TRUE(fabsf(runner.getOutput(1) - 2.0f) < 0.001f, "i_pos.x should be 2");
	ASSERT_TRUE(fabsf(runner.getOutput(2) - 2.0f) < 0.001f, "i_pos.y should be 2");
	ASSERT_TRUE(fabsf(runner.getOutput(3) - 3.0f) < 0.001f, "i_pos.z should be 3");
	ASSERT_TRUE(fabsf(runner.getOutput(4) - 111.0f) < 0.001f, "i_flag should be 111");

	return true;
}

// Test local variables (let declarations)
bool testLocalVars() {
	const char* code = R"(
		emitter test {
			material "particles/particle.mat"
			init_emit_count 1

			out i_result : float
			out i_vec : float3

			var result : float
			var vec : float3

			fn emit() {
				// Test simple local var
				let x : float = 10;
				let y : float = x + 5;
				result = y;  // should be 15
				
				// Test local float3
				let v1 : float3 = {1, 2, 3};
				let v2 : float3 = {v1.x * 2, v1.y * 2, v1.z * 2};
				vec = v2;  // should be {2, 4, 6}
				
				// Test local without explicit type (inferred as float)
				let inferred = 100;
				result = result + inferred;  // 15 + 100 = 115
			}

			fn update() {
				// Test local var with expressions
				let scale : float = 2;
				let offset : float = 100;
				result = result * scale + offset;  // 15 * 2 + 100 = 130
				
				// Test local var reusing same name in different scope
				let tmp : float3 = {vec.x + 1, vec.y + 1, vec.z + 1};
				vec = tmp;  // should be {3, 5, 7}
			}

			fn output() {
				i_result = result;
				i_vec = vec;
			}
		}
	)";

	ParticleScriptRunner runner;
	ASSERT_TRUE(runner.compile(code), "Compilation should succeed");

	runner.runEmit();

	// After emit: result=115 (15 + 100 from inferred), vec={2,4,6}
	ASSERT_TRUE(fabsf(runner.getChannel(0) - 115.0f) < 0.001f, "result should be 115 after emit");
	ASSERT_TRUE(fabsf(runner.getChannel(1) - 2.0f) < 0.001f, "vec.x should be 2 after emit");
	ASSERT_TRUE(fabsf(runner.getChannel(2) - 4.0f) < 0.001f, "vec.y should be 4 after emit");
	ASSERT_TRUE(fabsf(runner.getChannel(3) - 6.0f) < 0.001f, "vec.z should be 6 after emit");

	runner.runUpdate();

	// After update: result=330 (115 * 2 + 100), vec={3,5,7}
	ASSERT_TRUE(fabsf(runner.getChannel(0) - 330.0f) < 0.001f, "result should be 330 after update");
	ASSERT_TRUE(fabsf(runner.getChannel(1) - 3.0f) < 0.001f, "vec.x should be 3 after update");
	ASSERT_TRUE(fabsf(runner.getChannel(2) - 5.0f) < 0.001f, "vec.y should be 5 after update");
	ASSERT_TRUE(fabsf(runner.getChannel(3) - 7.0f) < 0.001f, "vec.z should be 7 after update");

	runner.runOutput();

	// Check output memory
	ASSERT_TRUE(fabsf(runner.getOutput(0) - 330.0f) < 0.001f, "i_result should be 330");
	ASSERT_TRUE(fabsf(runner.getOutput(1) - 3.0f) < 0.001f, "i_vec.x should be 3");
	ASSERT_TRUE(fabsf(runner.getOutput(2) - 5.0f) < 0.001f, "i_vec.y should be 5");
	ASSERT_TRUE(fabsf(runner.getOutput(3) - 7.0f) < 0.001f, "i_vec.z should be 7");

	return true;
}

// Test user-defined functions
bool testUserFunctions() {
	const char* code = R"(
		fn add(a, b) {
			result = a + b;
		}

		fn multiply(x, y) {
			result = x * y;
		}

		fn scale_vec(v, s) {
			result = { 
				v.x * s,
				v.y * s,
				v.z * s
			};
		}

		fn compute(a, b, c) {
			let sum = add(a, b);
			result = multiply(sum, c);
		}

		emitter test {
			material "particles/particle.mat"
			init_emit_count 1

			out i_result : float
			out i_vec : float3

			var result : float
			var vec : float3

			fn emit() {
				// Test simple function calls
				result = add(10, 5);  // 15
				
				// Test nested function calls
				result = compute(2, 3, 4);  // (2 + 3) * 4 = 20
				
				// Test function returning float3
				vec = scale_vec({1, 2, 3}, 2);  // {2, 4, 6}
			}

			fn update() {
				// Use functions in expressions
				let a = add(result, 10);  // 20 + 10 = 30
				let b = multiply(a, 2);   // 30 * 2 = 60
				result = b;
				
				vec = scale_vec(vec, 0.5);  // {1, 2, 3}
			}

			fn output() {
				i_result = result;
				i_vec = vec;
			}
		}
	)";

	ParticleScriptRunner runner;
	ASSERT_TRUE(runner.compile(code), "Compilation should succeed");

	runner.runEmit();

	// After emit: result=20, vec={2,4,6}
	ASSERT_TRUE(fabsf(runner.getChannel(0) - 20.0f) < 0.001f, "result should be 20 after emit");
	ASSERT_TRUE(fabsf(runner.getChannel(1) - 2.0f) < 0.001f, "vec.x should be 2 after emit");
	ASSERT_TRUE(fabsf(runner.getChannel(2) - 4.0f) < 0.001f, "vec.y should be 4 after emit");
	ASSERT_TRUE(fabsf(runner.getChannel(3) - 6.0f) < 0.001f, "vec.z should be 6 after emit");

	runner.runUpdate();

	// After update: result=60, vec={1,2,3}
	ASSERT_TRUE(fabsf(runner.getChannel(0) - 60.0f) < 0.001f, "result should be 60 after update");
	ASSERT_TRUE(fabsf(runner.getChannel(1) - 1.0f) < 0.001f, "vec.x should be 1 after update");
	ASSERT_TRUE(fabsf(runner.getChannel(2) - 2.0f) < 0.001f, "vec.y should be 2 after update");
	ASSERT_TRUE(fabsf(runner.getChannel(3) - 3.0f) < 0.001f, "vec.z should be 3 after update");

	runner.runOutput();

	// Check output memory
	ASSERT_TRUE(fabsf(runner.getOutput(0) - 60.0f) < 0.001f, "i_result should be 60");
	ASSERT_TRUE(fabsf(runner.getOutput(1) - 1.0f) < 0.001f, "i_vec.x should be 1");
	ASSERT_TRUE(fabsf(runner.getOutput(2) - 2.0f) < 0.001f, "i_vec.y should be 2");
	ASSERT_TRUE(fabsf(runner.getOutput(3) - 3.0f) < 0.001f, "i_vec.z should be 3");

	return true;
}

// Test that function result type is inferred when only `result` is assigned
bool testInferResultType() {
	const char* code = R"(
		fn make_vec() {
			result = {1, 2, 3};
			result.z = 4;
		}

		emitter test {
			material "particles/particle.mat"
			init_emit_count 1

			out o : float3

			fn output() {
				o = make_vec();
			}
		}
	)";

	ParticleScriptRunner runner;
	ASSERT_TRUE(runner.compile(code), "Compilation should succeed and infer result as float3");

	runner.runEmit();
	runner.runOutput();

	ASSERT_TRUE(fabsf(runner.getOutput(0) - 1.0f) < 0.001f, "o1.x should be 1");
	ASSERT_TRUE(fabsf(runner.getOutput(1) - 2.0f) < 0.001f, "o1.y should be 2");
	ASSERT_TRUE(fabsf(runner.getOutput(2) - 4.0f) < 0.001f, "o1.z should be 4");

	return true;
}

// Test duck typing for user-defined functions - functions accessing .xyz should accept both float3 and float4
bool testUserFunctionDuckTyping() {
	const char* code = R"(
		fn get_xyz_sum(v) {
			result = v.x + v.y + v.z;
		}

		emitter test {
			material "particles/particle.mat"
			init_emit_count 1

			out i_sum3 : float
			out i_sum4 : float

			var vec3 : float3
			var vec4 : float4

			fn emit() {
				vec3 = {1, 2, 3};
				vec4 = {4, 5, 6, 7};
			}

			fn update() {
				// Both float3 and float4 should work with function accessing .xyz
				let sum3 = get_xyz_sum(vec3);  // 1 + 2 + 3 = 6
				let sum4 = get_xyz_sum(vec4);  // 4 + 5 + 6 = 15
			}

			fn output() {
				i_sum3 = get_xyz_sum(vec3);
				i_sum4 = get_xyz_sum(vec4);
			}
		}
	)";

	ParticleScriptRunner runner;
	ASSERT_TRUE(runner.compile(code), "Compilation should succeed");

	runner.runEmit();
	runner.runUpdate();
	runner.runOutput();

	// Check that duck typing works - function accepts both float3 and float4
	ASSERT_TRUE(fabsf(runner.getOutput(0) - 6.0f) < 0.001f, "i_sum3 should be 6 (1+2+3)");
	ASSERT_TRUE(fabsf(runner.getOutput(1) - 15.0f) < 0.001f, "i_sum4 should be 15 (4+5+6)");

	return true;
}

// Test that a single user-defined function can return different types depending on argument type
bool testFunctionGeneric() {
	const char* code = R"(
		fn identity(v) {
			result = v;
		}

		emitter test {
			material "particles/particle.mat"
			init_emit_count 1

			out o3 : float3
			out o4 : float4

			var v3 : float3
			var v4 : float4

			fn emit() {
				v3 = {1, 2, 3};
				v4 = {4, 5, 6, 7};
			}

			fn output() {
				o3 = identity(v3);
				o4 = identity(v4);
			}
		}
	)";

	ParticleScriptRunner runner;
	ASSERT_TRUE(runner.compile(code), "Compilation should succeed");

	runner.runEmit();
	runner.runOutput();

	ASSERT_TRUE(fabsf(runner.getOutput(0) - 1.0f) < 0.001f, "o3.x should be 1");
	ASSERT_TRUE(fabsf(runner.getOutput(1) - 2.0f) < 0.001f, "o3.y should be 2");
	ASSERT_TRUE(fabsf(runner.getOutput(2) - 3.0f) < 0.001f, "o3.z should be 3");

	ASSERT_TRUE(fabsf(runner.getOutput(3) - 4.0f) < 0.001f, "o4.x should be 4");
	ASSERT_TRUE(fabsf(runner.getOutput(4) - 5.0f) < 0.001f, "o4.y should be 5");
	ASSERT_TRUE(fabsf(runner.getOutput(5) - 6.0f) < 0.001f, "o4.z should be 6");
	ASSERT_TRUE(fabsf(runner.getOutput(6) - 7.0f) < 0.001f, "o4.w should be 7");

	return true;
}

// Test that constant folding reduces instruction count
bool testFolding() {
	// Script with constant expressions and user-defined functions that should be folded at compile time
	const char* folded_code = R"(
		fn double(x) {
			result = x * 2;
		}

		fn add_ten(x) {
			result = x + 10;
		}

		fn compute(a, b) {
			result = double(a) + add_ten(b);
		}

		emitter test {
			material "particles/particle.mat"
			init_emit_count 1

			out i_value : float
			var value : float

			fn emit() {
				value = 2 + 3 * 4;  // Should fold to 14 at compile time
				value = double(7);  // Should fold to 14
				value = compute(3, 5);  // Should fold to double(3) + add_ten(5) = 6 + 15 = 21
			}

			fn update() {
				// Multiple foldable expressions with functions
				value = (10 + 5) * 2 + sqrt(16);  // Should fold to 34
				value = add_ten(double(12));  // Should fold to add_ten(24) = 34
			}

			fn output() {
				i_value = value;
			}
		}
	)";

	// Script with pre-computed literals (baseline for comparison)
	const char* literal_code = R"(
		fn double(x) {
			result = x * 2;
		}

		fn add_ten(x) {
			result = x + 10;
		}

		fn compute(a, b) {
			result = double(a) + add_ten(b);
		}

		emitter test {
			material "particles/particle.mat"
			init_emit_count 1

			out i_value : float
			var value : float

			fn emit() {
				value = 14;  // Pre-computed value
				value = 14;  // Pre-computed value
				value = 21;  // Pre-computed value
			}

			fn update() {
				value = 34;  // Pre-computed value
				value = 34;  // Pre-computed value
			}

			fn output() {
				i_value = value;
			}
		}
	)";

	TestableCompiler folded_compiler;
	OutputMemoryStream folded_output(getGlobalAllocator());
	ASSERT_TRUE(folded_compiler.compile(Path("test.pat"), folded_code, folded_output), "Folded code compilation should succeed");

	TestableCompiler literal_compiler;
	OutputMemoryStream literal_output(getGlobalAllocator());
	ASSERT_TRUE(literal_compiler.compile(Path("test.pat"), literal_code, literal_output), "Literal code compilation should succeed");

	const auto* folded_emitter = folded_compiler.getEmitter(0);
	const auto* literal_emitter = literal_compiler.getEmitter(0);
	ASSERT_TRUE(folded_emitter != nullptr, "Folded emitter should exist");
	ASSERT_TRUE(literal_emitter != nullptr, "Literal emitter should exist");

	// Constant folding should produce same instruction count as pre-computed literals
	ASSERT_TRUE(folded_emitter->m_num_emit_instructions == literal_emitter->m_num_emit_instructions,
		"Emit instruction count should match after folding");
	ASSERT_TRUE(folded_emitter->m_num_update_instructions == literal_emitter->m_num_update_instructions,
		"Update instruction count should match after folding");
	ASSERT_TRUE(folded_emitter->m_num_output_instructions == literal_emitter->m_num_output_instructions,
		"Output instruction count should match after folding");

	return true;
}

// Test that constant folding eliminates dead branches in if conditionals
bool testIfConditionalsFolding() {
	// Script with if conditionals that have constant conditions - should be folded away
	const char* folded_code = R"(
		fn conditional_calc(x) {
			let tmp : float;
			if x > 5 {
				tmp = x * 2;
			}
			else {
				tmp = x + 1;
			}
			if tmp > 10 {
				result = tmp + 7;
			}
			else {
				result = tmp * 3;
			}
		}

		emitter test {
			material "particles/particle.mat"
			init_emit_count 1

			out i_value : float
			var value : float

			fn emit() {
				// Conditionals with constant conditions should be folded
				if 10 > 5 {
					value = 100;  // This branch should be taken (condition is true)
				}
				
				if 3 > 7 {
					value = 200;  // This branch should be eliminated (condition is false)
				}
				
				// Function with conditional and constant argument
				value = conditional_calc(10);  // 10 > 5 is true, so 10 * 2 = 20
				value = conditional_calc(3);   // 3 > 5 is false, so 3 + 1 = 4
			}

			fn update() {
				// Nested conditionals with constants
				if 5 > 2 {
					if 8 > 4 {
						value = 50;  // Both conditions true
					}
				}
				
				// False outer condition - entire block eliminated
				if 1 > 10 {
					value = 999;
				}
			}

			fn output() {
				i_value = value;
			}
		}
	)";

	// Script with pre-computed results (baseline for comparison)
	const char* literal_code = R"(
		fn conditional_calc(x) {
			let tmp : float;
			if x > 5 {
				tmp = x * 2;
			}
			else {
				tmp = x + 1;
			}
			if tmp > 10 {
				result = tmp + 7;
			}
			else {
				result = tmp * 3;
			}
		}

		emitter test {
			material "particles/particle.mat"
			init_emit_count 1

			out i_value : float
			var value : float

			fn emit() {
				// Folded: if 10 > 5 -> true, just the assignment
				value = 100;
				
				// Folded: if 3 > 7 -> false, eliminated entirely
				
				value = 20;  // conditional_calc(10) folded
				value = 4;   // conditional_calc(3) folded
			}

			fn update() {
				// Nested if folded to single assignment
				value = 50;
				
				// if 1 > 10 eliminated entirely
			}

			fn output() {
				i_value = value;
			}
		}
	)";

	ParticleScriptRunner folded_runner;
	ASSERT_TRUE(folded_runner.compile(folded_code), "Folded runner compilation should succeed");
	folded_runner.runEmit();
	folded_runner.runUpdate();
	folded_runner.runOutput();

	ParticleScriptRunner literal_runner;
	ASSERT_TRUE(literal_runner.compile(literal_code), "Literal runner compilation should succeed");
	literal_runner.runEmit();
	literal_runner.runUpdate();
	literal_runner.runOutput();

	// Check that the output value is the same
	ASSERT_TRUE(fabsf(folded_runner.getChannel(0) - literal_runner.getChannel(0)) < 0.001f,
		"Runtime output value should match after constant folding");

	const auto* folded_emitter = folded_runner.compiler.getEmitter(0);
	const auto* literal_emitter = literal_runner.compiler.getEmitter(0);

	// Constant folding of if conditionals should produce same instruction count as pre-computed code
	ASSERT_TRUE(folded_emitter->m_num_emit_instructions == literal_emitter->m_num_emit_instructions,
		"Emit instruction count should match after folding if conditionals");
	ASSERT_TRUE(folded_emitter->m_num_update_instructions == literal_emitter->m_num_update_instructions,
		"Update instruction count should match after folding if conditionals");
	ASSERT_TRUE(folded_emitter->m_num_output_instructions == literal_emitter->m_num_output_instructions,
		"Output instruction count should match after folding if conditionals");
	return true;
}

// Test syscalls (built-in functions) computed at runtime
bool testSyscalls() {
	const char* code = R"(
		emitter test {
			material "particles/particle.mat"
			init_emit_count 1

			out i_result : float
			out i_vec : float3

			var result : float
			var a : float
			var b : float
			var vec : float3

			fn emit() {
				// Initialize with runtime values (not constants)
				a = 16;
				b = 9;
				
				// Test sqrt with runtime value
				result = sqrt(a);  // sqrt(16) = 4
				
				// Test min/max with runtime values
				result = result + min(a, b);  // 4 + 9 = 13
				result = result + max(a, b);  // 13 + 16 = 29
				
				// Test sin/cos with runtime values
				vec.x = 0;
				vec.y = sin(vec.x);  // sin(0) = 0
				vec.z = cos(vec.x);  // cos(0) = 1
			}

			fn update() {
				// More runtime syscall tests
				a = 25;
				b = 4;
				
				// Chain syscalls with runtime values
				let sq = sqrt(a);  // 5
				let mn = min(sq, b);  // min(5, 4) = 4
				let mx = max(sq, b);  // max(5, 4) = 5
				result = mn + mx;  // 4 + 5 = 9
				
				// Test sqrt of expression
				vec.x = sqrt(a + b);  // sqrt(29) ~ 5.385
			}

			fn output() {
				i_result = result;
				i_vec = vec;
			}
		}
	)";

	ParticleScriptRunner runner;
	ASSERT_TRUE(runner.compile(code), "Compilation should succeed");

	runner.runEmit();

	// After emit: result=29, vec={0, 0, 1}
	ASSERT_TRUE(fabsf(runner.getChannel(0) - 29.0f) < 0.001f, "result should be 29 after emit");
	// a=16, b=9 are channels 1 and 2
	// vec is channels 3,4,5
	ASSERT_TRUE(fabsf(runner.getChannel(3) - 0.0f) < 0.001f, "vec.x should be 0 after emit");
	ASSERT_TRUE(fabsf(runner.getChannel(4) - 0.0f) < 0.001f, "vec.y should be 0 (sin(0)) after emit");
	ASSERT_TRUE(fabsf(runner.getChannel(5) - 1.0f) < 0.001f, "vec.z should be 1 (cos(0)) after emit");

	runner.runUpdate();

	// After update: result=9, vec.x=sqrt(29)~5.385
	ASSERT_TRUE(fabsf(runner.getChannel(0) - 9.0f) < 0.001f, "result should be 9 after update");
	ASSERT_TRUE(fabsf(runner.getChannel(3) - sqrtf(29.0f)) < 0.001f, "vec.x should be sqrt(29) after update");

	runner.runOutput();

	// Check output memory
	ASSERT_TRUE(fabsf(runner.getOutput(0) - 9.0f) < 0.001f, "i_result should be 9");
	ASSERT_TRUE(fabsf(runner.getOutput(1) - sqrtf(29.0f)) < 0.001f, "i_vec.x should be sqrt(29)");

	return true;
}

// Test system values (time_delta, total_time, entity_position) are accessible in particle scripts
bool testSystemValues() {
	const char* code = R"(
		emitter test {
			material "particles/particle.mat"
			init_emit_count 1

			out i_dt : float
			out i_total : float
			out i_pos : float3

			var dt : float
			var total : float
			var pos : float3
			var vel : float3

			fn emit() {
				total = total_time;
				pos.x = entity_position.x;
				pos.y = entity_position.y;
				pos.z = entity_position.z;
				vel.x = 10;
				vel.y = 20;
				vel.z = 30;
			}

			fn update() {
				// Use time_delta to update position
				dt = time_delta;
				pos.x = pos.x + vel.x * time_delta;
				pos.y = pos.y + vel.y * time_delta;
				pos.z = pos.z + vel.z * time_delta;
				total = total_time;
			}

			fn output() {
				i_dt = dt;
				i_total = total;
				i_pos = pos;
			}
		}
	)";

	ParticleScriptRunner runner;
	ASSERT_TRUE(runner.compile(code), "Compilation should succeed");

	// Set custom system values
	runner.system_values[(u8)ParticleSystemValues::TIME_DELTA] = 0.1f;
	runner.system_values[(u8)ParticleSystemValues::TOTAL_TIME] = 5.0f;
	runner.system_values[(u8)ParticleSystemValues::ENTITY_POSITION_X] = 100.0f;
	runner.system_values[(u8)ParticleSystemValues::ENTITY_POSITION_Y] = 200.0f;
	runner.system_values[(u8)ParticleSystemValues::ENTITY_POSITION_Z] = 300.0f;

	runner.runEmit();

	// Check emit captured system values
	// total=5.0, pos={100,200,300}, vel={10,20,30}
	ASSERT_TRUE(fabsf(runner.getChannel(1) - 5.0f) < 0.001f, "total should be 5.0 after emit");
	ASSERT_TRUE(fabsf(runner.getChannel(2) - 100.0f) < 0.001f, "pos.x should be 100 after emit");
	ASSERT_TRUE(fabsf(runner.getChannel(3) - 200.0f) < 0.001f, "pos.y should be 200 after emit");
	ASSERT_TRUE(fabsf(runner.getChannel(4) - 300.0f) < 0.001f, "pos.z should be 300 after emit");

	// Update system values for update phase
	runner.system_values[(u8)ParticleSystemValues::TIME_DELTA] = 0.5f;
	runner.system_values[(u8)ParticleSystemValues::TOTAL_TIME] = 5.5f;

	runner.runUpdate();

	// After update: dt=0.5, pos = pos + vel * 0.5 = {100+5, 200+10, 300+15} = {105, 210, 315}
	ASSERT_TRUE(fabsf(runner.getChannel(0) - 0.5f) < 0.001f, "dt should be 0.5 after update");
	ASSERT_TRUE(fabsf(runner.getChannel(1) - 5.5f) < 0.001f, "total should be 5.5 after update");
	ASSERT_TRUE(fabsf(runner.getChannel(2) - 105.0f) < 0.001f, "pos.x should be 105 after update");
	ASSERT_TRUE(fabsf(runner.getChannel(3) - 210.0f) < 0.001f, "pos.y should be 210 after update");
	ASSERT_TRUE(fabsf(runner.getChannel(4) - 315.0f) < 0.001f, "pos.z should be 315 after update");

	runner.runOutput();

	// Verify outputs
	ASSERT_TRUE(fabsf(runner.getOutput(0) - 0.5f) < 0.001f, "i_dt should be 0.5");
	ASSERT_TRUE(fabsf(runner.getOutput(1) - 5.5f) < 0.001f, "i_total should be 5.5");
	ASSERT_TRUE(fabsf(runner.getOutput(2) - 105.0f) < 0.001f, "i_pos.x should be 105");
	ASSERT_TRUE(fabsf(runner.getOutput(3) - 210.0f) < 0.001f, "i_pos.y should be 210");
	ASSERT_TRUE(fabsf(runner.getOutput(4) - 315.0f) < 0.001f, "i_pos.z should be 315");

	return true;
}

// Test compilation errors like missing semicolons, undefined variables, etc.
bool testCompilationErrors() {
	bool all_tests_passed = true;
	OutputMemoryStream output(getGlobalAllocator());
	auto expectCompilationFailure = [&output, &all_tests_passed](const char* error_msg, const char* src) {
		TestableCompiler compiler;
		compiler.m_suppress_logging = true;
		if (compiler.compile(Path("test.pat"), src, output)) {
			logError("TEST FAILED: Compilation should fail with ", error_msg);
			all_tests_passed = false;
		}
	};

	expectCompilationFailure("material's path is not a string", "emitter test { material 0 }");

	expectCompilationFailure("invalid assignment to constant",
		R"(
            const C = 5;
            emitter test {
                material "particles/particle.mat"
                fn emit() { C = 10; }  // cannot assign to const
            }
        )"
	);

	expectCompilationFailure("expected a statement",
		R"(
            emitter test {
                material "particles/particle.mat"
                fn emit() { 10; }
            }
        )"
	);

	expectCompilationFailure("expected a statement",
		R"(
            emitter test {
				const C = 5;
                material "particles/particle.mat"
                fn emit() { C; }
            }
        )"
	);

	expectCompilationFailure("expected a statement",
		R"(
            emitter test {
                material "particles/particle.mat"
                fn emit() { ; }
            }
        )"
	);

	expectCompilationFailure("expected a statement",
		R"(
            emitter test {
                material "particles/particle.mat"
                fn emit() { {1, 2, 3}; }
            }
        )"
	);

	expectCompilationFailure("expected a statement",
		R"(
			emitter explosion {
				material "particles/particle.mat"
			}

			emitter test {
				material "particles/particle.mat"

				fn update() {
					let v = { emit(explosion), 1 };
				}
			}
		)"
	);

	expectCompilationFailure("too many components in a compound",
		R"(
			emitter test {
				material "particles/particle.mat"

				fn update() {
					let v = { 1, 2, 3, 4, 5 };
				}
			}
		)"
	);

	expectCompilationFailure("unexpected ,",
		R"(
			emitter test {
				material "particles/particle.mat"

				fn update() {
					let v = { 1, 2, 3, 4, };
				}
			}
		)"
	);

	expectCompilationFailure("expected a value, not an emitter",
		R"(
			emitter explosion {
				material "particles/particle.mat"
			}

			emitter test {
				material "particles/particle.mat"

				fn update() {
					let v = { explosion, 1 };
				}
			}
		)"
	);

	expectCompilationFailure("expected a value, not an emitter",
		R"(
			emitter explosion {
				material "particles/particle.mat"
			}

			emitter test {
				material "particles/particle.mat"

				fn update() {
					let v = explosion;
				}
			}
		)"
	);

	expectCompilationFailure("expected a statement",
		R"(
            emitter test {
                material "particles/particle.mat"
				var v : float
                fn emit() { v; }
            }
        )"
	);

	expectCompilationFailure("invalid assignment to global",
		R"(
	        global G : float

			fn f() {
				G = 5;
			}

            emitter test {
                material "particles/particle.mat"
            }
        )"
	);

	expectCompilationFailure("access to invalid component",
		R"(
			fn f() {
				let v = {1, 2}; // inferred to float2
				v.z = 123; // .z is not in float2
				result = v;
			} 

            emitter test {
				var a : float2

				fn update() {
					a = f();
				}
	
                material "particles/particle.mat"
            }
        )"
	);

	expectCompilationFailure("type mismatch",
		R"(
			fn f() {
				result = {1, 2};
			} 

            emitter test {
				var a : float3

				fn update() {
					a = f(); // error: assign float2 to float3
				}
	
                material "particles/particle.mat"
            }
        )"
	);


	expectCompilationFailure(
		"missing semicolon",
		R"(
			emitter test {
				material "particles/particle.mat"
				var value : float

				fn emit() {
					value = 10  // missing semicolon
				}
			}
		)"
	);

	expectCompilationFailure(
		"condition must be scalar",
		R"(
			emitter test {
				material "particles/particle.mat"
				var value : float

				fn emit() {
					if {1, 2, 3} < 0 {
						value = 10;
					}
				}
			}
		)"
	);

	expectCompilationFailure(
		"undefined variable",
		R"(
			emitter test {
				material "particles/particle.mat"
				var value : float

				fn emit() {
					value = undefined_var;
				}
			}
		)"
	);

	expectCompilationFailure(
		"missing closing brace",
		R"(
			emitter test {
				material "particles/particle.mat"
				init_emit_count 1

				out i_value : float
				var value : float

				fn emit() {
					value = 10;
				// missing closing brace

				fn output() {
					i_value = value;
				}
			}
		)"
	);

	expectCompilationFailure(
		"duplicate variable names",
		R"(
			emitter test {
				material "particles/particle.mat"
				init_emit_count 1

				var value : float
				var value : float  // duplicate
			}
		)"
	);

	expectCompilationFailure("invalid type",
		R"(
			emitter test {
				material "particles/particle.mat"
				out i_value : float5  // invalid type
			}
		)"
	);

	expectCompilationFailure("missing material",
		R"(
			emitter test {
				init_emit_count 1

				out i_value : float
				var value : float

				fn emit() {
					value = 10;
				}

				fn output() {
					i_value = value;
				}
			}
		)"
	);

	expectCompilationFailure("type mismatch",
		R"(
			emitter test {
				material "particles/particle.mat"
				var value : float

				fn emit() {
					value = {1, 2, 3};  // float3 to float
				}
			}
		)"
	);

	expectCompilationFailure("invalid_func_call.pat", 
		R"(
			emitter test {
				material "particles/particle.mat"
				var value : float
				fn emit() {
					value = sqrt(10, 20);  // sqrt takes 1 arg
				}
			}
		)"
	);

	expectCompilationFailure("invalid member access",
		R"(
			emitter test {
				material "particles/particle.mat"
				var vec : float3
				fn emit() {
					vec = {1,2,3};
					let x = vec.w;  // float3 has no .w
				}
			}
		)"
	);

	expectCompilationFailure("multiple swizzles",
		R"(
			emitter test {
				material "particles/particle.mat"
				var vec : float3
				fn emit() {
					vec = {1,2,3};
					let x = vec.xy.x;
				}
			}
		)"
	);

	expectCompilationFailure("division by zero in constant", "const BAD = 1 / 0;");

	expectCompilationFailure("duplicate parameter names in function",
		R"(
			fn bad_func(a, a) {  // duplicate parameter
				result = a;
			}
		)"
	);

	expectCompilationFailure("function redefinition",
		R"(
			fn my_func(a) {
				result = a * 2;
			}

			fn my_func(b) {  // redefinition
				result = b * 3;
			}
		)"
	);

	expectCompilationFailure("wrong argument count in function call",
		R"(
			fn my_func(a, b) {
				result = a + b;
			}

			emitter test {
				material "particles/particle.mat"
				var value : float

				fn emit() {
					value = my_func(1.0);  // should be 2 args
				}
			}
		)"
	);

	expectCompilationFailure("undefined variable in function",
		R"(
			fn bad_func() {
				result = undefined_var;  // undefined
			}
		)"
	);

	expectCompilationFailure("invalid syntax in function",
		R"(
			fn bad_func(a) {
				result = a + ;  // invalid syntax
			}
		)"
	);

	expectCompilationFailure("call to undefined function",
		R"(
			emitter test {
				material "particles/particle.mat"
				var value : float

				fn emit() {
					value = nonexistent_func(1.0);  // undefined function
				}
			}
		)"
	);

	expectCompilationFailure("function assigned to variable",
		R"(
			fn my_func(a) {
				result = a * 2;
			}

			emitter test {
				material "particles/particle.mat"
				var value : float

				fn emit() {
					let f = my_func;
					value = 10;
				}
			}
		)"
	);

	expectCompilationFailure("function passed as argument",
		R"(
			fn my_func(a) {
				result = a * 2;
			}

			fn call_func(f, x) {
				result = f(x);
			}

			emitter test {
				material "particles/particle.mat"
				var value : float

				fn emit() {
					value = call_func(my_func, 5);  // invalid: passing function as argument
				}
			}
		)"
	);

	expectCompilationFailure("recursion",
		R"(
			fn factorial(n) {
				if n < 2 {
					result = 1;
				} else {
					result = n * factorial(n - 1);  // recursive call
				}
			}

			emitter test {
				material "particles/particle.mat"
				var value : float

				fn emit() {
					value = factorial(5);
				}
			}
		)"
	);

	expectCompilationFailure("semicolon after import",
		R"(
			import "utils.pat";
			emitter test {
				material "particles/particle.mat"
			}
		)"
	);

	expectCompilationFailure("semicolon after function body",
		R"(
			emitter test {
				material "particles/particle.mat"
				fn emit() {};
			}
		)"
	);

	expectCompilationFailure("semicolon after var declaration",
		R"(
			emitter test {
				material "particles/particle.mat"

				var value : float;
			}
		)"
	);

	expectCompilationFailure("conditional expression in constant initialization",
		R"(
			const A = if true then 1 else 2;
			emitter test {
				material "particles/particle.mat"
			}
		)"
	);

	expectCompilationFailure("ternary conditional in constant initialization",
		R"(
			const A = true ? 1 : 2;
			emitter test {
				material "particles/particle.mat"
			}
		)"
	);

	expectCompilationFailure("random called in constant initialization",
		R"(
			const A = random(0, 10);
			emitter test {
				material "particles/particle.mat"
			}
		)"
	);

	expectCompilationFailure("== not supported",
		R"(
			emitter test {
				material "particles/particle.mat"
				var flag : float
				fn emit() {
					if 1 == 1 {
						flag = 1;
					}
				}
			}
		)"
	);

	expectCompilationFailure("cannot assign to input variable",
		R"(
			emitter test {
				material "particles/particle.mat"
				in in_var : float

				fn emit() {
					in_var = 10.0;
				}
			}
		)"
	);

	expectCompilationFailure("cannot call kill() outside of update()",
		R"(
			emitter test {
				material "particles/particle.mat"
				in in_var : float
				var v : float

				fn emit() {
					kill();
				}
			}
		)"
	);

	expectCompilationFailure("cannot call kill() outside of update()",
		R"(
			fn f() {
				kill();
			}

			emitter test {
				material "particles/particle.mat"
				in in_var : float
				var v : float

				fn emit() {
					f();
				}
			}
		)"
	);

	expectCompilationFailure("cannot call kill() outside of update",
		R"(
			emitter test {
				material "particles/particle.mat"
				in in_var : float
				var v : float

				fn output() {
					kill();
				}
			}
		)"
	);

	expectCompilationFailure("cannot access input variables outside of emit",
		R"(
			emitter test {
				material "particles/particle.mat"
				in in_var : float
				var v : float

				fn update() {
					v = in_var;
				}
			}
		)"
	);

	expectCompilationFailure("cannot access out variables outside of output",
		R"(
			emitter test {
				material "particles/particle.mat"
				out i_var : float
				var v : float

				fn emit() {
					i_var = v;
				}
			}
		)"
	);

	expectCompilationFailure("return is not supported",
		R"(
			emitter test {
				material "particles/particle.mat"
				fn f() { return 42; }
				fn emit() {	}
			}
		)"
	);

	expectCompilationFailure("missung = after result",
		R"(
			emitter test {
				material "particles/particle.mat"
				fn f() { result 42; }
				fn emit() {	}
			}
		)"
	);

	expectCompilationFailure("type mismatch",
		R"(
			fn bad() {
				if 1 > 0 {
					result = {1, 2, 3}; // inferred as float3
				} else {
					result = {4, 5}; // can not assign float2 to float3
				}
			}

			emitter test {
				material "particles/particle.mat"
				init_emit_count 1

				var v : float3

				fn update() { v = bad(); }
			}
		)"
	);

	expectCompilationFailure("invalid subscript",
		R"(
			fn bad() {
				result = {1, 2, 3}; // inferred as float3
				result.w = 4; // invalid subscript .w
			}

			emitter test {
				material "particles/particle.mat"
				init_emit_count 1

				var v : float3

				fn update() { v = bad(); }
			}
		)"
	);

	expectCompilationFailure("invalid variable name",
		R"(
			fn bad() {
				let result : float = 12; // local variable can not be named `result`
				result = 42;
			}

			emitter test {
				material "particles/particle.mat"
				init_emit_count 1

				var v : float

				fn update() { v = bad(); }
			}
		)"
	);

	expectCompilationFailure("type mismatch",
		R"(
			fn bad() {
				result = {1, 2, 3}; // inferred as float3
				result = {4, 5};	// can not assign float2 to float3
			}

			emitter test {
				material "particles/particle.mat"
				init_emit_count 1

				var v : float3

				fn update() { v = bad(); }
			}
		)"
	);

	expectCompilationFailure("invalid subscript",
		R"(
			fn bad() {
				result.z = 4; // can not infer type of the result
			}

			emitter test {
				material "particles/particle.mat"
				init_emit_count 1

				var v : float3

				fn update() { v = bad(); }
			}
		)"
	);


	return all_tests_passed;
}

bool testBasicImport() {
	const char* main_script = R"(
		import "utils.pat"
		emitter test {
			material "particles/particle.mat"
			out value : float
			fn output() { value = double(5); }
		}
	)";

	ParticleScriptRunner runner;
	runner.registerImport("utils.pat", R"(
		const SCALE = 2.0;
		fn double(x) { result = x * SCALE; }
	)");
	ASSERT_TRUE(runner.compile(main_script), "Runner compilation should succeed");
	runner.runEmit();
	runner.runOutput();
	ASSERT_TRUE(fabsf(runner.getOutput(0) - 10.0f) < 0.001f, "Imported function should work correctly");

	return true;
}

bool testNestedImport() {
	ParticleScriptRunner runner;
	runner.registerImport("base.pat", R"(
		const BASE_VALUE = 1.0;
	)");
	runner.registerImport("utils.pat", R"(
		import "base.pat"
		fn add_base(x) { result = x + BASE_VALUE; }
	)");

	const char* main_script = R"(
		import "utils.pat"
		emitter test {
			material "particles/particle.mat"
			out value : float
			fn output() { value = add_base(3); }
		}
	)";

	ASSERT_TRUE(runner.compile(main_script), "Runner compilation should succeed");
	runner.runEmit();
	runner.runOutput();
	ASSERT_TRUE(fabsf(runner.getOutput(0) - 4.0f) < 0.001f, "Nested import should work correctly");

	return true;
}

bool testImportErrors() {
	// No files added, so import should fail
	TestableCompiler compiler;
	compiler.m_suppress_logging = true;

	const char* main_script = R"(
		import "missing.pat"
		emitter test {
			material "particles/particle.mat"
		}
	)";

	OutputMemoryStream output(getGlobalAllocator());
	bool success = compiler.compile(Path("missing_import.pat"), main_script, output);
	ASSERT_TRUE(!success, "Compilation should fail with missing import file");

	return true;
}

// Test compilation and verification of multiple emitters in a single script
bool testMultipleEmitters() {
	const char* multi_emitter_code = R"(
		emitter emitter1 {
			material "particles/particle.mat"
			init_emit_count 10

			out i_position : float3
			out i_scale : float

			var position : float3
			var scale : float

			fn output() {
				i_position = position;
				i_scale = scale;
			}

			fn emit() {
				position = {1, 2, 3};
				scale = 1.0;
			}

			fn update() {
				position.y = position.y + time_delta;
				scale = scale + 0.1;
			}
		}

		emitter emitter2 {
			material "particles/particle.mat"
			init_emit_count 20

			out i_velocity : float3
			out i_color : float4

			var velocity : float3
			var color : float4

			fn output() {
				i_velocity = velocity;
				i_color = color;
			}

			fn emit() {
				velocity = {0, 0, 0};
				color = {1, 1, 1, 1};
			}

			fn update() {
				velocity.x = velocity.x + 1.0;
				color.r = color.r - 0.01;
			}
		}
	)";

	TestableCompiler compiler;
	OutputMemoryStream output(getGlobalAllocator());

	bool success = compiler.compile(Path("multi_emitter.pat"), multi_emitter_code, output);
	ASSERT_TRUE(success, "Compilation with multiple emitters should succeed");
	ASSERT_TRUE(output.size() > 0, "Output should contain compiled data");

	// Verify first emitter
	const auto* emitter1 = compiler.getEmitter(0);
	ASSERT_TRUE(emitter1 != nullptr, "First emitter should be compiled");

	// Check emitter1 outputs
	ASSERT_TRUE(emitter1->m_outputs.size() == 2, "Emitter1 should have 2 output variables");
	ASSERT_TRUE(equalStrings(emitter1->m_outputs[0].name, "i_position"), "Emitter1 first output should be i_position");
	ASSERT_TRUE(emitter1->m_outputs[0].type == ParticleScriptCompiler::ValueType::FLOAT3, "i_position should be float3");
	ASSERT_TRUE(equalStrings(emitter1->m_outputs[1].name, "i_scale"), "Emitter1 second output should be i_scale");
	ASSERT_TRUE(emitter1->m_outputs[1].type == ParticleScriptCompiler::ValueType::FLOAT, "i_scale should be float");

	// Check emitter1 vars
	ASSERT_TRUE(emitter1->m_vars.size() == 2, "Emitter1 should have 2 var variables");
	ASSERT_TRUE(equalStrings(emitter1->m_vars[0].name, "position"), "Emitter1 first var should be position");
	ASSERT_TRUE(emitter1->m_vars[0].type == ParticleScriptCompiler::ValueType::FLOAT3, "position should be float3");
	ASSERT_TRUE(equalStrings(emitter1->m_vars[1].name, "scale"), "Emitter1 second var should be scale");
	ASSERT_TRUE(emitter1->m_vars[1].type == ParticleScriptCompiler::ValueType::FLOAT, "scale should be float");

	// Verify second emitter
	const auto* emitter2 = compiler.getEmitter(1);
	ASSERT_TRUE(emitter2 != nullptr, "Second emitter should be compiled");

	// Check emitter2 outputs
	ASSERT_TRUE(emitter2->m_outputs.size() == 2, "Emitter2 should have 2 output variables");
	ASSERT_TRUE(equalStrings(emitter2->m_outputs[0].name, "i_velocity"), "Emitter2 first output should be i_velocity");
	ASSERT_TRUE(emitter2->m_outputs[0].type == ParticleScriptCompiler::ValueType::FLOAT3, "i_velocity should be float3");
	ASSERT_TRUE(equalStrings(emitter2->m_outputs[1].name, "i_color"), "Emitter2 second output should be i_color");
	ASSERT_TRUE(emitter2->m_outputs[1].type == ParticleScriptCompiler::ValueType::FLOAT4, "i_color should be float4");

	// Check emitter2 vars
	ASSERT_TRUE(emitter2->m_vars.size() == 2, "Emitter2 should have 2 var variables");
	ASSERT_TRUE(equalStrings(emitter2->m_vars[0].name, "velocity"), "Emitter2 first var should be velocity");
	ASSERT_TRUE(emitter2->m_vars[0].type == ParticleScriptCompiler::ValueType::FLOAT3, "velocity should be float3");
	ASSERT_TRUE(equalStrings(emitter2->m_vars[1].name, "color"), "Emitter2 second var should be color");
	ASSERT_TRUE(emitter2->m_vars[1].type == ParticleScriptCompiler::ValueType::FLOAT4, "color should be float4");

	// Verify no third emitter
	const auto* emitter3 = compiler.getEmitter(2);
	ASSERT_TRUE(emitter3 == nullptr, "There should be no third emitter");

	return true;
}

// Test that unused local variables are optimized out
bool testUnusedLocalOptimization() {
	const char* code_with_unused = R"(
		emitter test {
			material "particles/particle.mat"
			var result : float

			fn emit() {
				let unused = 1;
				result = 2;
			}
		}
	)";

	const char* code_without_unused = R"(
		emitter test {
			material "particles/particle.mat"
			var result : float

			fn emit() {
				result = 2;
			}
		}
	)";

	ParticleScriptRunner runner_with;
	ASSERT_TRUE(runner_with.compile(code_with_unused), "Compilation with unused local should succeed");

	ParticleScriptRunner runner_without;
	ASSERT_TRUE(runner_without.compile(code_without_unused), "Compilation without unused local should succeed");

	ASSERT_TRUE(runner_with.num_emit_registers == runner_without.num_emit_registers, "Unused local should not increase register count");

	return true;
}

// Test unary minus operator in particle scripts
bool testUnaryMinus() {
	const char* code = R"(
		emitter test {
			material "particles/particle.mat"

			var tmp: float

			fn emit() {
				let l = -1;
				l = -l * 5;
				tmp = l * 2;
			}

			fn update() {
				tmp = -tmp * 5;
			}
		}
	)";

	ParticleScriptRunner runner;
	ASSERT_TRUE(runner.compile(code), "Compilation with unary minus should succeed");

	runner.runEmit();
	runner.runUpdate();
	ASSERT_TRUE(fabsf(runner.getChannel(0) - (-50.0f)) < 0.001f, "result should be -5 after emit");

	return true;
}

bool testSwizzling() {
	const char* code = R"(
		emitter test {
			material "particles/particle.mat"
			init_emit_count 1

			out i_vec2 : float2
			out i_vec3 : float3
			out i_vec4 : float4

			var vec4 : float4
			var vec3 : float3
			var vec2 : float2

			fn emit() {
				vec4 = {1, 2, 3, 4};
				vec3 = {5, 6, 7};
				vec2 = {8, 9};

				// Test reading single components
				let x = vec4.x;  // 1
				let y = vec4.y;  // 2
				let z = vec4.z;  // 3
				let w = vec4.w;  // 4

				// Test reading multiple components
				let xy : float2 = vec4.xy;   // {1, 2}
				let xyz : float3 = vec4.xyz; // {1, 2, 3}
				let rgb : float3 = vec4.rgb; // {1, 2, 3} (same as xyz)

				// Test reading with repeated components
				let xx : float2 = vec4.xx;   // {1, 1}
				let yyy : float3 = vec3.yyy; // {6, 6, 6}
				let zz : float2 = vec4.zz;   // {3, 3}
				let www : float4 = vec4.wwww; // {4, 4, 4, 4}

				// Test reading with mixed repeated components
				let xyx : float3 = vec4.xyx; // {1, 2, 1}
				let zwz : float3 = vec4.zwz; // {3, 4, 3}

				// Test writing to swizzles
				vec4.xy = {10, 20};         // vec4 becomes {10, 20, 3, 4}
				vec3.z = 30;                // vec3 becomes {5, 6, 30}
				vec2.y = 40;                // vec2 becomes {8, 40}

				// Test swizzle assignment with expressions
				vec4.zw = vec2;             // vec4 becomes {10, 20, 8, 40}
				vec3.xy = vec4.zw;          // vec3 becomes {8, 40, 30}
			}

			fn output() {
				i_vec2 = vec2;
				i_vec3 = vec3;
				i_vec4 = vec4;
			}
		}
	)";

	ParticleScriptRunner runner;
	ASSERT_TRUE(runner.compile(code), "Compilation with swizzling should succeed");

	runner.runEmit();
	runner.runOutput();

	// Check vec2: should be {8, 40}
	ASSERT_TRUE(fabsf(runner.getOutput(0) - 8.0f) < 0.001f, "i_vec2.x should be 8");
	ASSERT_TRUE(fabsf(runner.getOutput(1) - 40.0f) < 0.001f, "i_vec2.y should be 40");

	// Check vec3: should be {8, 40, 30}
	ASSERT_TRUE(fabsf(runner.getOutput(2) - 8.0f) < 0.001f, "i_vec3.x should be 8");
	ASSERT_TRUE(fabsf(runner.getOutput(3) - 40.0f) < 0.001f, "i_vec3.y should be 40");
	ASSERT_TRUE(fabsf(runner.getOutput(4) - 30.0f) < 0.001f, "i_vec3.z should be 30");

	// Check vec4: should be {10, 20, 8, 40}
	ASSERT_TRUE(fabsf(runner.getOutput(5) - 10.0f) < 0.001f, "i_vec4.x should be 10");
	ASSERT_TRUE(fabsf(runner.getOutput(6) - 20.0f) < 0.001f, "i_vec4.y should be 20");
	ASSERT_TRUE(fabsf(runner.getOutput(7) - 8.0f) < 0.001f, "i_vec4.z should be 8");
	ASSERT_TRUE(fabsf(runner.getOutput(8) - 40.0f) < 0.001f, "i_vec4.w should be 40");

	return true;
}

bool testEmitAfterBlock() {
	const char* code = R"(
		emitter explosion {
			material "/maps/particles/explosion.mat"
			init_emit_count 1

			in in_col : float3
		}

		emitter fireworks {
			material "/maps/particles/explosion.mat"
			emit_per_second 1

			fn update() {
				emit(explosion) {
					in_col.x = random(0, 1);
					in_col.y = random(0, 1);
					in_col.z = random(0, 1);
				};
			}
		}
	)";

	TestableCompiler compiler;
	OutputMemoryStream compiled(getGlobalAllocator());
	if (!compiler.compile(Path("emit_after_block.pat"), code, compiled)) return false;
	return true;
}

bool testNegativeEmitterSettings() {
	const char* code = R"(
		emitter test {
			material "particles/particle.mat"
			emit_move_distance -1.5
			emit_per_second -2.25
		}
	)";

	TestableCompiler compiler;
	OutputMemoryStream compiled(getGlobalAllocator());
	ASSERT_TRUE(compiler.compile(Path("negative_emitter_settings.pat"), code, compiled), "Compilation should succeed");

	const auto* emitter = compiler.getEmitter(0);
	ASSERT_TRUE(emitter != nullptr, "Emitter should be compiled");
	ASSERT_TRUE(fabsf(emitter->m_emit_move_distance - (-1.5f)) < 0.001f, "emit_move_distance should be -1.5");
	ASSERT_TRUE(fabsf(emitter->m_emit_per_second - (-2.25f)) < 0.001f, "emit_per_second should be -2.25");

	return true;
}

// Regression test for optimizer reorder/fold affecting swizzle -> channel writes
bool testOptimizerRegression() {
	const char* code = R"(
		emitter test {
			material "particles/particle.mat"

			var a : float
			var b : float

			fn emit() {
				b = 3;
				a = 8;
				a = 40;

				// These assignments exercised the optimizer bug previously
				b = a;
			}
		}
	)";

	ParticleScriptRunner runner;
	ASSERT_TRUE(runner.compile(code), "Compilation should succeed");

	runner.runEmit();

	ASSERT_TRUE(fabsf(runner.getChannel(0, 0) - 40.0f) < 0.001f, "a should be 40");
	ASSERT_TRUE(fabsf(runner.getChannel(1, 0) - 40.0f) < 0.001f, "b should be 40");

	return true;
}

bool testLogicOperators() {
	const char* code = R"(
		emitter test {
			material "particles/particle.mat"

			var tmp: float
			var	tmp2 : float

			fn emit() {
				let a = 1.0;
				let b = 0.0;
				let c = 5.0;
				let d = 3.0;
				
				// Test 'and'
				if a > 0.0 and c > d {
					tmp = 1.0;
				} else {
					tmp = 0.0;
				}
				
				// Test 'or'
				if b > 0.0 or c > d {
					tmp = tmp + 2.0;
				}
				
				// Test 'not'
				if not (b > 0.0) {
					tmp = tmp + 4.0;
				}

				tmp2 = 0;
			}

			fn update() {
				// Additional test
				if not tmp2 {
					tmp = tmp * 2.0;
				}
			}
		}
	)";

	ParticleScriptRunner runner;
	ASSERT_TRUE(runner.compile(code), "Compilation with logic operators should succeed");

	runner.runEmit();
	runner.runUpdate();
	// Expected: 1 (and true) + 2 (or true) + 4 (not true) = 7, then *2 = 14
	ASSERT_TRUE(fabsf(runner.getChannel(0) - 14.0f) < 0.001f, "result should be 14 after logic operations");

	return true;
}

bool testIfElse() {
	const char* code = R"(
		emitter test {
			material "particles/particle.mat"
			var v : float

			fn emit() { v = 1; }

			fn update() {
				if v > 0 {
					v = 2;
				}
				else {
					v = 3;
				}
			}
		}
	)";

	ParticleScriptRunner runner;
	ASSERT_TRUE(runner.compile(code), "Compilation should succeed");

	runner.runEmit();
	runner.runUpdate();

	ASSERT_TRUE(fabsf(runner.getChannel(0) - 2.0f) < 0.001f, "flag should be 2 (true branch)");

	return true;
}

bool testElseIf() {
	const char* code = R"(
		emitter test {
			material "particles/particle.mat"
			var v : float

			fn emit() { v = 0; }

			fn update() {
				if v > 1 {
					v = 10;
				} else if v > -1 {
					v = 5;
				} else {
					v = 3;
				}
			}
		}
	)";

	ParticleScriptRunner runner;
	ASSERT_TRUE(runner.compile(code), "Compilation should succeed");

	runner.runEmit();
	runner.runUpdate();

	ASSERT_TRUE(fabsf(runner.getChannel(0) - 5.0f) < 0.001f, "v should be 5 (else-if branch)");

	return true;
}

bool testInputs() {
	const char* code = R"(
		emitter test {
			material "particles/particle.mat"
			init_emit_count 1

			in in_pos : float3
			in in_vel : float3
			in in_scale : float

			out i_position : float3
			out i_velocity : float3
			out i_scale : float

			var position : float3
			var velocity : float3
			var scale : float

			fn emit() {
				position = in_pos + {1, 0, 0};
				velocity = in_vel * 2;
				scale = in_scale + 0.5;
			}

			fn update() {
				position = position + velocity * time_delta;
			}

			fn output() {
				i_position = position;
				i_velocity = velocity;
				i_scale = scale;
			}
		}
	)";

	ParticleScriptRunner runner;
	ASSERT_TRUE(runner.compile(code), "Compilation with inputs should succeed");

	// Set input values: in_pos = {10, 20, 30}, in_vel = {1, 2, 3}, in_scale = 2.0
	float inputs[] = {10, 20, 30, 1, 2, 3, 2.0f};
	runner.runEmit(Span(inputs, sizeof(inputs) / sizeof(inputs[0])));

	// After emit: position = {11, 20, 30}, velocity = {2, 4, 6}, scale = 2.5
	ASSERT_TRUE(fabsf(runner.getChannel(0) - 11.0f) < 0.001f, "position.x should be 11");
	ASSERT_TRUE(fabsf(runner.getChannel(1) - 20.0f) < 0.001f, "position.y should be 20");
	ASSERT_TRUE(fabsf(runner.getChannel(2) - 30.0f) < 0.001f, "position.z should be 30");
	ASSERT_TRUE(fabsf(runner.getChannel(3) - 2.0f) < 0.001f, "velocity.x should be 2");
	ASSERT_TRUE(fabsf(runner.getChannel(4) - 4.0f) < 0.001f, "velocity.y should be 4");
	ASSERT_TRUE(fabsf(runner.getChannel(5) - 6.0f) < 0.001f, "velocity.z should be 6");
	ASSERT_TRUE(fabsf(runner.getChannel(6) - 2.5f) < 0.001f, "scale should be 2.5");

	runner.runUpdate();

	// After update: position = {11, 20, 30} + {2, 4, 6} * 0.016 Γëê {11.032, 20.064, 30.096}
	float expected_x = 11.0f + 2.0f * 0.016f;
	float expected_y = 20.0f + 4.0f * 0.016f;
	float expected_z = 30.0f + 6.0f * 0.016f;
	ASSERT_TRUE(fabsf(runner.getChannel(0) - expected_x) < 0.001f, "position.x after update");
	ASSERT_TRUE(fabsf(runner.getChannel(1) - expected_y) < 0.001f, "position.y after update");
	ASSERT_TRUE(fabsf(runner.getChannel(2) - expected_z) < 0.001f, "position.z after update");

	runner.runOutput();

	// Check output memory
	ASSERT_TRUE(fabsf(runner.getOutput(0) - expected_x) < 0.001f, "i_position.x");
	ASSERT_TRUE(fabsf(runner.getOutput(1) - expected_y) < 0.001f, "i_position.y");
	ASSERT_TRUE(fabsf(runner.getOutput(2) - expected_z) < 0.001f, "i_position.z");
	ASSERT_TRUE(fabsf(runner.getOutput(3) - 2.0f) < 0.001f, "i_velocity.x");
	ASSERT_TRUE(fabsf(runner.getOutput(4) - 4.0f) < 0.001f, "i_velocity.y");
	ASSERT_TRUE(fabsf(runner.getOutput(5) - 6.0f) < 0.001f, "i_velocity.z");
	ASSERT_TRUE(fabsf(runner.getOutput(6) - 2.5f) < 0.001f, "i_scale");
	return true;
}

} // anonymous namespace

void runParticleScriptCompilerTests() {
	logInfo("=== Running Particle Script Compiler Tests ===");
	
	RUN_TEST(testCompileTimeEval);
	RUN_TEST(testCompileTimeConstUsingConst);
	RUN_TEST(testCompileTimeConstUsingUserFunction);
	RUN_TEST(testCompileTimeConstWithUserFunctionIf);
	RUN_TEST(testCompileTimeConstFloatN);
	RUN_TEST(testCompileEmitterVariables);
	RUN_TEST(testCompileCompounds);
	RUN_TEST(testExecution);
	RUN_TEST(testLocalVars);
	RUN_TEST(testUserFunctions);
	RUN_TEST(testInferResultType);
	RUN_TEST(testUserFunctionDuckTyping);
	RUN_TEST(testFolding);
	RUN_TEST(testIfConditionalsFolding);
	RUN_TEST(testSyscalls);
	RUN_TEST(testSystemValues);
	RUN_TEST(testBasicImport);
	RUN_TEST(testNestedImport);
	RUN_TEST(testImportErrors);
	RUN_TEST(testMultipleEmitters);
	RUN_TEST(testCompilationErrors);
	RUN_TEST(testUnusedLocalOptimization);
	RUN_TEST(testUnaryMinus);
	RUN_TEST(testSwizzling);
	RUN_TEST(testOptimizerRegression);
	RUN_TEST(testLogicOperators);
	RUN_TEST(testIfElse);
	RUN_TEST(testElseIf);
	RUN_TEST(testInputs);
	RUN_TEST(testEmitAfterBlock);
	RUN_TEST(testNegativeEmitterSettings);
	RUN_TEST(testFunctionGeneric);
}
