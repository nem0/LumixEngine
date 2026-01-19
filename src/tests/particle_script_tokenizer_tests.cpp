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
	ASSERT_EQ(Token::LEFT_PAREN, tok.type, "LEFT_PAREN");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::RIGHT_PAREN, tok.type, "RIGHT_PAREN");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::LEFT_BRACE, tok.type, "LEFT_BRACE");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::RIGHT_BRACE, tok.type, "RIGHT_BRACE");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::SEMICOLON, tok.type, "SEMICOLON");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::COLON, tok.type, "COLON");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::COMMA, tok.type, "COMMA");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::DOT, tok.type, "DOT");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::PLUS, tok.type, "PLUS");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::MINUS, tok.type, "MINUS");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::STAR, tok.type, "STAR");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::SLASH, tok.type, "SLASH");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::PERCENT, tok.type, "PERCENT");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::EQUAL, tok.type, "EQUAL");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::LT, tok.type, "LT");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::GT, tok.type, "GT");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::EOF, tok.type, "EOF");
	
	return true;
}

bool testNumbers() {
	const char* source = "123 456.789 0 1.0";
	ParticleScriptTokenizer tokenizer;
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = tokenizer.m_document.begin;
	
	using Token = ParticleScriptToken;
	
	Token tok = tokenizer.nextToken();
	ASSERT_EQ(Token::NUMBER, tok.type, "First number");
	ASSERT_TRUE(tok.value == "123", "First number value");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::NUMBER, tok.type, "Second number");
	ASSERT_TRUE(tok.value == "456.789", "Second number value");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::NUMBER, tok.type, "Third number");
	ASSERT_TRUE(tok.value == "0", "Third number value");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::NUMBER, tok.type, "Fourth number");
	ASSERT_TRUE(tok.value == "1.0", "Fourth number value");
	
	return true;
}

bool testInvalidNumber() {
	const char* source = "123.";
	ParticleScriptTokenizer tokenizer;
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = tokenizer.m_document.begin;
	
	using Token = ParticleScriptToken;
	
	Token tok = tokenizer.nextToken();
	ASSERT_EQ(Token::ERROR, tok.type, "Invalid number should produce ERROR token");
	
	return true;
}

bool testStrings() {
	const char* source = "\"hello\" \"world with spaces\" \"\"";
	ParticleScriptTokenizer tokenizer;
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = tokenizer.m_document.begin;
	
	using Token = ParticleScriptToken;
	
	Token tok = tokenizer.nextToken();
	ASSERT_EQ(Token::STRING, tok.type, "First string");
	ASSERT_TRUE(tok.value == "hello", "First string value");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::STRING, tok.type, "Second string");
	ASSERT_TRUE(tok.value == "world with spaces", "Second string value");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::STRING, tok.type, "Empty string");
	ASSERT_TRUE(tok.value == "", "Empty string value");
	
	return true;
}

bool testUnterminatedString() {
	const char* source = "\"unterminated";
	ParticleScriptTokenizer tokenizer;
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = tokenizer.m_document.begin;
	
	using Token = ParticleScriptToken;
	
	Token tok = tokenizer.nextToken();
	ASSERT_EQ(Token::ERROR, tok.type, "Unterminated string should produce ERROR token");
	
	return true;
}

bool testIdentifiers() {
	const char* source = "foo bar _test test123 _123";
	ParticleScriptTokenizer tokenizer;
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = tokenizer.m_document.begin;
	
	using Token = ParticleScriptToken;
	
	Token tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "First identifier");
	ASSERT_TRUE(tok.value == "foo", "First identifier value");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "Second identifier");
	ASSERT_TRUE(tok.value == "bar", "Second identifier value");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "Third identifier");
	ASSERT_TRUE(tok.value == "_test", "Third identifier value");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "Fourth identifier");
	ASSERT_TRUE(tok.value == "test123", "Fourth identifier value");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "Fifth identifier");
	ASSERT_TRUE(tok.value == "_123", "Fifth identifier value");
	
	return true;
}

bool testKeywords() {
	const char* source = "const global emitter fn var out in let import if else and or not";
	ParticleScriptTokenizer tokenizer;
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = tokenizer.m_document.begin;
	
	using Token = ParticleScriptToken;
	
	Token tok = tokenizer.nextToken();
	ASSERT_EQ(Token::CONST, tok.type, "const keyword");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::GLOBAL, tok.type, "global keyword");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::EMITTER, tok.type, "emitter keyword");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::FN, tok.type, "fn keyword");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::VAR, tok.type, "var keyword");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::OUT, tok.type, "out keyword");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IN, tok.type, "in keyword");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::LET, tok.type, "let keyword");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IMPORT, tok.type, "import keyword");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IF, tok.type, "if keyword");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::ELSE, tok.type, "else keyword");

	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::AND, tok.type, "and keyword");

	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::OR, tok.type, "or keyword");

	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::NOT, tok.type, "not keyword");

	return true;
}

bool testKeywordPrefixes() {
	const char* source = "cons constants emi emitters fnn global2 vary output input lets returns importing iff elses an andd orr nott";
	ParticleScriptTokenizer tokenizer;
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = tokenizer.m_document.begin;
	
	using Token = ParticleScriptToken;
	
	Token tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "cons should be identifier");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "constants should be identifier");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "emi should be identifier");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "emitters should be identifier");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "fnn should be identifier");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "global2 should be identifier");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "vary should be identifier");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "output should be identifier");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "input should be identifier");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "lets should be identifier");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "returns should be identifier");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "importing should be identifier");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "iff should be identifier");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "elses should be identifier");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "an should be identifier");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "andd should be identifier");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "orr should be identifier");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "nott should be identifier");
	
	return true;
}

