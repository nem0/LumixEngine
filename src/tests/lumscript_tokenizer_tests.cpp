#include "core/log.h"
#include "tests/lumscript_test_common.h"

using namespace Lumix;

namespace {

bool testTokenizer() {
	LumScript::Tokenizer tokenizer;
	tokenizer.init("struct Vec3 { x : f32; } fn main() : void { }");
	using Token = LumScript::Token;

	Token tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::STRUCT, tok.type);
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	ASSERT_TRUE(tok.value == "Vec3");
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::LEFT_BRACE, tok.type);
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	ASSERT_TRUE(tok.value == "x");
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::COLON, tok.type);
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::F32, tok.type);
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::SEMICOLON, tok.type);
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::RIGHT_BRACE, tok.type);
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::FN, tok.type);
	return true;
}

bool testTokenizerCastKeyword() {
	LumScript::Tokenizer tokenizer;
	tokenizer.init("x as f32");
	using Token = LumScript::Token;

	Token tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::AS, tok.type);
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::F32, tok.type);
	return true;
}

bool testTokenizerRefKeyword() {
	LumScript::Tokenizer tokenizer;
	tokenizer.init("ref value");
	using Token = LumScript::Token;

	Token tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::REF, tok.type);
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	return true;
}

bool testTokenizerDeferKeyword() {
	LumScript::Tokenizer tokenizer;
	tokenizer.init("defer cleanup");
	using Token = LumScript::Token;

	Token tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::DEFER, tok.type);
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	return true;
}

bool testTokenizerNullableTokens() {
	LumScript::Tokenizer tokenizer;
	tokenizer.init("?i32 null");
	using Token = LumScript::Token;

	Token tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::QUESTION, tok.type);
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::I32, tok.type);
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::NULL_KW, tok.type);
	return true;
}

bool testTokenizerExtendedScalarTypes() {
	LumScript::Tokenizer tokenizer;
	tokenizer.init("i8 u8 i16 u16 i64 u64 f64");
	using Token = LumScript::Token;

	Token tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::I8, tok.type);
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::U8, tok.type);
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::I16, tok.type);
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::U16, tok.type);
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::I64, tok.type);
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::U64, tok.type);
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::F64, tok.type);
	return true;
}

bool testTokenizerLocations() {
	LumScript::Tokenizer tokenizer;
	tokenizer.init("// comment\n  fn main() : void {\n\treturn;\n}");
	using Token = LumScript::Token;

	Token tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::FN, tok.type);
	ASSERT_EQ(2, tok.line);
	ASSERT_EQ(3, tok.column);

	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	ASSERT_EQ(2, tok.line);
	ASSERT_EQ(6, tok.column);

	while (tok.type != Token::RETURN) tok = tokenizer.consumeToken();
	ASSERT_EQ(3, tok.line);
	ASSERT_EQ(2, tok.column);
	return true;
}

} // anonymous namespace

void runLumScriptTokenizerTests() {
	RUN_TEST(testTokenizer);
	RUN_TEST(testTokenizerCastKeyword);
	RUN_TEST(testTokenizerRefKeyword);
	RUN_TEST(testTokenizerDeferKeyword);
	RUN_TEST(testTokenizerNullableTokens);
	RUN_TEST(testTokenizerExtendedScalarTypes);
	RUN_TEST(testTokenizerLocations);
}
