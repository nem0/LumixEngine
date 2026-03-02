#include "core/log.h"
#include "core/string.h"
#include "gui_ng/ui_tokenizer.h"
#include "tests/common.h"

using namespace Lumix;

namespace {

bool testSimpleTokens() {
	const char* source = "{ } =";
	UITokenizer tokenizer;
	tokenizer.m_filename = "test";
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = source;
	tokenizer.m_current_token = tokenizer.nextToken();
	
	using Token = UITokenizer::Token;
	
	Token tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::LBRACE, tok.type);
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::RBRACE, tok.type);
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::EQUALS, tok.type);
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::EOF, tok.type);
	
	return true;
}

bool testIdentifiers() {
	const char* source = "panel button class id";
	UITokenizer tokenizer;
	tokenizer.m_filename = "test";
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = source;
	tokenizer.m_current_token = tokenizer.nextToken();
	using Token = UITokenizer::Token;
	
	Token tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	ASSERT_TRUE(tok.value == "panel");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	ASSERT_TRUE(tok.value == "button");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	ASSERT_TRUE(tok.value == "class");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	ASSERT_TRUE(tok.value == "id");
	
	return true;
}

bool testStrings() {
	const char* source = "\"hello\" \"world with spaces\" \"\" \"escaped \\\"quote\\\"\"";
	UITokenizer tokenizer;
	tokenizer.m_filename = "test";
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = source;
	tokenizer.m_current_token = tokenizer.nextToken();
	using Token = UITokenizer::Token;
	
	Token tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::STRING, tok.type);
	ASSERT_TRUE(tok.value == "hello");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::STRING, tok.type);
	ASSERT_TRUE(tok.value == "world with spaces");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::STRING, tok.type);
	ASSERT_TRUE(tok.value == "");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::STRING, tok.type);
	ASSERT_TRUE(tok.value == "escaped \\\"quote\\\"");
	
	return true;
}

bool testUnterminatedString() {
	const char* source = "\"unterminated";
	UITokenizer tokenizer;
	tokenizer.m_filename = "test";
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = source;
	tokenizer.m_current_token = tokenizer.nextToken();
	using Token = UITokenizer::Token;
	
	Token tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::ERROR, tok.type);
	
	return true;
}

bool testNumbers() {
	const char* source = "123 456.789 0 1.0";
	UITokenizer tokenizer;
	tokenizer.m_filename = "test";
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = source;
	tokenizer.m_current_token = tokenizer.nextToken();
	using Token = UITokenizer::Token;
	
	Token tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::NUMBER, tok.type);
	ASSERT_TRUE(tok.value == "123");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::NUMBER, tok.type);
	ASSERT_TRUE(tok.value == "456.789");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::NUMBER, tok.type);
	ASSERT_TRUE(tok.value == "0");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::NUMBER, tok.type);
	ASSERT_TRUE(tok.value == "1.0");
	
	return true;
}

bool testPercentages() {
	const char* source = "50% 100% 0%";
	UITokenizer tokenizer;
	tokenizer.m_filename = "test";
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = source;
	tokenizer.m_current_token = tokenizer.nextToken();
	using Token = UITokenizer::Token;
	
	Token tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::PERCENTAGE, tok.type);
	ASSERT_TRUE(tok.value == "50%");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::PERCENTAGE, tok.type);
	ASSERT_TRUE(tok.value == "100%");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::PERCENTAGE, tok.type);
	ASSERT_TRUE(tok.value == "0%");
	
	return true;
}

bool testEm() {
	const char* source = "1.5em 2em 10em";
	UITokenizer tokenizer;
	tokenizer.m_filename = "test";
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = source;
	tokenizer.m_current_token = tokenizer.nextToken();
	using Token = UITokenizer::Token;
	
	Token tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::EM, tok.type);
	ASSERT_TRUE(tok.value == "1.5em");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::EM, tok.type);
	ASSERT_TRUE(tok.value == "2em");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::EM, tok.type);
	ASSERT_TRUE(tok.value == "10em");
	
	return true;
}

bool testEmFollowedByIdentifier() {
	const char* source = "emsome";
	UITokenizer tokenizer;
	tokenizer.m_filename = "test";
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = source;
	tokenizer.m_current_token = tokenizer.nextToken();
	using Token = UITokenizer::Token;
	
	Token tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	ASSERT_TRUE(tok.value == "emsome");
	
	return true;
}

bool testComments() {
	const char* source = "panel // this is a comment\nbutton /* block comment */ text";
	UITokenizer tokenizer;
	tokenizer.m_filename = "test";
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = source;
	tokenizer.m_current_token = tokenizer.nextToken();
	using Token = UITokenizer::Token;
	
	Token tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	ASSERT_TRUE(tok.value == "panel");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	ASSERT_TRUE(tok.value == "button");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	ASSERT_TRUE(tok.value == "text");
	
	return true;
}

