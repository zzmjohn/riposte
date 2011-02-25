#include <string>
#include <sstream>
#include <stdexcept>
#include <string>

#include "value.h"
#include "type.h"
#include "bc.h"

static bool isLanguage(Value const& expr) {
	return expr.type() == Type::R_symbol ||
			expr.type() == Type::R_call ||
			expr.type() == Type::I_internalcall ||
			expr.type() == Type::R_expression;
}

// compilation routines
static void compile(State& state, Value const& expr, Block& block); 

static void compileConstant(State& state, Value const& expr, Block& block) {
	block.constants().push_back(expr);
	block.code().push_back(Instruction(ByteCode::kget, block.constants().size()-1));
}

static void compileGetSymbol(State& state, Symbol const& symbol, Block& block) {
	block.code().push_back(Instruction(ByteCode::get, symbol.index()));
}

static void compileInternalCall(State& state, InternalCall const& call, Block& block) {
	Symbol func(call[0]);
	if(func.toString() == "<-" || func.toString() == ".Assign") {
		compile(state, call[2], block);
		//block.constants().push_back(call[1]);
		//block.code().push_back(Instruction(ByteCode::kget, block.constants().size()-1));
		//compile(state, call[1], block);
		//block.code().push_back(Instruction(ByteCode::pop));
		block.code().push_back(Instruction(ByteCode::assign, Symbol(call[1]).index()));
	}
	else if(func.toString() == "for" || func.toString() == ".For") {
		compile(state, Call(call[2])[2], block);
		compile(state, Call(call[2])[1], block);
		compileConstant(state, call[1], block);
		// FIXME: to special common case "i in x:y", need to check if ':' has been replaced, also only works if stepping forward...
		block.code().push_back(Instruction(ByteCode::forbegin, 0));
		uint64_t beginbody = block.code().size();
		compile(state, call[3], block);
		uint64_t endbody = block.code().size();
		block.code().push_back(Instruction(ByteCode::forend, endbody-beginbody));
		block.code()[beginbody-1].a = endbody-beginbody+1;
	}
	else if(func.toString() == ".Brace" || func.toString() == "{") {
		uint64_t length = call.length();
		for(uint64_t i = 1; i < length; i++) {
			compile(state, call[i], block);
			if(i < length-1)
				block.code().push_back(Instruction(ByteCode::pop));
		}
	}
	else if(func.toString() == ".Paren" || func.toString() == "(") {
		//uint64_t length = call.length();
		compile(state, call[1], block);
	}
	else if(func.toString() == ".Add" || func.toString() == "+") {
		if(call.length() == 3)
			compile(state, call[2], block);
		compile(state, call[1], block);
		block.code().push_back(Instruction(ByteCode::add, call.length()-1));
	}
}

static void compileCall(State& state, Call const& call, Block& block) {
	uint64_t length = call.length();
	if(length == 0) {
		printf("call without any stuff\n");
		return;
	}

	// create a new block for each parameter...
	// insert delay instruction to make promise
	for(uint64_t i = length-1; i >= 1; i--) {
		if(call[i].type() == Type::R_symbol) {
			Value v;
			compile(state, call[i]).toValue(v);
			block.constants().push_back(v);
			block.code().push_back(Instruction(ByteCode::symdelay, block.constants().size()-1));
		}
		else if(isLanguage(call[i])) {
			Value v;
			compile(state, call[i]).toValue(v);
			block.constants().push_back(v);
			block.code().push_back(Instruction(ByteCode::delay, block.constants().size()-1));
		} else {
			compile(state, call[i], block);
		}
	}
	compile(state, call[0], block);
	
	// insert call
	block.code().push_back(Instruction(ByteCode::call, length-1));
}

static void compileICCall(State& state, Call const& call, Block& block) {
	uint64_t length = call.length();
	if(length == 0) {
		printf("call without any stuff\n");
		return;
	}

	// we might be able to inline if the function is a symbol...
	if(call[0].type() == Type::R_symbol) {
		if(Symbol(call[0]).toString() == "<-" 
			|| Symbol(call[0]).toString() == "for"
			|| Symbol(call[0]).toString() == "{"
			|| Symbol(call[0]).toString() == "("
			|| Symbol(call[0]).toString() == "+") {
			
			// compile the expensive call.
			Block b;
			compileCall(state, call, b);
			b.code().push_back(Instruction(ByteCode::ret));
			Value exp_value;
			b.toValue(exp_value);
			block.constants().push_back(exp_value);
			uint64_t exp_call_index = block.constants().size()-1;

			Value spec_value;
			state.baseenv->get(state, Symbol(call[0]), spec_value);
			block.constants().push_back(spec_value);
			uint64_t spec_value_index = block.constants().size()-1;

			// check needs 1) function, 2) specialized value, 3) expensive call, and 4) skip amount
			compile(state, call[0], block);
			block.code().push_back(Instruction(ByteCode::fguard, spec_value_index, exp_call_index, 0));
			uint64_t start = block.code().size();
			compileInternalCall(state, InternalCall(call), block);
			uint64_t end = block.code().size();
			block.code()[start-1].c = end-start+1;
			return;
		}
	}

	// otherwise...just generate a normal call
	compileCall(state, call, block);
}

