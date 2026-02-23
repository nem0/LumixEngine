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
	ASSERT_EQ(Token::LBRACE, tok.type, "LBRACE");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::RBRACE, tok.type, "RBRACE");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::EQUALS, tok.type, "EQUALS");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::EOF, tok.type, "EOF");
	
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
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "First identifier");
	ASSERT_TRUE(tok.value == "panel", "First identifier value");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "Second identifier");
	ASSERT_TRUE(tok.value == "button", "Second identifier value");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "Third identifier");
	ASSERT_TRUE(tok.value == "class", "Third identifier value");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "Fourth identifier");
	ASSERT_TRUE(tok.value == "id", "Fourth identifier value");
	
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
	ASSERT_EQ(Token::STRING, tok.type, "First string");
	ASSERT_TRUE(tok.value == "hello", "First string value");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::STRING, tok.type, "Second string");
	ASSERT_TRUE(tok.value == "world with spaces", "Second string value");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::STRING, tok.type, "Empty string");
	ASSERT_TRUE(tok.value == "", "Empty string value");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::STRING, tok.type, "Escaped string");
	ASSERT_TRUE(tok.value == "escaped \\\"quote\\\"", "Escaped string value");
	
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
	ASSERT_EQ(Token::ERROR, tok.type, "Unterminated string should produce ERROR token");
	
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
	ASSERT_EQ(Token::NUMBER, tok.type, "First number");
	ASSERT_TRUE(tok.value == "123", "First number value");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::NUMBER, tok.type, "Second number");
	ASSERT_TRUE(tok.value == "456.789", "Second number value");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::NUMBER, tok.type, "Third number");
	ASSERT_TRUE(tok.value == "0", "Third number value");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::NUMBER, tok.type, "Fourth number");
	ASSERT_TRUE(tok.value == "1.0", "Fourth number value");
	
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
	ASSERT_EQ(Token::PERCENTAGE, tok.type, "First percentage");
	ASSERT_TRUE(tok.value == "50%", "First percentage value");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::PERCENTAGE, tok.type, "Second percentage");
	ASSERT_TRUE(tok.value == "100%", "Second percentage value");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::PERCENTAGE, tok.type, "Third percentage");
	ASSERT_TRUE(tok.value == "0%", "Third percentage value");
	
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
	ASSERT_EQ(Token::EM, tok.type, "First em");
	ASSERT_TRUE(tok.value == "1.5em", "First em value");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::EM, tok.type, "Second em");
	ASSERT_TRUE(tok.value == "2em", "Second em value");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::EM, tok.type, "Third em");
	ASSERT_TRUE(tok.value == "10em", "Third em value");
	
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
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "emsome");
	ASSERT_TRUE(tok.value == "emsome", "emsome value");
	
	return true;
}

bool testNumberFollowedByIdentifier() {
	const char* source = "123abc";
	UITokenizer tokenizer;
	tokenizer.m_filename = "test";
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = source;
	tokenizer.m_current_token = tokenizer.nextToken();
	using Token = UITokenizer::Token;
	
	Token tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::ERROR, tok.type, "Number followed by identifier without space should error");
	
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
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "First identifier");
	ASSERT_TRUE(tok.value == "panel", "First identifier value");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "Second identifier");
	ASSERT_TRUE(tok.value == "button", "Second identifier value");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "Third identifier");
	ASSERT_TRUE(tok.value == "text", "Third identifier value");
	
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
	ASSERT_EQ(Token::EOF, tok.type, "Unterminated block comment should be treated as whitespace and result in EOF");
	
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
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "First identifier after whitespace");
	ASSERT_TRUE(tok.value == "panel", "First identifier value");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "Second identifier after whitespace");
	ASSERT_TRUE(tok.value == "button", "Second identifier value");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::EOF, tok.type, "EOF after whitespace");
	
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
	ASSERT_EQ(Token::LBRACKET, tok.type, "left bracket");

	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "panel");
	ASSERT_TRUE(tok.value == "panel", "panel value");

	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "id");
	ASSERT_TRUE(tok.value == "id", "id value");

	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::EQUALS, tok.type, "equals");

	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::STRING, tok.type, "main");
	ASSERT_TRUE(tok.value == "main", "main value");

	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "width");
	ASSERT_TRUE(tok.value == "width", "width value");

	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::EQUALS, tok.type, "equals");

	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::NUMBER, tok.type, "800");
	ASSERT_TRUE(tok.value == "800", "800 value");

	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "height");
	ASSERT_TRUE(tok.value == "height", "height value");

	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::EQUALS, tok.type, "equals");

	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::NUMBER, tok.type, "600");
	ASSERT_TRUE(tok.value == "600", "600 value");

	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::RBRACKET, tok.type, "right bracket");

	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::LBRACE, tok.type, "left brace");

	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::RBRACE, tok.type, "right brace");

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
	ASSERT_EQ(Token::EOF, tok.type, "Empty input should produce EOF");
	
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
	ASSERT_EQ(Token::EOF, tok.type, "Only whitespace should produce EOF");
	
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
	ASSERT_EQ(Token::STRING, tok.type, "Multiline string");
	ASSERT_TRUE(tok.value == "line1\nline2", "Multiline string value");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::STRING, tok.type, "Indented multiline string");
	ASSERT_TRUE(tok.value == "  indented\n    more", "Indented multiline string value");
	
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
	ASSERT_EQ(Token::STRING, tok.type, "Escaped quote");
	ASSERT_TRUE(tok.value == "quote \\\" here", "Escaped quote value");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::STRING, tok.type, "Escaped sequences");
	ASSERT_TRUE(tok.value == "newline \\n tab \\t backslash \\\\", "Escaped sequences value");
	
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
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "Identifier with underscore");
	ASSERT_TRUE(tok.value == "panel_name", "Identifier with underscore value");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "Identifier with hyphen");
	ASSERT_TRUE(tok.value == "button-class", "Identifier with hyphen value");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "Identifier starting with underscore");
	ASSERT_TRUE(tok.value == "_private", "Identifier starting with underscore value");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "Identifier with numbers");
	ASSERT_TRUE(tok.value == "attr_123", "Identifier with numbers value");
	
	return true;
}