bool testUnterminatedBlockComment() {
	const char* source = "/* unterminated comment";
	UITokenizer tokenizer;
	tokenizer.m_filename = "test";
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = source;
	tokenizer.m_current_token = tokenizer.nextToken();
	using Token = UITokenizer::Token;
	
	Token tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::EOF, tok.type);
	
	return true;
}

bool testWhitespace() {
	const char* source = "  \t  \n  panel  \t  button  \n  ";
	UITokenizer tokenizer;
	tokenizer.m_filename = "test";
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = source;
	tokenizer.m_current_token = tokenizer.nextToken();
	using Token = UITokenizer::Token;
	
	Token tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	ASSERT_TRUE(tok.value == "panel");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	ASSERT_TRUE(tok.value == "button");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::EOF, tok.type);
	
	return true;
}

bool testComplexUI() {
	const char* source = "[panel id=\"main\" width=800 height=600] { }";
	UITokenizer tokenizer;
	tokenizer.m_filename = "test";
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = source;
	tokenizer.m_current_token = tokenizer.nextToken();
	using Token = UITokenizer::Token;

	Token tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::LBRACKET, tok.type);

	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	ASSERT_TRUE(tok.value == "panel");

	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	ASSERT_TRUE(tok.value == "id");

	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::EQUALS, tok.type);

	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::STRING, tok.type);
	ASSERT_TRUE(tok.value == "main");

	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	ASSERT_TRUE(tok.value == "width");

	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::EQUALS, tok.type);

	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::NUMBER, tok.type);
	ASSERT_TRUE(tok.value == "800");

	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	ASSERT_TRUE(tok.value == "height");

	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::EQUALS, tok.type);

	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::NUMBER, tok.type);
	ASSERT_TRUE(tok.value == "600");

	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::RBRACKET, tok.type);

	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::LBRACE, tok.type);

	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::RBRACE, tok.type);

	return true;
}

bool testEmptyInput() {
	const char* source = "";
	UITokenizer tokenizer;
	tokenizer.m_filename = "test";
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = source;
	tokenizer.m_current_token = tokenizer.nextToken();
	using Token = UITokenizer::Token;
	
	Token tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::EOF, tok.type);
	
	return true;
}

bool testOnlyWhitespace() {
	const char* source = "   \t   \n   \r   ";
	UITokenizer tokenizer;
	tokenizer.m_filename = "test";
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = source;
	tokenizer.m_current_token = tokenizer.nextToken();
	using Token = UITokenizer::Token;
	
	Token tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::EOF, tok.type);
	
	return true;
}

bool testMultilineStrings() {
	const char* source = "\"line1\nline2\" \"  indented\n    more\"";
	UITokenizer tokenizer;
	tokenizer.m_filename = "test";
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = source;
	tokenizer.m_current_token = tokenizer.nextToken();
	using Token = UITokenizer::Token;
	
	Token tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::STRING, tok.type);
	ASSERT_TRUE(tok.value == "line1\nline2");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::STRING, tok.type);
	ASSERT_TRUE(tok.value == "  indented\n    more");
	
	return true;
}

bool testEscapedCharacters() {
	const char* source = "\"quote \\\" here\" \"newline \\n tab \\t backslash \\\\\"";
	UITokenizer tokenizer;
	tokenizer.m_filename = "test";
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = source;
	tokenizer.m_current_token = tokenizer.nextToken();
	using Token = UITokenizer::Token;
	
	Token tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::STRING, tok.type);
	ASSERT_TRUE(tok.value == "quote \\\" here");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::STRING, tok.type);
	ASSERT_TRUE(tok.value == "newline \\n tab \\t backslash \\\\");
	
	return true;
}

bool testIdentifiersWithSpecialChars() {
	const char* source = "panel_name button-class _private attr_123";
	UITokenizer tokenizer;
	tokenizer.m_filename = "test";
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = source;
	tokenizer.m_current_token = tokenizer.nextToken();
	using Token = UITokenizer::Token;
	
	Token tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	ASSERT_TRUE(tok.value == "panel_name");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	ASSERT_TRUE(tok.value == "button-class");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	ASSERT_TRUE(tok.value == "_private");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	ASSERT_TRUE(tok.value == "attr_123");
	
	return true;
}

bool testWhitespaceAroundEquals() {
	const char* source = "width = 800 height=600";
	UITokenizer tokenizer;
	tokenizer.m_filename = "test";
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = source;
	tokenizer.m_current_token = tokenizer.nextToken();
	using Token = UITokenizer::Token;
	
	Token tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	ASSERT_TRUE(tok.value == "width");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::EQUALS, tok.type);
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::NUMBER, tok.type);
	ASSERT_TRUE(tok.value == "800");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	ASSERT_TRUE(tok.value == "height");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::EQUALS, tok.type);
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::NUMBER, tok.type);
	ASSERT_TRUE(tok.value == "600");

	return true;
}