static void compileExpression(State& state, Expression const& values, Block& block) {
	uint64_t length = values.length();
	for(uint64_t i = 0; i < length; i++) {
		compile(state, values[i], block);
		if(i < length-1)
			block.code().push_back(Instruction(ByteCode::pop));
	}
}


void compile(State& state, Value const& expr, Block& block) {

	switch(expr.type().internal())
	{
		case Type::R_symbol:
			compileGetSymbol(state, Symbol(expr), block);
			break;
		case Type::R_call:
			compileICCall(state, Call(expr), block);
			break;
		case Type::I_internalcall:
			compileInternalCall(state, InternalCall(expr), block);
			break;	
		case Type::R_expression:
			compileExpression(state, Expression(expr), block);
			break;
		default:
			compileConstant(state, expr, block);
			break;
	};
}

Block compile(State& state, Value const& expr) {
	Block block;
	compile(state, expr, block);
	block.expression() = expr;
	// insert return statement at end of block
	block.code().push_back(Instruction(ByteCode::ret));
	return block;	
}


/*
void functionCall(Value const& func, Value const* values, uint64_t length, Environment* env, Value& result) {
	Function const& f = asFunction(func);
	// create a new environment for the function call 
	// (stack discipline is hard to prove in R, but can we do better?)
    // 1. If a function is returned, must assume that it contains upvalues to anything in either
	//    its static scope (true upvalues) or dynamic scope (promises)
    // 2. Upvalues can be contained in eval'ed code! consider, function(x) return(function() eval(parse(text='x')))
    // 3. Functions can be held in any non-basic datatype (though lists seem like the obvious possibility)
	// 4. So by (2) can't statically check, 
	//		by (3) we'd have to traverse entire returned data structure to check for a function.
	// 5. More conservatively, we could flag the creation of any function within a scope,
	//		if that scope returns a non-basic type, we'd have to move the environment off the stack.
    //    ---but, what about updating references to the environment. Ugly since we don't know
	//	  ---which function is the problem or which upvalues will be used.
	// Conclusion for now: heap allocate environments. Try to make that fast, maybe with a pooled allocator...
	Environment* call_env = new Environment(f.s, env);
	// populate with parameters
	Character names(f.args.names());
	for(uint64_t i = 0; i < length; i++) {
		call_env->assign(names[i], values[i]);
	}
	// call interpret
	eval(Block(f.body), call_env, values, length, result);	
}

void functionCallInternal(Value const& func, Value const* values, uint64_t length, Environment* env, Value& result) {
	CFunction const& f = asCFunction(func);
	f.func(env, values, length, result);
}

void eval(Block const& block, Environment* env, Value const* slots, uint64_t slength, Value& result) {
	Value registers[16];
	Promise promises[16];
	uint64_t pindex = 0;
	const uint64_t length = block.inner->code.size();
	for(uint64_t i = 0; i < length; i++) {
		Instruction const& inst = block.inner->code[i];
		switch(inst.bc.internal()) {
			case ByteCode::call:
			case ByteCode::ccall:
			{
				Value func(registers[inst.a]);
				uint64_t start = inst.a+1;
				uint64_t length = inst.b;
			
				if(func.type() == Type::R_function) {
					functionCall(func, &registers[start], length, env, registers[inst.c]);
				} else if(func.type() == Type::R_cfunction) {
					functionCallInternal(func, &registers[start], length, env, registers[inst.c]);
				} else {
					printf("Non-function as first parameter to call\n");
				}
			} break;
			case ByteCode::slot:
				registers[inst.c] = slots[inst.a];
			break;
			case ByteCode::get:
				env->get(Symbol(inst.a), registers[inst.c]);
			break;
			case ByteCode::kget:
				registers[inst.c] = block.inner->constants[inst.a];
			break;
			case ByteCode::delay:
				promises[inst.c].set(block.inner->constants[inst.a], block.inner->constants[inst.b], env);
				Value::set(registers[inst.c], Type::R_promise, &promises[inst.c]);
			break;
			case ByteCode::assign:
				env->assign(Symbol(registers[inst.a]), registers[inst.c]);
			break;
			case ByteCode::zip2:
				zip2(registers[inst.a], registers[inst.b], registers[inst.c], registers[inst.op]);
			break;
			case ByteCode::forbegin:
				env->assign(Symbol(inst.a), registers[inst.c]);
				if(asReal1(registers[inst.c]) > asReal1(registers[inst.b]))
					i = i + inst.op;
			break;
			case ByteCode::forend:
				Value::setDouble(registers[inst.c], asReal1(registers[inst.c])+1);
				if(asReal1(registers[inst.c]) <= asReal1(registers[inst.b])) {
					env->assign(Symbol(inst.a), registers[inst.c]);
					i = i - inst.op;
				} else {
					Value::set(registers[inst.c], Type::R_null, 0);
				}
			break;
			case ByteCode::function:
				Value::set(registers[inst.c], Type::R_function, new Function(List(registers[inst.a]), Block(registers[inst.b]), env));
			break;
			case ByteCode::quote:
				if(registers[inst.a].type() == Type::R_promise)
					asPromise(registers[inst.a]).inner(registers[inst.c]);
				else
					registers[inst.c] = registers[inst.a];
				//env->getQuoted(Symbol(inst.a), registers[inst.c]);
			break;
			case ByteCode::force:
				if(registers[inst.a].type() == Type::R_promise)
					asPromise(registers[inst.a]).eval(registers[inst.c]);
				else
					registers[inst.c] = registers[inst.a];
			break;
			case ByteCode::forceall: {
				for(uint64_t i = 0; i < slength; i++) {
					if(slots[i].type() == Type::R_promise)
						asPromise(slots[i]).eval(registers[inst.c]);
					else
						registers[inst.c] = slots[i];
				}
			} break;
			case ByteCode::code:
				if(registers[inst.a].type() == Type::R_promise)
					asPromise(registers[inst.a]).code(registers[inst.c]);
				else
					registers[inst.c] = registers[inst.a];
				//env->getCode(Symbol(inst.a), registers[inst.c]);
			break;
		}
	}
	result = registers[block.inner->code[length-1].c];
}

void eval(Block const& block, Environment* env, Value& result) {
	eval(block, env, 0, 0, result);	
}

void compile(Value& expr, Environment* env, Block& block) {
	
	switch(expr.type().internal())
	{
		case Type::R_null:
		case Type::R_raw:
		case Type::R_logical:
		case Type::R_integer:
		case Type::R_double:
		case Type::R_scalardouble:
		case Type::R_complex:		
		case Type::R_character:
		case Type::R_list:
		case Type::R_pairlist:
		case Type::R_function:
		case Type::R_cfunction:
		case Type::R_promise:
		case Type::R_default:
		case Type::ByteCode:
			block.inner->constants.push_back(expr);
			block.inner->code.push_back(Instruction(ByteCode::kget, block.inner->constants.size()-1,0,block.inner->reg++));
			break;
		case Type::R_symbol:
			block.inner->code.push_back(Instruction(ByteCode::get, Symbol(expr).index(), 0,block.inner->reg++));
			break;
		case Type::R_call:
		{
			Call call(expr);
			uint64_t length = call.length();
			if(length == 0) printf("call without any stuff\n");
			uint64_t start = block.inner->reg;
			compile(call[0], env, block);

			// create a new block for each parameter...
			// insert delay instruction to make promise
			for(uint64_t i = 1; i < length; i++) {
				if(isLanguage(call[i])) {
					Block b;
					compile(call[i], env, b);
					Value v;
					b.toValue(v);
					block.inner->constants.push_back(v);
					block.inner->constants.push_back(call[i]);
					block.inner->code.push_back(Instruction(ByteCode::delay, block.inner->constants.size()-2,block.inner->constants.size()-1,block.inner->reg++));
				} else {
					compile(call[i], env, block);
				}
			}
	
			// insert call
			block.inner->code.push_back(Instruction(ByteCode::call, start, block.inner->reg-1-start, start));
			block.inner->reg = start+1;
		} break;
		case Type::R_internalcall: {
			InternalCall call(expr);
			Symbol func(call[0]);
			if(func.toString() == ".Assign") {
				uint64_t start = block.inner->reg;
				compile(call[1], env, block);
				uint64_t a = block.inner->reg-1;
				compile(call[2], env, block);
				uint64_t b = block.inner->reg-1;
				compile(call[3], env, block);
				uint64_t c = block.inner->reg-1;
				block.inner->code.push_back(Instruction(ByteCode::assign, 
					a,
					c,
					b));
				block.inner->reg = start;
			}
			else if(func.toString() == ".Slot") {
				block.inner->code.push_back(Instruction(ByteCode::slot, 
					asReal1(call[1]),
					0,
					block.inner->reg++));
			}
			else if(func.toString() == ".Zip2") {
				uint64_t start = block.inner->reg;
				compile(call[1], env, block);
				uint64_t a = block.inner->reg-1;
				compile(call[2], env, block);
				uint64_t b = block.inner->reg-1;
				compile(call[3], env, block);
				uint64_t c = block.inner->reg-1;
				block.inner->code.push_back(Instruction(ByteCode::zip2, 
					a,
					b,
					start,
					c));
				block.inner->reg = start+1;
			}
			else if(func.toString() == ".Brace") {
				InternalCall call(expr);
				uint64_t length = call.length();
				uint64_t start = block.inner->reg;
				for(uint64_t i = 1; i < length; i++) {
					uint64_t istart = block.inner->reg;
					compile(call[i], env, block);
					block.inner->reg = istart;
				}
				block.inner->reg = start+1;
			}
			else if(func.toString() == ".Paren") {
				InternalCall call(expr);
				uint64_t length = call.length();
				if(length == 2) {
					uint64_t start = block.inner->reg;
					compile(call[1], env, block);
					block.inner->reg = start+1;
				}
			}
			else if(func.toString() == ".For") {
				uint64_t start = block.inner->reg;
				block.inner->constants.push_back(call[1]);
				block.inner->code.push_back(Instruction(ByteCode::kget, block.inner->constants.size()-1,0,block.inner->reg++));
				uint64_t lvar = block.inner->reg-1;
	
				// FIXME: to special common case "i in x:y", need to check if ':' has been replaced, also only works if stepping forward...
				compile(Call(call[2])[1], env, block);
				uint64_t lower = block.inner->reg-1;
				compile(Call(call[2])[2], env, block);
				uint64_t upper = block.inner->reg-1;
				uint64_t begin = block.inner->code.size();
				block.inner->code.push_back(Instruction(ByteCode::forbegin, lvar, upper, lower));
				compile(call[3], env, block);
				uint64_t endbody = block.inner->code.size();
				block.inner->code.push_back(Instruction(ByteCode::forend, lvar, upper, lower, endbody-upper-1));
				block.inner->code[begin].op = endbody-begin;
				block.inner->reg = start+1;
			}
			else if(func.toString() == ".Function") {
				// two parameters: argument as list and body
				uint64_t start = block.inner->reg;
				compile(call[1], env, block);
				uint64_t args = block.inner->reg-1;
				//Block b;
				//compile(call[2], env, b);
				//Value v;
				//b.toValue(v);
				//compile(v, env, block);
				compile(call[2], env, block);
				uint64_t body = block.inner->reg-1;
				block.inner->code.push_back(Instruction(ByteCode::function, args, body, start, 0));
				block.inner->reg = start+1;
			}
			else if(func.toString() == ".RawFunction") {
				// two parameters: argument as list and body
				uint64_t start = block.inner->reg;
				Block b;
				compile(call[1], env, b);
				Value v;
				b.toValue(v);
				compile(v, env, block);
				uint64_t body = block.inner->reg-1;
				block.inner->code.push_back(Instruction(ByteCode::rawfunction, 0, body, start, 0));
				block.inner->reg = start+1;
			}
			else if(func.toString() == ".Quote") {
				uint64_t start = block.inner->reg;
				compile(call[1], env, block);
				uint64_t arg = block.inner->reg-1;
				block.inner->code.push_back(Instruction(ByteCode::quote, arg, 0, start, 0));
				block.inner->reg = start+1;
				
				//block.inner->code.push_back(Instruction(ByteCode::quote, Symbol(call[1]).index(), 0,block.inner->reg++));
			}
			else if(func.toString() == ".Force") {
				uint64_t start = block.inner->reg;
				compile(call[1], env, block);
				uint64_t arg = block.inner->reg-1;
				block.inner->code.push_back(Instruction(ByteCode::force, arg, 0, start, 0));
				block.inner->reg = start+1;
			}
			else if(func.toString() == ".ForceAll") {
				uint64_t start = block.inner->reg;
				block.inner->code.push_back(Instruction(ByteCode::forceall, 0, 0, start, 0));
				block.inner->reg = start+1;
			}
			else if(func.toString() == ".Code") {
				uint64_t start = block.inner->reg;
				compile(call[1], env, block);
				uint64_t arg = block.inner->reg-1;
				block.inner->code.push_back(Instruction(ByteCode::code, arg, 0, start, 0));
				block.inner->reg = start+1;
				//block.inner->code.push_back(Instruction(ByteCode::code, Symbol(call[1]).index(), 0,block.inner->reg++));
			}
			else if(func.toString() == ".Block") {
				Block b;
				compile(call[1], env, b);
				Value v;
				b.toValue(v);
				compile(v, env, block);
			}
			else if(func.toString() == ".List") {
				Value v;
				call.subset(1, call.length()-1, v);
				v.t = Type::R_list;
				List l(v);
				if(call.names().type() == Type::R_character)
					Character(call.names()).subset(1, call.length()-1, l.inner->names);
				l.toValue(v);
				compile(v, env, block);
			}
			else if(func.toString() == ".Const") {
				block.inner->constants.push_back(call[1]);
				block.inner->code.push_back(Instruction(ByteCode::kget, block.inner->constants.size()-1,0,block.inner->reg++));
			}
		} break;	
		case Type::R_expression:
		{
			Expression values(expr);
			uint64_t length = values.length();
			for(uint64_t i = 0; i < length; i++) {
				compile(values[i], env, block);
			}
		} break;
	};
}
*/