bool testWhitespaceAroundEquals() {
	const char* source = "width = 800 height=600 class = primary";
	UITokenizer tokenizer;
	tokenizer.m_filename = "test";
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = source;
	tokenizer.m_current_token = tokenizer.nextToken();
	using Token = UITokenizer::Token;
	
	Token tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "width");
	ASSERT_TRUE(tok.value == "width", "width value");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::EQUALS, tok.type, "equals with spaces");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::NUMBER, tok.type, "800");
	ASSERT_TRUE(tok.value == "800", "800 value");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "height");
	ASSERT_TRUE(tok.value == "height", "height value");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::EQUALS, tok.type, "equals without spaces");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::NUMBER, tok.type, "600");
	ASSERT_TRUE(tok.value == "600", "600 value");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "class");
	ASSERT_TRUE(tok.value == "class", "class value");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::EQUALS, tok.type, "equals with spaces again");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "primary");
	ASSERT_TRUE(tok.value == "primary", "primary value");
	
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
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "panel");
	ASSERT_TRUE(tok.value == "panel", "panel value");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::LBRACE, tok.type, "left brace");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "button");
	ASSERT_TRUE(tok.value == "button", "button value");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "class");
	ASSERT_TRUE(tok.value == "class", "class value");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::EQUALS, tok.type, "equals");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::STRING, tok.type, "primary");
	ASSERT_TRUE(tok.value == "primary", "primary value");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::LBRACE, tok.type, "left brace 2");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::STRING, tok.type, "Start");
	ASSERT_TRUE(tok.value == "Start", "Start value");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::RBRACE, tok.type, "right brace 2");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::RBRACE, tok.type, "right brace");
	
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
	ASSERT_EQ(Token::LBRACKET, tok.type, "left bracket");

	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "panel");

	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "width");

	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::EQUALS, tok.type, "equals");

	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::PERCENTAGE, tok.type, "100%");
	ASSERT_TRUE(tok.value == "100%", "100% value");

	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "height");

	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::EQUALS, tok.type, "equals");

	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::PERCENTAGE, tok.type, "50%");
	ASSERT_TRUE(tok.value == "50%", "50% value");

	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::RBRACKET, tok.type, "right bracket");

	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::LBRACE, tok.type, "left brace");

	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::RBRACE, tok.type, "right brace");

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
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "button");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::LBRACKET, tok.type, "left bracket");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "class");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::EQUALS, tok.type, "equals");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::STRING, tok.type, "primary");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "id");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::EQUALS, tok.type, "equals");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::STRING, tok.type, "start");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "width");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::EQUALS, tok.type, "equals");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::STRING, tok.type, "12em");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::RBRACKET, tok.type, "right bracket");
	
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
	ASSERT_EQ(Token::STRING, tok.type, "Escape sequences are accepted");
	ASSERT_TRUE(tok.value == "invalid \\z escape", "Escape sequence value");
	
	return true;
}

bool testInvalidNumberFormat() {
	const char* source = "123.456.789";
	UITokenizer tokenizer;
	tokenizer.m_filename = "test";
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = source;
	tokenizer.m_current_token = tokenizer.nextToken();
	using Token = UITokenizer::Token;
	
	Token tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::ERROR, tok.type, "Invalid number format (multiple decimals) should produce ERROR token");
	
	return true;
}

