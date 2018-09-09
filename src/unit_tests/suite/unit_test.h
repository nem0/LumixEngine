#pragma once


#include "engine/math_utils.h"
#include "engine/string.h"


namespace Lumix
{
	namespace UnitTest
	{
		enum Operator
		{
			EQ,
			LT,
			GT,
			GE,
			LE,
			NE
		};


		template <typename T1, typename T2>
		struct Expression
		{
			Expression(const char* expr, Operator op, T1 t1, T2 t2, bool res)
				: lhs(t1)
				, rhs(t2)
				, result(res)
				, expression(expr)
				, oper(op)
			{
			}


			template <int C, typename T>
			static void getOperand(char(&str)[C], T value)
			{
				catString(str, "Unknown type");
			}


			template<int C>
			static void getOperand(char(&str)[C], int value)
			{
				char tmp[20];
				toCString(value, tmp, C);
				catString(str, tmp);
			}


			template<int C>
			static void getOperand(char(&str)[C], float value)
			{
				char tmp[20];
				toCString(value, tmp, C, 5);
				catString(str, tmp);
			}


			template<int C>
			static void getOperand(char(&str)[C], EntityRef value)
			{
				char tmp[20];
				toCString(value.index, tmp, C);
				catString(str, "{");
				catString(str, tmp);
				catString(str, "}");
			}


			template <int C>
			void getOperator(char(&str)[C])
			{
				switch (oper)
				{
					case EQ: catString(str, " == "); break;
					case NE: catString(str, " != "); break;
					case LT: catString(str, " < "); break;
					case GT: catString(str, " > "); break;
					case GE: catString(str, " >= "); break;
					case LE: catString(str, " <= "); break;
					default: ASSERT(false); break;
				}
			}


			T1 lhs;
			T2 rhs;
			bool result;
			const char* expression;
			Operator oper;
		};


		template <typename T>
		struct ExpressionLHS
		{
			ExpressionLHS(const char* expression, T value)
			{
				this->value = value;
				this->expression = expression;
			}


			template <typename T2>
			Expression<T, T2> operator <= (T2 value)
			{
				return Expression<T, T2>(expression, LE, this->value, value, this->value <= value);
			}


			template <typename T2>
			Expression<T, T2> operator == (T2 value)
			{
				return Expression<T, T2>(expression, EQ, this->value, value, this->value == value);
			}


			template <typename T2>
			Expression<T, T2> operator >= (T2 value)
			{
				return Expression<T, T2>(expression, GE, this->value, value, this->value >= value);
			}


			template <typename T2>
			Expression<T, T2> operator != (T2 value)
			{
				return Expression<T, T2>(expression, NE, this->value, value, this->value != value);
			}


			template <typename T2>
			Expression<T, T2> operator > (T2 value)
			{
				return Expression<T, T2>(expression, GT, this->value, value, this->value > value);
			}


			template <typename T2>
			Expression<T, T2> operator < (T2 value)
			{
				return Expression<T, T2>(expression, LT, this->value, value, this->value < value);
			}


			const char* expression;
			T value;
		};


		struct Result
		{
			Result(const char* expression)
			{
				this->expression = expression;
			}


			template <typename T> ExpressionLHS<T> operator <= (T value)
			{
				return ExpressionLHS<T>(expression, value);
			}


			const char* expression;
		};


		LUMIX_FORCE_INLINE void expect(ExpressionLHS<bool> expr, const char* file, u32 line)
		{
			if (!expr.value)
			{
				Manager::instance().handleFail(expr.expression, file, line);
			}
		}


		template <typename T1, typename T2>
		LUMIX_FORCE_INLINE void expect(Expression<T1, T2> expr, const char* file, u32 line)
		{
			if(!expr.result)
			{
				char tmp[1024];
				copyString(tmp, "\"");
				catString(tmp, expr.expression);
				catString(tmp, "\" evaluated to ");
				expr.getOperand(tmp, expr.lhs);
				expr.getOperator(tmp);
				expr.getOperand(tmp, expr.rhs);
				Manager::instance().handleFail(tmp, file, line);
			}
		}
	} // ~UnitTest
} // ~Lumix

#define LUMIX_EXPECT(b)	UnitTest::expect(UnitTest::Result(#b) <= b, __FILE__, __LINE__)
#define LUMIX_EXPECT_CLOSE_EQ(a, b, e)	UnitTest::expect(UnitTest::Result(#a " close equals " #b) <= (((a) - (e)) < (b) && ((a) + (e)) > (b)), __FILE__, __LINE__)
