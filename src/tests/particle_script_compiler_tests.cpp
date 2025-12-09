#include "core/log.h"
#include "core/string.h"
#include "core/stream.h"
#include "core/path.h"
#include "engine/file_system.h"
#include "renderer/editor/particle_script_compiler.h"
#include "tests/common.h"

using namespace Lumix;

namespace {

struct DummyFileSystem : FileSystem {
	bool saveContentSync(const struct Path& file, Span<const u8> content) override { return true; }
	bool getContentSync(const struct Path& file, struct OutputMemoryStream& content) override { return false; }
	const char* getEngineDataDir() override { return ""; }
	u64 getLastModified(StringView) override { return 0; }
	bool copyFile(StringView, StringView) override { return false; }
	bool moveFile(StringView, StringView) override { return false; }
	bool deleteFile(StringView) override { return false; }
	bool fileExists(StringView) override { return false; }
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

struct TestableCompiler : ParticleScriptCompiler {
	TestableCompiler() 
		: ParticleScriptCompiler(m_fs, getGlobalAllocator()) {}
	
	void initTokenizer(StringView code) {
		m_tokenizer.m_current = code.begin;
		m_tokenizer.m_document = code;
		m_tokenizer.m_current_token = m_tokenizer.nextToken();
	}
	
	Node* testExpression(CompileContext& ctx) {
		Node* result = expression(ctx, 0);
		if (result) result = collapseConstants(result, nullptr, false);
		return result;
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

	DummyFileSystem m_fs;
};

// Helper function to test constant folding optimization
bool testConstantFolding(const char* expr, float expected_value) {
	TestableCompiler compiler;
	compiler.initTokenizer(StringView(expr));
	
	ParticleScriptCompiler::CompileContext ctx(compiler);
	ParticleScriptCompiler::Node* result = compiler.testExpression(ctx);
	
	if (!result) return false;
	if (result->type != ParticleScriptCompiler::Node::LITERAL) return false;
	
	auto* literal = (ParticleScriptCompiler::LiteralNode*)result;
	const float epsilon = 0.0001f;
	return fabsf(literal->value - expected_value) < epsilon;
}

// Test constant folding for arithmetic operations and math functions
bool testConstantFoldingOperations() {
	ASSERT_TRUE(testConstantFolding("2 + 3", 5.0f), "2 + 3 should be folded to 5");
	ASSERT_TRUE(testConstantFolding("10 - 3", 7.0f), "10 - 3 should be folded to 7");
	ASSERT_TRUE(testConstantFolding("4 * 5", 20.0f), "4 * 5 should be folded to 20");
	ASSERT_TRUE(testConstantFolding("20 / 4", 5.0f), "20 / 4 should be folded to 5");
	ASSERT_TRUE(testConstantFolding("10 % 3", 1.0f), "10 % 3 should be folded to 1");
	ASSERT_TRUE(testConstantFolding("2 + 3 * 4", 14.0f), "2 + 3 * 4 should be folded to 14");
	ASSERT_TRUE(testConstantFolding("(2 + 3) * 4", 20.0f), "(2 + 3) * 4 should be folded to 20");
	ASSERT_TRUE(testConstantFolding("10 - 2 - 3", 5.0f), "10 - 2 - 3 should be folded to 5");
	ASSERT_TRUE(testConstantFolding("100 / 5 / 2", 10.0f), "100 / 5 / 2 should be folded to 10");
	ASSERT_TRUE(testConstantFolding("-5 + 3", -2.0f), "-5 + 3 should be folded to -2");
	ASSERT_TRUE(testConstantFolding("-(2 + 3)", -5.0f), "-(2 + 3) should be folded to -5");
	ASSERT_TRUE(testConstantFolding("2 * 3 + 4 * 5", 26.0f), "2 * 3 + 4 * 5 should be folded to 26");
	ASSERT_TRUE(testConstantFolding("sqrt(16)", 4.0f), "sqrt(16) should be folded to 4");
	ASSERT_TRUE(testConstantFolding("sqrt(25)", 5.0f), "sqrt(25) should be folded to 5");
	ASSERT_TRUE(testConstantFolding("sqrt(4) + sqrt(9)", 5.0f), "sqrt(4) + sqrt(9) should be folded to 5");
	ASSERT_TRUE(testConstantFolding("sin(0)", 0.0f), "sin(0) should be folded to 0");
	ASSERT_TRUE(testConstantFolding("cos(0)", 1.0f), "cos(0) should be folded to 1");
	ASSERT_TRUE(testConstantFolding("min(3, 7)", 3.0f), "min(3, 7) should be folded to 3");
	ASSERT_TRUE(testConstantFolding("max(3, 7)", 7.0f), "max(3, 7) should be folded to 7");
	ASSERT_TRUE(testConstantFolding("min(5, 2) + max(1, 4)", 6.0f), "min(5, 2) + max(1, 4) should be folded to 6");
	ASSERT_TRUE(testConstantFolding("2.5 + 3.5", 6.0f), "2.5 + 3.5 should be folded to 6");
	ASSERT_TRUE(testConstantFolding("10.5 - 3.2", 7.3f), "10.5 - 3.2 should be folded to 7.3");
	ASSERT_TRUE(testConstantFolding("2.5 * 4.0", 10.0f), "2.5 * 4.0 should be folded to 10");
	ASSERT_TRUE(testConstantFolding("7.5 / 2.5", 3.0f), "7.5 / 2.5 should be folded to 3");
	ASSERT_TRUE(testConstantFolding("0.5 + 0.25 * 4.0", 1.5f), "0.5 + 0.25 * 4.0 should be folded to 1.5");
	ASSERT_TRUE(testConstantFolding("-3.14 + 1.14", -2.0f), "-3.14 + 1.14 should be folded to -2");
	ASSERT_TRUE(testConstantFolding("sqrt(max(16, 9))", 4.0f), "sqrt(max(16, 9)) should be folded to 4");
	ASSERT_TRUE(testConstantFolding("2 * sqrt(4) + 3", 7.0f), "2 * sqrt(4) + 3 should be folded to 7");
	ASSERT_TRUE(testConstantFolding("sin(cos(0))", 0.8414709848f), "sin(cos(0)) should be folded to sin(1)");
	ASSERT_TRUE(testConstantFolding("max(min(5, 3), 2)", 3.0f), "max(min(5, 3), 2) should be folded to 3");
	ASSERT_TRUE(testConstantFolding("sqrt(9) * sqrt(4)", 6.0f), "sqrt(9) * sqrt(4) should be folded to 6");
	return true;
}

// Test constant declarations with literal values and expressions
bool testCompileConst() {
	const char* emitter_code = R"(
		const PI = 3.14159265;
		const GRAVITY = 9.8;
		const TWO_PI = PI * 2;
		const HALF_PI = PI / 2;
		const SQRT_TWO = sqrt(2);
		const COMBINED = (PI + 1) * 2;

		emitter test {
			material "particles/particle.mat"
			init_emit_count 10

			out i_position : float3
			out i_scale : float

			var position : float3
	
			fn output() {
				i_position = position;
				i_scale = TWO_PI;
			}
	
			fn emit() {
				position = {0, 0, 0};
			}
	
			fn update() {
				position.y = position.y - GRAVITY * time_delta;
				position.x = HALF_PI + SQRT_TWO + COMBINED;
			}
		}
	)";
	