bool testInvalidPercentageFormat() {
	const char* source = "50%%";
	UITokenizer tokenizer;
	tokenizer.m_filename = "test";
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = source;
	tokenizer.m_current_token = tokenizer.nextToken();
	using Token = UITokenizer::Token;
	
	Token tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::ERROR, tok.type, "Invalid percentage format (double %) should produce ERROR token");

	return true;
}

bool testInvalidEmFormat() {
	const char* source = "1.5emem";
	UITokenizer tokenizer;
	tokenizer.m_filename = "test";
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = source;
	tokenizer.m_current_token = tokenizer.nextToken();
	using Token = UITokenizer::Token;
	
	Token tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::ERROR, tok.type, "Invalid em format (double em) should produce ERROR token");
	
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
	ASSERT_EQ(Token::EOF, tok.type, "Line comment should be treated as whitespace, resulting in EOF");
	
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
	ASSERT_EQ(Token::STRING, tok.type, "String with escape");
	ASSERT_TRUE(tok.value == "string with \\x invalid hex", "String with escape value");
	
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
	ASSERT_EQ(Token::NUMBER, tok.type, "Number with leading zero");
	ASSERT_TRUE(tok.value == "0123", "Number with leading zero value");
	
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
	ASSERT_EQ(Token::ERROR, tok.type, "String with backslash at end should produce ERROR token");
	
	return true;
}

bool testInvalidCharacter() {
	const char* source = "@";
	UITokenizer tokenizer;
	tokenizer.m_filename = "test";
	tokenizer.m_document = StringView(source);
	tokenizer.m_current = source;
	tokenizer.m_current_token = tokenizer.nextToken();
	using Token = UITokenizer::Token;
	
	Token tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::ERROR, tok.type, "Invalid character should produce ERROR token");
	
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
	ASSERT_EQ(Token::DOT, tok.type, "dot");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "primary identifier");
	ASSERT_TRUE(tok.value == "primary", "primary value");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::LBRACE, tok.type, "LBRACE");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "color identifier");
	ASSERT_TRUE(tok.value == "color", "color value");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::COLON, tok.type, "COLON");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "red identifier");
	ASSERT_TRUE(tok.value == "red", "red value");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::SEMICOLON, tok.type, "SEMICOLON");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "width identifier");
	ASSERT_TRUE(tok.value == "width", "width value");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::COLON, tok.type, "COLON");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::PERCENTAGE, tok.type, "100%");
	ASSERT_TRUE(tok.value == "100%", "100% value");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::SEMICOLON, tok.type, "SEMICOLON");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::RBRACE, tok.type, "RBRACE");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::DOLLAR, tok.type, "DOLLAR");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::IDENTIFIER, tok.type, "main_menu identifier");
	ASSERT_TRUE(tok.value == "main_menu", "main_menu value");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::LBRACE, tok.type, "LBRACE");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::RBRACE, tok.type, "RBRACE");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::EOF, tok.type, "EOF");
	
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
	ASSERT_EQ(Token::COLOR, tok.type, "First color");
	ASSERT_TRUE(tok.value == "#FF0000", "#FF0000 value");
	
	tok = tokenizer.consumeToken();
ASSERT_EQ(Token::ERROR, tok.type, "Second color should be ERROR");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::COLOR, tok.type, "Third color");
	ASSERT_TRUE(tok.value == "#123456", "#123456 value");
	
	tok = tokenizer.consumeToken();
ASSERT_EQ(Token::ERROR, tok.type, "Fourth color should be ERROR");
	
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
	ASSERT_EQ(Token::EQUALS, tok.type, "EQUALS");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::LBRACE, tok.type, "LBRACE");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::RBRACE, tok.type, "RBRACE");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::COLON, tok.type, "COLON");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::SEMICOLON, tok.type, "SEMICOLON");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::DOT, tok.type, "DOT");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::ERROR, tok.type, "ERROR");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::DOLLAR, tok.type, "DOLLAR");
	
	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::EOF, tok.type, "EOF");
	
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
	ASSERT_EQ(Token::ERROR, tok.type, "#000 should be ERROR");

	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::COLOR, tok.type, "#000000 should be COLOR");
	ASSERT_TRUE(tok.value == "#000000", "#000000 value");

	tok = tokenizer.consumeToken();
	ASSERT_EQ(Token::EOF, tok.type, "EOF");

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
	RUN_TEST(testNumberFollowedByIdentifier);
	RUN_TEST(testComments);
	RUN_TEST(testUnterminatedBlockComment);
	RUN_TEST(testWhitespace);
	RUN_TEST(testComplexUI);
	RUN_TEST(testEmptyInput);
	RUN_TEST(testOnlyWhitespace);
	RUN_TEST(testInvalidCharacter);
	RUN_TEST(testInvalidEscapeSequence);
	RUN_TEST(testInvalidNumberFormat);
	RUN_TEST(testInvalidPercentageFormat);
	RUN_TEST(testInvalidEmFormat);
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