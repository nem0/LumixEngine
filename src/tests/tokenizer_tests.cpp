#include "core/log.h"
#include "core/tokenizer.h"
#include "tests/common.h"

using namespace Lumix;

namespace {

bool testBoundedTokenization() {
	const char source[] = "_x -12.5 \"ok\"!";
	Tokenizer tokenizer(StringView(source, sizeof(source) - 1), "test");

	Tokenizer::Token token = tokenizer.tryNextToken();
	ASSERT_EQ(token.type, Tokenizer::Token::IDENTIFIER);
	ASSERT_TRUE(token == "_x");

	token = tokenizer.tryNextToken();
	ASSERT_EQ(token.type, Tokenizer::Token::NUMBER);
	ASSERT_TRUE(token == "-12.5");

	token = tokenizer.tryNextToken();
	ASSERT_EQ(token.type, Tokenizer::Token::STRING);
	ASSERT_TRUE(token == "ok");

	token = tokenizer.tryNextToken();
	ASSERT_EQ(token.type, Tokenizer::Token::SYMBOL);
	ASSERT_TRUE(token == "!");

	token = tokenizer.tryNextToken();
	ASSERT_EQ(token.type, Tokenizer::Token::EOF);
	return true;
}

bool testBoundedTokenizerConsumptionAndParse() {
	const char vector_source[] = "{1,2.5,-3}";
	Tokenizer vector_tokenizer(StringView(vector_source, sizeof(vector_source) - 1), "test");
	ASSERT_TRUE(vector_tokenizer.consume("{"));
	float vector[4];
	u32 vector_size = 0;
	ASSERT_TRUE(vector_tokenizer.consumeVector(vector, vector_size));
	ASSERT_EQ(vector_size, 3u);
	ASSERT_EQ(vector[0], 1.0f);
	ASSERT_EQ(vector[1], 2.5f);
	ASSERT_EQ(vector[2], -3.0f);

	const char source[] = "enabled = true count = 42 name = value items = [ one [ two ] ]";
	bool enabled = false;
	u32 count = 0;
	StringView name;
	StringView items;
	const ParseItemDesc descs[] = {
		ParseItemDesc("enabled", &enabled),
		ParseItemDesc("count", &count),
		ParseItemDesc("name", &name),
		ParseItemDesc("items", &items, true)
	};
	ASSERT_TRUE(parse(StringView(source, sizeof(source) - 1), "test", Span(descs)));
	ASSERT_TRUE(enabled);
	ASSERT_EQ(count, 42u);
	ASSERT_TRUE(equalStrings(name, "value"));
	ASSERT_TRUE(equalStrings(items, "[ one [ two ] ]"));
	return true;
}

bool testBoundedTokenizerErrors() {
	const char unterminated[] = "\"x";
	Tokenizer tokenizer(StringView(unterminated, sizeof(unterminated) - 1), "test");
	ASSERT_EQ(tokenizer.tryNextToken().type, Tokenizer::Token::ERROR);

	// This follows the error-position path, which scans the bounded input for its line.
	const char invalid_number[] = "1x";
	Tokenizer invalid_tokenizer(StringView(invalid_number, sizeof(invalid_number) - 1), "test");
	ASSERT_EQ(invalid_tokenizer.tryNextToken().type, Tokenizer::Token::ERROR);

	const StringView empty;
	Tokenizer empty_tokenizer(empty, "test");
	ASSERT_EQ(empty_tokenizer.tryNextToken().type, Tokenizer::Token::EOF);
	return true;
}

} // namespace

void runTokenizerTests() {
	RUN_TEST(testBoundedTokenization);
	RUN_TEST(testBoundedTokenizerConsumptionAndParse);
	RUN_TEST(testBoundedTokenizerErrors);
}