bool testBlockCommentInMarkup() {
	const char* source = "panel /* this is a block comment */ { button class=\"primary\" { \"Start\" } }";
	UITokenizer tokenizer;
	tokenizer.m_filename = "test";
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = source;
	tokenizer.m_current_token = tokenizer.nextToken();
	using Token = UITokenizer::Token;
	
	Token tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	ASSERT_TRUE(tok.value == "panel");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::LBRACE, tok.type);
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	ASSERT_TRUE(tok.value == "button");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	ASSERT_TRUE(tok.value == "class");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::EQUALS, tok.type);
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::STRING, tok.type);
	ASSERT_TRUE(tok.value == "primary");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::LBRACE, tok.type);
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::STRING, tok.type);
	ASSERT_TRUE(tok.value == "Start");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::RBRACE, tok.type);
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::RBRACE, tok.type);
	
	return true;
}

bool testPercentageInAttributes() {
	const char* source = "[panel width=100% height=50%] { }";
	UITokenizer tokenizer;
	tokenizer.m_filename = "test";
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = source;
	tokenizer.m_current_token = tokenizer.nextToken();
	using Token = UITokenizer::Token;

	Token tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::LBRACKET, tok.type);

	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);

	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);

	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::EQUALS, tok.type);

	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::PERCENTAGE, tok.type);
	ASSERT_TRUE(tok.value == "100%");

	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);

	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::EQUALS, tok.type);

	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::PERCENTAGE, tok.type);
	ASSERT_TRUE(tok.value == "50%");

	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::RBRACKET, tok.type);

	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::LBRACE, tok.type);

	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::RBRACE, tok.type);

	return true;
}

bool testQuotedAttributeValues() {
	const char* source = "button [class=\"primary\" id=\"start\" width=\"12em\"]";
	UITokenizer tokenizer;
	tokenizer.m_filename = "test";
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = source;
	tokenizer.m_current_token = tokenizer.nextToken();
	using Token = UITokenizer::Token;
	
	Token tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::LBRACKET, tok.type);
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::EQUALS, tok.type);
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::STRING, tok.type);
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::EQUALS, tok.type);
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::STRING, tok.type);
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::EQUALS, tok.type);
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::STRING, tok.type);
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::RBRACKET, tok.type);
	
	return true;
}

bool testInvalidEscapeSequence() {
	const char* source = "\"invalid \\z escape\"";
	UITokenizer tokenizer;
	tokenizer.m_filename = "test";
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = source;
	tokenizer.m_current_token = tokenizer.nextToken();
	using Token = UITokenizer::Token;
	
	Token tok = tokenizer.consumeToken();
	// Actually, the tokenizer accepts any escape sequence, so this should be a valid string
	ASSERT_EQ(Token::STRING, tok.type);
	ASSERT_TRUE(tok.value == "invalid \\z escape");
	
	return true;
}

bool testUnterminatedLineComment() {
	const char* source = "// unterminated line comment";
	UITokenizer tokenizer;
	tokenizer.m_filename = "test";
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = source;
	tokenizer.m_current_token = tokenizer.nextToken();
	using Token = UITokenizer::Token;
	
	Token tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::EOF, tok.type);
	
	return true;
}

bool testInvalidStringEscape() {
	const char* source = "\"string with \\x invalid hex\"";
	UITokenizer tokenizer;
	tokenizer.m_filename = "test";
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = source;
	tokenizer.m_current_token = tokenizer.nextToken();
	using Token = UITokenizer::Token;
	
	Token tok = tokenizer.consumeToken();
	// The tokenizer accepts any escape sequence
	ASSERT_EQ(Token::STRING, tok.type);
	ASSERT_TRUE(tok.value == "string with \\x invalid hex");
	
	return true;
}

bool testNumberStartingWithZero() {
	const char* source = "0123";
	UITokenizer tokenizer;
	tokenizer.m_filename = "test";
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = source;
	tokenizer.m_current_token = tokenizer.nextToken();
	using Token = UITokenizer::Token;
	
	Token tok = tokenizer.consumeToken();
	// This might be valid or invalid depending on implementation, but let's test it
	// For now, assuming leading zeros are allowed
	ASSERT_EQ(Token::NUMBER, tok.type);
	ASSERT_TRUE(tok.value == "0123");
	
	return true;
}

