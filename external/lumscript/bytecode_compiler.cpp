#include "bytecode.h"

#include "ast.h"

ls_bytecode::ls_bytecode(const ls_host* host_)
	: host(host_ ? *host_ : ls_host{})
{}

static void pushInstruction(ls_bytecode& bytecode, BytecodeOp op, u32 payload) {
	u32* instr_bin = (u32*)&bytecode.instructions.emplace_back();
	*instr_bin = u8(op) | (payload << 8);
}

static bool bytecodeCompileExpr(Module& module, ls_bytecode& bytecode, i32 expr_idx) {
	if (expr_idx < 0 || expr_idx >= module.expressions.size()) return false;
	Expr& expr = module.expressions[expr_idx];
	switch (expr.kind) {
		case Expr::NUMBER: {
			u32 payload = 0;
			switch (expr.type.kind) {
				case TypeRef::I32: {
					payload = (u32)expr.number;
					// TODO make sure only 24 bits of value
					break;
				}
				default: ASSERT(false); return false; // TODO other types
			}

			pushInstruction(bytecode, BytecodeOp::LOAD_CONST, payload);
			return true;
		}
		default:
			// TODO
			return false;
	}
}

static bool bytecodeCompileStmt(Module& module, ls_bytecode& bytecode, i32 stmt_idx) {
	if (stmt_idx < 0 || stmt_idx >= module.statements.size()) return false;
	Stmt& stmt = module.statements[stmt_idx];
	switch (stmt.kind) {
		case Stmt::BLOCK:
			for (i32 child : stmt.children) {
				if (!bytecodeCompileStmt(module, bytecode, child)) return false;
			}
			return true;
		case Stmt::RETURN:
			if (stmt.expr >= 0 && !bytecodeCompileExpr(module, bytecode, stmt.expr)) return false;
			pushInstruction(bytecode, BytecodeOp::RETURN, 0);
			return true;
		default:
			return false;
	}
}

static bool bytecodeCompileFunction(Module& module, ls_bytecode& bytecode, FunctionDecl& fn) {
	if (fn.is_nested) return true;
	if (!fn.params.empty()) return false;

	BytecodeFunction out;
	out.name = fn.name;
	out.param_count = (i32)fn.params.size();
	out.first_instruction = (i32)bytecode.instructions.size();
	if (!bytecodeCompileStmt(module, bytecode, fn.body)) return false;

	out.instruction_count = (i32)bytecode.instructions.size() - out.first_instruction;
	bytecode.functions.push_back(out);
	return true;
}

ls_bytecode* compileBytecode(Module& module, const ls_host* host) {
	const ls_host* bytecode_host = host ? host : module.host;
	ls_bytecode* bytecode = allocateObject<ls_bytecode>(bytecode_host, bytecode_host);
	if (!bytecode) return nullptr;
	
	for (FunctionDecl& fn : module.functions) {
		if (!bytecodeCompileFunction(module, *bytecode, fn)) {
			destroyBytecode(bytecode);
			return nullptr;
		}
	}
	return bytecode;
}

void destroyBytecode(ls_bytecode* bytecode) {
	if (!bytecode) return;
	ls_host host = bytecode->host;
	deleteObject(&host, bytecode);
}
