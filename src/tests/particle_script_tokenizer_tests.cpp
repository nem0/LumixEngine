#include "core/log.h"
#include "core/string.h"
#include "renderer/editor/particle_script_compiler.h"
#include "tests/common.h"

using namespace Lumix;

namespace {

bool testSimpleTokens() {
	const char* source = "( ) { } ; : , . + - * / % = < >";
	ParticleScriptTokenizer tokenizer;
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = tokenizer.m_document.begin;
	
	using Token = ParticleScriptToken;
	
	Token tok = tokenizer.nextToken();
	ASSERT_EQ(Token::LEFT_PAREN, tok.type);
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::RIGHT_PAREN, tok.type);
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::LEFT_BRACE, tok.type);
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::RIGHT_BRACE, tok.type);
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::SEMICOLON, tok.type);
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::COLON, tok.type);
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::COMMA, tok.type);
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::DOT, tok.type);
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::PLUS, tok.type);
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::MINUS, tok.type);
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::STAR, tok.type);
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::SLASH, tok.type);
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::PERCENT, tok.type);
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::EQUAL, tok.type);
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::LT, tok.type);
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::GT, tok.type);
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::EOF, tok.type);
	
	return true;
}

bool testNumbers() {
	const char* source = "123 456.789 0 1.0";
	ParticleScriptTokenizer tokenizer;
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = tokenizer.m_document.begin;
	
	using Token = ParticleScriptToken;
	
	Token tok = tokenizer.nextToken();
	ASSERT_EQ(Token::NUMBER, tok.type);
	ASSERT_TRUE(tok.value == "123");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::NUMBER, tok.type);
	ASSERT_TRUE(tok.value == "456.789");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::NUMBER, tok.type);
	ASSERT_TRUE(tok.value == "0");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::NUMBER, tok.type);
	ASSERT_TRUE(tok.value == "1.0");
	
	return true;
}

bool testInvalidNumber() {
	const char* source = "123.";
	ParticleScriptTokenizer tokenizer;
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = tokenizer.m_document.begin;
	
	using Token = ParticleScriptToken;
	
	Token tok = tokenizer.nextToken();
	ASSERT_EQ(Token::ERROR, tok.type);
	
	return true;
}

bool testStrings() {
	const char* source = "\"hello\" \"world with spaces\" \"\"";
	ParticleScriptTokenizer tokenizer;
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = tokenizer.m_document.begin;
	
	using Token = ParticleScriptToken;
	
	Token tok = tokenizer.nextToken();
	ASSERT_EQ(Token::STRING, tok.type);
	ASSERT_TRUE(tok.value == "hello");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::STRING, tok.type);
	ASSERT_TRUE(tok.value == "world with spaces");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::STRING, tok.type);
	ASSERT_TRUE(tok.value == "");
	
	return true;
}

bool testUnterminatedString() {
	const char* source = "\"unterminated";
	ParticleScriptTokenizer tokenizer;
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = tokenizer.m_document.begin;
	
	using Token = ParticleScriptToken;
	
	Token tok = tokenizer.nextToken();
	ASSERT_EQ(Token::ERROR, tok.type);
	
	return true;
}

bool testIdentifiers() {
	const char* source = "foo bar _test test123 _123";
	ParticleScriptTokenizer tokenizer;
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = tokenizer.m_document.begin;
	
	using Token = ParticleScriptToken;
	
	Token tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	ASSERT_TRUE(tok.value == "foo");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	ASSERT_TRUE(tok.value == "bar");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	ASSERT_TRUE(tok.value == "_test");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	ASSERT_TRUE(tok.value == "test123");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	ASSERT_TRUE(tok.value == "_123");
	
	return true;
}

bool testKeywords() {
	const char* source = "const global emitter fn var out in let import if else and or not";
	ParticleScriptTokenizer tokenizer;
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = tokenizer.m_document.begin;
	
	using Token = ParticleScriptToken;
	
	Token tok = tokenizer.nextToken();
	ASSERT_EQ(Token::CONST, tok.type);
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::GLOBAL, tok.type);
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::EMITTER, tok.type);
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::FN, tok.type);
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::VAR, tok.type);
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::OUT, tok.type);
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IN, tok.type);
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::LET, tok.type);
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IMPORT, tok.type);
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IF, tok.type);
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::ELSE, tok.type);

	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::AND, tok.type);

	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::OR, tok.type);

	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::NOT, tok.type);

	return true;
}

bool testKeywordPrefixes() {
	const char* source = "cons constants emi emitters fnn global2 vary output input lets returns importing iff elses an andd orr nott";
	ParticleScriptTokenizer tokenizer;
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = tokenizer.m_document.begin;
	
	using Token = ParticleScriptToken;
	
	Token tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	
	return true;
}

