#pragma once


#include "engine/core/math_utils.h"
#include "engine/core/string.h"


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
				Lumix::catString(str, "Unknown type");
			}


			template<int C>
			static void getOperand(char(&str)[C], int value)
			{
				char tmp[20];
				Lumix::toCString(value, tmp, C);
				Lumix::catString(str, tmp);
			}


			template<int C>
			static void getOperand(char(&str)[C], float value)
			{
				char tmp[20];
				Lumix::toCString(value, tmp, C, 5);
				Lumix::catString(str, tmp);
			}


			template <int C>
			void getOperator(char(&str)[C])
			{
				switch (oper)
				{
					case EQ: Lumix::catString(str, " == "); break;
					case NE: Lumix::catString(str, " != "); break;
					case LT: Lumix::catString(str, " < "); break;
					case GT: Lumix::catString(str, " > "); break;
					case GE: Lumix::catString(str, " >= "); break;
					case LE: Lumix::catString(str, " <= "); break;
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


		LUMIX_FORCE_INLINE void expect(ExpressionLHS<bool> expr, const char* file, uint32 line)
		{
			if (!expr.value)
			{
				Manager::instance().handleFail(expr.expression, file, line);
			}
		}


		template <typename T1, typename T2>
		LUMIX_FORCE_INLINE void expect(Expression<T1, T2> expr, const char* file, uint32 line)
		{
			if(!expr.result)
			{
				char tmp[1024];
				Lumix::copyString(tmp, "\"");
				Lumix::catString(tmp, expr.expression);
				Lumix::catString(tmp, "\" evaluated to ");
				expr.getOperand(tmp, expr.lhs);
				expr.getOperator(tmp);
				expr.getOperand(tmp, expr.rhs);
				Manager::instance().handleFail(tmp, file, line);
			}
		}
	} // ~UnitTest
} // ~Lumix

#define LUMIX_EXPECT(b)	Lumix::UnitTest::expect(Lumix::UnitTest::Result(#b) <= b, __FILE__, __LINE__)
#define LUMIX_EXPECT_CLOSE_EQ(a, b, e)	Lumix::UnitTest::expect(Lumix::UnitTest::Result(#a " close equals " #b) <= (((a) - (e)) < (b) && ((a) + (e)) > (b)), __FILE__, __LINE__)