	TestableCompiler compiler;
	OutputMemoryStream output(getGlobalAllocator());
	
	bool success = compiler.compile(Path("test.pat"), emitter_code, output);
	ASSERT_TRUE(success, "Compilation with constants should succeed");
	ASSERT_TRUE(output.size() > 0, "Output should contain compiled data");
	
	const auto* pi = compiler.findConstant("PI");
	ASSERT_TRUE(pi != nullptr, "PI constant should be defined");
	ASSERT_TRUE(fabsf(pi->value[0] - 3.14159265f) < 0.0001f, "PI should have correct value");
	
	const auto* gravity = compiler.findConstant("GRAVITY");
	ASSERT_TRUE(gravity != nullptr, "GRAVITY constant should be defined");
	ASSERT_TRUE(fabsf(gravity->value[0] - 9.8f) < 0.0001f, "GRAVITY should have correct value");
	
	const auto* two_pi = compiler.findConstant("TWO_PI");
	ASSERT_TRUE(two_pi != nullptr, "TWO_PI constant should be defined");
	ASSERT_TRUE(fabsf(two_pi->value[0] - (3.14159265f * 2)) < 0.0001f, "TWO_PI should be PI * 2");
	
	const auto* half_pi = compiler.findConstant("HALF_PI");
	ASSERT_TRUE(half_pi != nullptr, "HALF_PI constant should be defined");
	ASSERT_TRUE(fabsf(half_pi->value[0] - (3.14159265f / 2)) < 0.0001f, "HALF_PI should be PI / 2");
	
	const auto* sqrt_two = compiler.findConstant("SQRT_TWO");
	ASSERT_TRUE(sqrt_two != nullptr, "SQRT_TWO constant should be defined");
	ASSERT_TRUE(fabsf(sqrt_two->value[0] - sqrtf(2)) < 0.0001f, "SQRT_TWO should be sqrt(2)");
	
	const auto* combined = compiler.findConstant("COMBINED");
	ASSERT_TRUE(combined != nullptr, "COMBINED constant should be defined");
	ASSERT_TRUE(fabsf(combined->value[0] - ((3.14159265f + 1) * 2)) < 0.0001f, "COMBINED should be (PI + 1) * 2");
	
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
	
			fn output() {
				i_position = position;
				i_scale = 0.5 * (1 - age / lifetime);
				i_color.rgb = in_color;
				i_color.a = 1 - age / lifetime;
				i_rotation = age * 2;
			}
	
			fn emit() {
				position = in_position;
				velocity = in_velocity;
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
	ASSERT_TRUE(emitter->m_vars.size() == 4, "Should have 4 var variables");
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

} // anonymous namespace

void runParticleScriptCompilerTests() {
	logInfo("=== Running Particle Script Compiler Tests ===");
	
	RUN_TEST(testConstantFoldingOperations);
	RUN_TEST(testCompileConst);
	RUN_TEST(testCompileEmitterVariables);
	RUN_TEST(testCompileCompounds);
	
	logInfo("=== Test Results: ", passed_count, "/", test_count, " passed ===");
}