bool testWhitespace() {
	const char* source = "  \t\n\r  123  \t\n  foo  ";
	ParticleScriptTokenizer tokenizer;
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = tokenizer.m_document.begin;
	
	using Token = ParticleScriptToken;
	
	Token tok = tokenizer.nextToken();
	ASSERT_EQ(Token::NUMBER, tok.type);
	ASSERT_TRUE(tok.value == "123");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	ASSERT_TRUE(tok.value == "foo");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::EOF, tok.type);
	
	return true;
}

bool testComments() {
	const char* source = "123 // this is a comment\n456";
	ParticleScriptTokenizer tokenizer;
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = tokenizer.m_document.begin;
	
	using Token = ParticleScriptToken;
	
	Token tok = tokenizer.nextToken();
	ASSERT_EQ(Token::NUMBER, tok.type);
	ASSERT_TRUE(tok.value == "123");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::NUMBER, tok.type);
	ASSERT_TRUE(tok.value == "456");
	
	return true;
}

bool testCommentAtEnd() {
	const char* source = "123 // comment at end";
	ParticleScriptTokenizer tokenizer;
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = tokenizer.m_document.begin;
	
	using Token = ParticleScriptToken;
	
	Token tok = tokenizer.nextToken();
	ASSERT_EQ(Token::NUMBER, tok.type);
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::EOF, tok.type);
	
	return true;
}

bool testComplexExpression() {
	const char* source = "let x = 3.14 * radius;";
	ParticleScriptTokenizer tokenizer;
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = tokenizer.m_document.begin;
	
	using Token = ParticleScriptToken;
	
	Token tok = tokenizer.nextToken();
	ASSERT_EQ(Token::LET, tok.type);
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	ASSERT_TRUE(tok.value == "x");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::EQUAL, tok.type);
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::NUMBER, tok.type);
	ASSERT_TRUE(tok.value == "3.14");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::STAR, tok.type);
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	ASSERT_TRUE(tok.value == "radius");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::SEMICOLON, tok.type);
	
	return true;
}

bool testFunctionDefinition() {
	const char* source = "fn update() { }";
	ParticleScriptTokenizer tokenizer;
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = tokenizer.m_document.begin;
	
	using Token = ParticleScriptToken;
	
	Token tok = tokenizer.nextToken();
	ASSERT_EQ(Token::FN, tok.type);
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	ASSERT_TRUE(tok.value == "update");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::LEFT_PAREN, tok.type);
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::RIGHT_PAREN, tok.type);
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::LEFT_BRACE, tok.type);
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::RIGHT_BRACE, tok.type);
	
	return true;
}

bool testMultipleComments() {
	const char* source = "// comment 1\n123 // comment 2\n// comment 3\n456";
	ParticleScriptTokenizer tokenizer;
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = tokenizer.m_document.begin;
	
	using Token = ParticleScriptToken;
	
	Token tok = tokenizer.nextToken();
	ASSERT_EQ(Token::NUMBER, tok.type);
	ASSERT_TRUE(tok.value == "123");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::NUMBER, tok.type);
	ASSERT_TRUE(tok.value == "456");
	
	return true;
}

bool testEmptyInput() {
	const char* source = "";
	ParticleScriptTokenizer tokenizer;
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = tokenizer.m_document.begin;
	
	using Token = ParticleScriptToken;
	
	Token tok = tokenizer.nextToken();
	ASSERT_EQ(Token::EOF, tok.type);
	
	return true;
}

bool testOnlyWhitespace() {
	const char* source = "   \t\n\r   ";
	ParticleScriptTokenizer tokenizer;
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = tokenizer.m_document.begin;
	
	using Token = ParticleScriptToken;
	
	Token tok = tokenizer.nextToken();
	ASSERT_EQ(Token::EOF, tok.type);
	
	return true;
}

bool testDotAfterIdentifier() {
	const char* source = "position.x";
	ParticleScriptTokenizer tokenizer;
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = tokenizer.m_document.begin;
	
	using Token = ParticleScriptToken;
	
	Token tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	ASSERT_TRUE(tok.value == "position");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::DOT, tok.type);
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	ASSERT_TRUE(tok.value == "x");
	
	return true;
}

} // anonymous namespace

void runParticleScriptTokenizerTests() {
	logInfo("=== Running Particle Script Tokenizer Tests ===");
	
	RUN_TEST(testSimpleTokens);
	RUN_TEST(testNumbers);
	RUN_TEST(testInvalidNumber);
	RUN_TEST(testStrings);
	RUN_TEST(testUnterminatedString);
	RUN_TEST(testIdentifiers);
	RUN_TEST(testKeywords);
	RUN_TEST(testKeywordPrefixes);
	RUN_TEST(testWhitespace);
	RUN_TEST(testComments);
	RUN_TEST(testCommentAtEnd);
	RUN_TEST(testComplexExpression);
	RUN_TEST(testFunctionDefinition);
	RUN_TEST(testMultipleComments);
	RUN_TEST(testEmptyInput);
	RUN_TEST(testOnlyWhitespace);
	RUN_TEST(testDotAfterIdentifier);
}