/*void functionCall(Value const& func, Call const& values, Environment* env, Value& result) {
	// create a new environment for the function call
	Environment call_env;
	
	Function const& f = asFunction(func);
	call_env.initialize(f.s, env);
	// populate with parameters
	uint64_t length = values.length();
	for(uint64_t i = 0; i < length-1; i++) {
		if(isLanguage(values[i+1]))
			call_env.assign(f.formals[i].name, values[i+1], env);
		else
			call_env.assign(f.formals[i].name, values[i+1]);
	}
	// call interpret
	interpret(f.body, &call_env, result);	
}

void functionCallInternal(Value const& func, Call const& values, Environment* env, Value& result) {
	// static stack seems to be a bit faster than stack allocating slots
	static Value parameters[256];
	static Promise promises[256];
	static uint64_t index = 0;
	
	CFunction const& f = asCFunction(func);
	// populate with parameters
	uint64_t length = values.length();
	for(uint64_t i = 0; i < length-1; i++) {
		if(isLanguage(values[i+1])) {
			promises[i+index].set(values[i+1], values[i+1], env);
			Value::set(parameters[i+index], Type::R_promise, &promises[i+index]);
		} else
			parameters[i+index] = values[i+1];
	}
	// call internal
	index += length-1;
	f.func(env, &parameters[index-(length-1)], length-1, result);
	index -= length-1;
}

void vm(Block const& block, Environment* env, Value& result);

void interpret(Value const& expr, Environment* env, Value& result) {
	switch(expr.type().internal())
	{
		case Type::ByteCode:
		{
			Block b(expr);
			vm(b, env, result);
		} break;
		case Type::R_null:
		case Type::R_raw:
		case Type::R_logical:
		case Type::R_integer:
		case Type::R_double:
		case Type::R_scalardouble:
		case Type::R_complex:		
		case Type::R_character:
		case Type::R_list:
		case Type::R_pairlist:
		case Type::R_function:
		case Type::R_cfunction:
			result = expr;
			break;			// don't have to do anything for primitive types
		case Type::R_symbol:
			env->get(Symbol(expr), result);
			break;
		case Type::R_call:
		{
			Call call(expr);
			uint64_t length = call.length();
			if(length == 0) printf("call without any stuff\n");
			Value func;
			interpret(call[0], env, func);
			
			if(func.type() == Type::R_function) {
				functionCall(func, call, env, result);
			} else if(func.type() == Type::R_cfunction) {
				functionCallInternal(func, call, env, result);
			} else {
				printf("Non-function as first parameter to call\n");
			}
		} 	break;
		case Type::R_expression:
		{
			Expression statements(expr);
			uint64_t length = statements.length();
			for(uint64_t i = 0; i < length; i++) {
				interpret(statements[i], env, result);
			}
		} 	break;
		case Type::R_promise:
		case Type::R_default:
			printf("promise or default value exposed at interpreter?\n");
			break;
	};
}*/