bool testWhitespace() {
	const char* source = "  \t\n\r  123  \t\n  foo  ";
	ParticleScriptTokenizer tokenizer;
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = tokenizer.m_document.begin;
	
	using Token = ParticleScriptToken;
	
	Token tok = tokenizer.nextToken();
	ASSERT_EQ(Token::NUMBER, tok.type, "Should skip leading whitespace");
	ASSERT_TRUE(tok.value == "123", "Number value");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "Should skip whitespace between tokens");
	ASSERT_TRUE(tok.value == "foo", "Identifier value");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::EOF, tok.type, "Should skip trailing whitespace");
	
	return true;
}

bool testComments() {
	const char* source = "123 // this is a comment\n456";
	ParticleScriptTokenizer tokenizer;
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = tokenizer.m_document.begin;
	
	using Token = ParticleScriptToken;
	
	Token tok = tokenizer.nextToken();
	ASSERT_EQ(Token::NUMBER, tok.type, "First number");
	ASSERT_TRUE(tok.value == "123", "First number value");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::NUMBER, tok.type, "Second number after comment");
	ASSERT_TRUE(tok.value == "456", "Second number value");
	
	return true;
}

bool testCommentAtEnd() {
	const char* source = "123 // comment at end";
	ParticleScriptTokenizer tokenizer;
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = tokenizer.m_document.begin;
	
	using Token = ParticleScriptToken;
	
	Token tok = tokenizer.nextToken();
	ASSERT_EQ(Token::NUMBER, tok.type, "Number before comment");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::EOF, tok.type, "Should reach EOF after comment");
	
	return true;
}

bool testComplexExpression() {
	const char* source = "let x = 3.14 * radius;";
	ParticleScriptTokenizer tokenizer;
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = tokenizer.m_document.begin;
	
	using Token = ParticleScriptToken;
	
	Token tok = tokenizer.nextToken();
	ASSERT_EQ(Token::LET, tok.type, "let keyword");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "identifier x");
	ASSERT_TRUE(tok.value == "x", "x value");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::EQUAL, tok.type, "equal sign");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::NUMBER, tok.type, "number 3.14");
	ASSERT_TRUE(tok.value == "3.14", "3.14 value");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::STAR, tok.type, "multiplication");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "identifier radius");
	ASSERT_TRUE(tok.value == "radius", "radius value");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::SEMICOLON, tok.type, "semicolon");
	
	return true;
}

bool testFunctionDefinition() {
	const char* source = "fn update() { }";
	ParticleScriptTokenizer tokenizer;
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = tokenizer.m_document.begin;
	
	using Token = ParticleScriptToken;
	
	Token tok = tokenizer.nextToken();
	ASSERT_EQ(Token::FN, tok.type, "fn keyword");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "function name");
	ASSERT_TRUE(tok.value == "update", "update value");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::LEFT_PAREN, tok.type, "left paren");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::RIGHT_PAREN, tok.type, "right paren");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::LEFT_BRACE, tok.type, "left brace");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::RIGHT_BRACE, tok.type, "right brace");
	
	return true;
}

bool testMultipleComments() {
	const char* source = "// comment 1\n123 // comment 2\n// comment 3\n456";
	ParticleScriptTokenizer tokenizer;
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = tokenizer.m_document.begin;
	
	using Token = ParticleScriptToken;
	
	Token tok = tokenizer.nextToken();
	ASSERT_EQ(Token::NUMBER, tok.type, "First number");
	ASSERT_TRUE(tok.value == "123", "First number value");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::NUMBER, tok.type, "Second number");
	ASSERT_TRUE(tok.value == "456", "Second number value");
	
	return true;
}

bool testEmptyInput() {
	const char* source = "";
	ParticleScriptTokenizer tokenizer;
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = tokenizer.m_document.begin;
	
	using Token = ParticleScriptToken;
	
	Token tok = tokenizer.nextToken();
	ASSERT_EQ(Token::EOF, tok.type, "Empty input should return EOF");
	
	return true;
}

bool testOnlyWhitespace() {
	const char* source = "   \t\n\r   ";
	ParticleScriptTokenizer tokenizer;
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = tokenizer.m_document.begin;
	
	using Token = ParticleScriptToken;
	
	Token tok = tokenizer.nextToken();
	ASSERT_EQ(Token::EOF, tok.type, "Only whitespace should return EOF");
	
	return true;
}

bool testInvalidCharacter() {
	const char* source = "@";
	ParticleScriptTokenizer tokenizer;
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = tokenizer.m_document.begin;
	
	using Token = ParticleScriptToken;
	
	Token tok = tokenizer.nextToken();
	ASSERT_EQ(Token::ERROR, tok.type, "Invalid character should produce ERROR token");
	
	return true;
}

bool testDotAfterIdentifier() {
	const char* source = "position.x";
	ParticleScriptTokenizer tokenizer;
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = tokenizer.m_document.begin;
	
	using Token = ParticleScriptToken;
	
	Token tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "position identifier");
	ASSERT_TRUE(tok.value == "position", "position value");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::DOT, tok.type, "dot");
	
	tok = tokenizer.nextToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "x identifier");
	ASSERT_TRUE(tok.value == "x", "x value");
	
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
	RUN_TEST(testInvalidCharacter);
	RUN_TEST(testDotAfterIdentifier);
	
	logInfo("=== Test Results: ", passed_count, "/", test_count, " passed ===");
}