bool testStringWithBackslashAtEnd() {
	const char* source = "\"string with backslash\\";
	UITokenizer tokenizer;
	tokenizer.m_filename = "test";
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = source;
	tokenizer.m_current_token = tokenizer.nextToken();
	using Token = UITokenizer::Token;
	
	Token tok = tokenizer.consumeToken();
	// Backslash at end of string should cause ERROR
	ASSERT_EQ(Token::ERROR, tok.type);
	
	return true;
}

bool testStyleTokens() {
	const char* source = ".primary { color: red; width: 100%; } $main_menu {}";
	UITokenizer tokenizer;
	tokenizer.m_filename = "test";
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = source;
	tokenizer.m_current_token = tokenizer.nextToken();
	using Token = UITokenizer::Token;
	
	Token tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::DOT, tok.type);
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	ASSERT_TRUE(tok.value == "primary");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::LBRACE, tok.type);
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	ASSERT_TRUE(tok.value == "color");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::COLON, tok.type);
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	ASSERT_TRUE(tok.value == "red");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::SEMICOLON, tok.type);
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	ASSERT_TRUE(tok.value == "width");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::COLON, tok.type);
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::PERCENTAGE, tok.type);
	ASSERT_TRUE(tok.value == "100%");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::SEMICOLON, tok.type);
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::RBRACE, tok.type);
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::DOLLAR, tok.type);
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type);
	ASSERT_TRUE(tok.value == "main_menu");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::LBRACE, tok.type);
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::RBRACE, tok.type);
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::EOF, tok.type);
	
	return true;
}

bool testColors() {
const char* source = "#FF0000 #abc #123456 #def";
	UITokenizer tokenizer;
	tokenizer.m_filename = "test";
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = source;
	tokenizer.m_current_token = tokenizer.nextToken();
	using Token = UITokenizer::Token;
	
	Token tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::COLOR, tok.type);
	ASSERT_TRUE(tok.value == "#FF0000");
	
	tok = tokenizer.consumeToken();
ASSERT_EQ(Token::ERROR, tok.type);
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::COLOR, tok.type);
	ASSERT_TRUE(tok.value == "#123456");
	
	tok = tokenizer.consumeToken();
ASSERT_EQ(Token::ERROR, tok.type);
	
	return true;
}

bool testSpecialCharacters() {
	const char* source = "= { } : ; . # $";
	UITokenizer tokenizer;
	tokenizer.m_filename = "test";
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = source;
	tokenizer.m_current_token = tokenizer.nextToken();
	using Token = UITokenizer::Token;
	
	Token tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::EQUALS, tok.type);
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::LBRACE, tok.type);
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::RBRACE, tok.type);
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::COLON, tok.type);
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::SEMICOLON, tok.type);
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::DOT, tok.type);
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::ERROR, tok.type);
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::DOLLAR, tok.type);
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::EOF, tok.type);
	
	return true;
}

bool testColorParsingStrictness() {
	const char* source = "#000 #000000";
	UITokenizer tokenizer;
	tokenizer.m_filename = "test";
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = source;
	tokenizer.m_current_token = tokenizer.nextToken();
	using Token = UITokenizer::Token;

	Token tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::ERROR, tok.type);

	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::COLOR, tok.type);
	ASSERT_TRUE(tok.value == "#000000");

	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::EOF, tok.type);

	return true;
}

} // anonymous namespace

void runUITokenizerTests() {
	logInfo("=== Running UI Tokenizer Tests ===");
	
	RUN_TEST(testSimpleTokens);
	RUN_TEST(testIdentifiers);
	RUN_TEST(testStrings);
	RUN_TEST(testUnterminatedString);
	RUN_TEST(testNumbers);
	RUN_TEST(testPercentages);
	RUN_TEST(testEm);
	RUN_TEST(testEmFollowedByIdentifier);
	RUN_TEST(testComments);
	RUN_TEST(testUnterminatedBlockComment);
	RUN_TEST(testWhitespace);
	RUN_TEST(testComplexUI);
	RUN_TEST(testEmptyInput);
	RUN_TEST(testOnlyWhitespace);
	RUN_TEST(testInvalidEscapeSequence);
	RUN_TEST(testUnterminatedLineComment);
	RUN_TEST(testInvalidStringEscape);
	RUN_TEST(testNumberStartingWithZero);
	RUN_TEST(testStringWithBackslashAtEnd);
	RUN_TEST(testBlockCommentInMarkup);
	RUN_TEST(testPercentageInAttributes);
	RUN_TEST(testQuotedAttributeValues);
	RUN_TEST(testMultilineStrings);
	RUN_TEST(testEscapedCharacters);
	RUN_TEST(testIdentifiersWithSpecialChars);
	RUN_TEST(testWhitespaceAroundEquals);
	RUN_TEST(testStyleTokens);
	RUN_TEST(testColors);
	RUN_TEST(testSpecialCharacters);
    RUN_TEST(testColorParsingStrictness);
}