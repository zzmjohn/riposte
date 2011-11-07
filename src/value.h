
#ifndef _RIPOSTE_VALUE_H
#define _RIPOSTE_VALUE_H

#include <map>
#include <iostream>
#include <vector>
#include <stack>
#include <deque>
#include <assert.h>
#include <limits>
#include <complex>

#include "gc.h"
#include "type.h"
#include "bc.h"
#include "common.h"
#include "enum.h"
#include "string.h"
#include "exceptions.h"


#include "ir.h"
#include "recording.h"

struct Value {
	
	union {
		struct {
			Type::Enum type:4;
			int64_t length:60;
		};
		int64_t header;
	};
	union {
		void* p;
		int64_t i;
		double d;
		unsigned char c;
		String s;
		struct {
			Type::Enum typ;
			uint32_t ref;
		} future;
	};

	static void Init(Value& v, Type::Enum type, int64_t length) {
		v.header =  type + (length<<4);
	}

	// Warning: shallow equality!
	bool operator==(Value const& other) const {
		return header == other.header && p == other.p;
	}
	
	bool operator!=(Value const& other) const {
		return header != other.header || p != other.p;
	}
	
	bool isNil() const { return header == 0; }
	bool isNull() const { return type == Type::Null; }
	bool isLogical() const { return type == Type::Logical; }
	bool isInteger() const { return type == Type::Integer; }
	bool isLogical1() const { return header == (1<<4) + Type::Logical; }
	bool isInteger1() const { return header == (1<<4) + Type::Integer; }
	bool isDouble() const { return type == Type::Double; }
	bool isDouble1() const { return header == (1<<4) + Type::Double; }
	bool isCharacter() const { return type == Type::Character; }
	bool isCharacter1() const { return header == (1<<4) + Type::Character; }
	bool isList() const { return type == Type::List; }
	bool isSymbol() const { return type == Type::Symbol; }
	bool isPromise() const { return type == Type::Promise && !isNil(); }
	bool isFunction() const { return type == Type::Function; }
	bool isObject() const { return type == Type::Object; }
	bool isFuture() const { return type == Type::Future; }
	bool isEnvironment() const { return type == Type::Environment; }
	bool isMathCoerce() const { return isDouble() || isInteger() || isLogical(); }
	bool isLogicalCoerce() const { return isDouble() || isInteger() || isLogical(); }
	bool isVector() const { return isNull() || isLogical() || isInteger() || isDouble() || isCharacter() || isList(); }
	bool isClosureSafe() const { return isNull() || isLogical() || isInteger() || isDouble() || isFuture() || isCharacter() || isSymbol() || (isList() && length==0); }
	bool isConcrete() const { return type != Type::Promise; }

	template<class T> T& scalar() { throw "not allowed"; }
	template<class T> T const& scalar() const { throw "not allowed"; }

	static Value const& Nil() { static const Value v = { {{Type::Promise, 0}}, {0} }; return v; }
};

template<> inline int64_t& Value::scalar<int64_t>() { return i; }
template<> inline double& Value::scalar<double>() { return d; }
template<> inline unsigned char& Value::scalar<unsigned char>() { return c; }
template<> inline String& Value::scalar<String>() { return s; }

template<> inline int64_t const& Value::scalar<int64_t>() const { return i; }
template<> inline double const& Value::scalar<double>() const { return d; }
template<> inline unsigned char const& Value::scalar<unsigned char>() const { return c; }
template<> inline String const& Value::scalar<String>() const { return s; }


//
// Value type implementations
//

class Prototype;
class Environment;
class State;

// A symbol has the same format as a 1-element character vector.
struct Symbol : public Value {
	Symbol() {
		Value::Init(*this, Type::Symbol, 1);
		s = Strings::NA;
	} 
	
	explicit Symbol(String str) {
		Value::Init(*this, Type::Symbol, 1);
		s = str; 
	} 

	explicit Symbol(Value const& v) {
		assert(v.isSymbol() || v.isCharacter1()); 
		header = v.header;
		s = v.s;
	}

	operator String() const {
		return s;
	}

	operator Value() const {
		return *(Value*)this;
	}

	bool operator==(Symbol const& other) const { return s == other.s; }
	bool operator!=(Symbol const& other) const { return s != other.s; }
	bool operator==(String other) const { return s == other; }
	bool operator!=(String other) const { return s != other; }

	Symbol Clone() const { return *this; }
};


template<Type::Enum VType, typename ElementType, bool Recursive,
	bool canPack = sizeof(ElementType) <= sizeof(int64_t) && !Recursive>
struct Vector : public Value {
private:	
	void reserve(State& state, int64_t size);
public:
	typedef ElementType Element;
	static const int64_t width = sizeof(ElementType); 
	static const Type::Enum VectorType = VType;

	bool isScalar() const {
		return length == 1;
	}

	struct Inner : public HeapObject {
		uint64_t capacity;	// HeapObjects are now 24 bytes, this makes data 16B aligned
		ElementType data[];

		Inner(uint64_t capacity) : capacity(capacity) {}

		virtual void walk(Heap* heap) {
			//if(Recursive) {
			//	for(uint64_t i = 0; i < length; i++) walkValue(heap, data[i]);
			//}
		}
	};

	ElementType& s() { return canPack ? Value::scalar<ElementType>() : ((Inner*)p)->data[0]; }
	ElementType const& s() const { return canPack ? Value::scalar<ElementType>() : ((Inner const*)p)->data[0]; }

	ElementType const* v() const { return (canPack && isScalar()) ? &Value::scalar<ElementType>() : ((Inner const*)p)->data; }
	ElementType* v() { return (canPack && isScalar()) ? &Value::scalar<ElementType>() : ((Inner*)p)->data; }
	
	ElementType& operator[](int64_t index) { return v()[index]; }
	ElementType const& operator[](int64_t index) const { return v()[index]; }

	explicit Vector() {
		Value::Init(*this, VectorType, 0);
	}

	explicit Vector(State& state, int64_t length) {
		Value::Init(*this, VectorType, length);
		reserve(state, length);
	}

	static Vector<VType, ElementType, Recursive>& Init(State& state, Value& v, int64_t length) {
		v = Vector(state, length);
		return (Vector<VType, ElementType, Recursive>&)v;
	}

	static void InitScalar(State& state, Value& v, ElementType const& d) {
		if(canPack) {
			Value::Init(v, VectorType, 1);
			v.scalar<ElementType>() = d;
		}
		else {
			v = Vector(state, 4);
			((Inner*)v.p)->data[0] = d;
		}
	}

	explicit Vector(Value const& v) {
		assert(v.type == VType);
		type = VType;
		length = v.length;
		p = v.p;
	}

	operator Value() const {
		return (Value&)*this;
	}

	// these functions are only really safe if you know you have the only pointer to this vector

	void append(State& state, ElementType e) {
		if(length == 0 && canPack) {
			length = 1;
			scalar<ElementType>() = e;
		}
		else {
			if(length == 0) {
				Vector<VType, ElementType, Recursive> n(state, 1LL);
				p = n.p;
			}
			if(length >= ((Inner*)p)->capacity) {
				Vector<VType, ElementType, Recursive> n(state, (int64_t)length*2);
				memcpy(n.v(), v(), length*sizeof(ElementType));
				p = n.p;
			}
			((Inner*)p)->data[length++] = e;
		}
	}
};

union _doublena {
	int64_t i;
	double d;
};


#define VECTOR_IMPL(Name, Element, Recursive) 				\
struct Name : public Vector<Type::Name, Element, Recursive> { 			\
	explicit Name() : Vector<Type::Name, Element, Recursive>() {} 	\
	explicit Name(State& state, int64_t length) : Vector<Type::Name, Element, Recursive>(state, length) {} 	\
	explicit Name(Value const& v) : Vector<Type::Name, Element, Recursive>(v) {} 	\
	static Name c() { Name c; return c; } \
	static Name c(State& state, Element v0) { Name c(state, 1); c[0] = v0; return c; } \
	static Name c(State& state, Element v0, Element v1) { Name c(state, 2); c[0] = v0; c[1] = v1; return c; } \
	static Name c(State& state, Element v0, Element v1, Element v2) { Name c(state, 3); c[0] = v0; c[1] = v1; c[2] = v2; return c; } \
	static Name c(State& state, Element v0, Element v1, Element v2, Element v3) { Name c(state, 4); c[0] = v0; c[1] = v1; c[2] = v2; c[3] = v3; return c; } \
	const static Element NAelement; \
	static Name NA(State& state) { static Name na = Name::c(state, NAelement); return na; }  \
	static Name& Init(State& state, Value& v, int64_t length) { return (Name&)Vector<Type::Name, Element, Recursive>::Init(state, v, length); } \
	static void InitScalar(State& state, Value& v, Element const& d) { Vector<Type::Name, Element, Recursive>::InitScalar(state, v, d); }\
	Name Clone(State& state) const { Name c(state, length); memcpy(c.v(), v(), length*width); return c; }
/* note missing }; */

VECTOR_IMPL(Null, unsigned char, false)  
	static Null Singleton() { static Null s = Null::c(); return s; } 
	static bool isNA() { return false; }
	static bool isCheckedNA() { return false; }
};

VECTOR_IMPL(Logical, unsigned char, false)
	static Logical True(State& state) { static Logical t = Logical::c(state, 1); return t; }
	static Logical False(State& state) { static Logical f = Logical::c(state, 0); return f; } 
	
	static bool isTrue(unsigned char c) { return c == 1; }
	static bool isFalse(unsigned char c) { return c == 0; }
	static bool isNA(unsigned char c) { return c == NAelement; }
	static bool isCheckedNA(unsigned char c) { return isNA(c); }
	static bool isNaN(unsigned char c) { return false; }
	static bool isFinite(unsigned char c) { return false; }
	static bool isInfinite(unsigned char c) { return false; }
};

VECTOR_IMPL(Integer, int64_t, false)
	static bool isNA(int64_t c) { return c == NAelement; }
	static bool isCheckedNA(int64_t c) { return isNA(c); }
	static bool isNaN(int64_t c) { return false; }
	static bool isFinite(int64_t c) { return c != NAelement; }
	static bool isInfinite(int64_t c) { return false; }
}; 

VECTOR_IMPL(Double, double, false)
	static Double Inf(State& state) { static Double i = Double::c(state, std::numeric_limits<double>::infinity()); return i; }
	static Double NInf(State& state) { static Double i = Double::c(state, -std::numeric_limits<double>::infinity()); return i; }
	static Double NaN(State& state) { static Double n = Double::c(state, std::numeric_limits<double>::quiet_NaN()); return n; } 
	
	static bool isNA(double c) { _doublena a, b; a.d = c; b.d = NAelement; return a.i==b.i; }
	static bool isCheckedNA(int64_t c) { return false; }
	static bool isNaN(double c) { return (c != c) && !isNA(c); }
	static bool isFinite(double c) { return c == c && c != std::numeric_limits<double>::infinity() && c != -std::numeric_limits<double>::infinity(); }
	static bool isInfinite(double c) { return c == std::numeric_limits<double>::infinity() || c == -std::numeric_limits<double>::infinity(); }
};

VECTOR_IMPL(Character, String, false)
	static bool isNA(String c) { return c == Strings::NA; }
	static bool isCheckedNA(String c) { return isNA(c); }
	static bool isNaN(String c) { return false; }
	static bool isFinite(String c) { return false; }
	static bool isInfinite(String c) { return false; }
};

VECTOR_IMPL(Raw, unsigned char, false) 
	static bool isNA(unsigned char c) { return false; }
	static bool isCheckedNA(unsigned char c) { return false; }
	static bool isNaN(unsigned char c) { return false; }
	static bool isFinite(unsigned char c) { return false; }
	static bool isInfinite(unsigned char c) { return false; }
};

VECTOR_IMPL(List, Value, true) 
	static bool isNA(Value const& c) { return c.isNil(); }
	static bool isCheckedNA(Value const& c) { return isNA(c); }
	static bool isNaN(Value const& c) { return false; }
	static bool isFinite(Value const& c) { return false; }
	static bool isInfinite(Value const& c) { return false; }
};

VECTOR_IMPL(Code, Instruction, false)
};

struct Future : public Value {
	static void Init(Value & f, Type::Enum typ,int64_t length, IRef ref) {
		Value::Init(f,Type::Future,length);
		f.future.ref = ref;
		f.future.typ = typ;
	}
	
	Future Clone() const { throw("shouldn't be cloning futures"); }
};


inline void walkValue(Heap* heap, Value& r) {
	switch(r.type) {
		case Type::Environment:
		case Type::Object:
		case Type::HeapObject:
			r.p = (void*)heap->mark((HeapObject*)(r.p));
			break;
		case Type::Promise:
		case Type::Function:
			r.length = ((uint64_t)heap->mark((HeapObject*)(r.length<<4)))>>4;
			r.p = (void*)heap->mark((HeapObject*)(r.p));
			break;
		case Type::Logical:
		case Type::Integer:
		case Type::Double:
		case Type::Raw:
		case Type::Character:
			if(r.length > 1)
				r.p = (void*)heap->mark((HeapObject*)(r.p));
			break;
		case Type::List:
			if(r.length > 0) {
				r.p = (void*)heap->mark((HeapObject*)(r.p));
				List l(r);
				for(int64_t i = 0; i < r.length; i++) walkValue(heap, l[i]);
			}
			break;
		case Type::Code:
			if(r.length > 0) {
				r.p = (void*)heap->mark((HeapObject*)(r.p));
			}
			break;
		default: break;
	};
}


// Object implements an immutable dictionary interface.
// Objects also have a base value which right now must be a non-object type...
//  However S4 objects can contain S3 objects so we may have to change this.
//  If we make this change, then all code that unwraps objects must do so recursively.
struct Object : public Value {

	struct Pair { String n; Value v; };

	struct Inner : public HeapObject {
		Value base;
		uint64_t capacity;
		Pair attributes[];

		virtual void walk(Heap* heap) {
			for(uint64_t i = 0; i < capacity; ++i) {
				if(attributes[i].n != Strings::NA) {
					walkValue(heap, attributes[i].v);
				}
			}
		}
	};

	// Contract: base is a non-object type.
	Value const& base() const { return ((Inner const*)p)->base; }
	Pair* attributes() { return ((Inner*)p)->attributes; }
	Pair const* attributes() const { return ((Inner const*)p)->attributes; }
	uint64_t capacity() const { return ((Inner const*)p)->capacity; }

	Object() {p = 0;}

	Object(State& state, Value base, uint64_t length, uint64_t capacity);

	uint64_t find(String s) const {
		uint64_t i = (uint64_t)s.i & (capacity()-1);	// hash this?
		while(attributes()[i].n != s && attributes()[i].n != Strings::NA) i = (i+1) & (capacity()-1);
		assert(i >= 0 && i < capacity());
		return i; 
	}
	
	static void Init(State& state, Value& v, Value _base, uint64_t capacity=4) {
		v = Object(state, _base, 0, capacity);
	}

	static void Init(State& state, Value& v, Value const& _base, Value const& _names) {
		Init(state, v, _base, 4);
		v = ((Object&)v).setNames(state, _names);
	}
	
	static void Init(State& state, Value& v, Value const& _base, Value const& _names, Value const& className) {
		Init(state, v, _base, 4);
		v = ((Object&)v).setNames(state, _names);
		v = ((Object&)v).setClass(state, className);
	}

	bool hasAttribute(String s) const {
		return attributes()[find(s)].n != Strings::NA;
	}

	bool hasNames() const { return hasAttribute(Strings::names); }
	bool hasClass() const { return hasAttribute(Strings::classSym); }
	bool hasDim() const   { return hasAttribute(Strings::dim); }

	Value const& getAttribute(String s) const {
		uint64_t i = find(s);
		if(attributes()[i].n != Strings::NA) return attributes()[i].v;
		else _error("Subscript out of range"); 
	}

	Value const& getNames() const { return getAttribute(Strings::names); }
	Value const& getClass() const { return getAttribute(Strings::classSym); }
	Value const& getDim() const { return getAttribute(Strings::dim); }

	String className() const {
		if(!hasClass()) {
			return String::Init(base().type);	// TODO: make sure types line up correctly with strings
		}
		else {
			return Character(getClass())[0];
		}
	}

	// Generate derived versions...

	Object setAttribute(State& state, String s, Value const& v) const {
		uint64_t l=0;
		
		uint64_t i = find(s);
		if(!v.isNil() && attributes()[i].n == Strings::NA) l = length+1;
		else if(v.isNil() && attributes()[i].n != Strings::NA) l = length-1;
		else l = length;

		Object out;
		if((l*2) > capacity()) {
			// going to have to rehash the result
			uint64_t c = std::max(capacity()*2ULL, 1ULL);
			out = Object(state, base(), l, c);

			// clear
			for(uint64_t j = 0; j < c; j++)
				out.attributes()[j] = (Pair) { Strings::NA, Value::Nil() };

			// rehash
			for(uint64_t j = 0; j < capacity(); j++)
				if(attributes()[j].n != Strings::NA)
					out.attributes()[out.find(attributes()[j].n)] = attributes()[j];
		}
		else {
			// otherwise, just copy straight over
			out = Object(state, base(), l, capacity());

			for(uint64_t j = 0; j < capacity(); j++)
				out.attributes()[j] = attributes()[j];
		}
		if(v.isNil())
			out.attributes()[out.find(s)] = (Pair) { Strings::NA, Value::Nil() };
		else 
			out.attributes()[out.find(s)] = (Pair) { s, v };

		return out;
	}
	
	Object setNames(State& state, Value const& v) const { return setAttribute(state, Strings::names, v); }
	Object setClass(State& state, Value const& v) const { return setAttribute(state, Strings::classSym, v); }
	Object setDim(State& state, Value const& v) const { return setAttribute(state, Strings::dim, v); }

};

inline Value CreateExpression(State& state, List const& list) {
	Value v;
	Object::Init(state, v, list, Value::Nil(), Character::c(state, Strings::Expression));
	return v;
}

inline Value CreateCall(State& state, List const& list, Value const& names = Value::Nil()) {
	Value v;
	Object::Init(state, v, list, names, Character::c(state, Strings::Call));
	return v;
}




////////////////////////////////////////////////////////////////////
// VM data structures
///////////////////////////////////////////////////////////////////


struct CompiledCall : public Value {
	
	struct Inner : public HeapObject {
		List call;
		List arguments;
		Character names;
		int64_t dots;

		Inner(List const& call, List const& arguments, Character const& names, int64_t dots)
			: call(call), arguments(arguments), names(names), dots(dots) {}
	
		virtual void walk(Heap* heap) {
			walkValue(heap, call);
			walkValue(heap, arguments);
			walkValue(heap, names);
		}
	};
	
	explicit CompiledCall(State& state, List const& call, List const& arguments, Character const& names, int64_t dots); 

	List const& call() const { return ((Inner const*)p)->call; }
	List const& arguments() const { return ((Inner const*)p)->arguments; }
	Character const& names() const { return ((Inner const*)p)->names; }
	int64_t dots() const { return ((Inner const*)p)->dots; }
};

struct Prototype : public Value {

	struct Inner : public HeapObject {
		Value expression;
		String string;
		Character parameters;
		List defaults;
		int64_t dots;

		int64_t registers;
		List constants;

		Code bc;			// bytecode
	
		Inner(Value expression, String string, Character parameters, List defaults, int64_t dots) : expression(expression), string(string), parameters(parameters), defaults(defaults), dots(dots) {}
	
		virtual void walk(Heap* heap) {
			walkValue(heap, expression);
			walkValue(heap, parameters);
			walkValue(heap, defaults);
			walkValue(heap, constants);
			walkValue(heap, bc);
		}
	};

	explicit Prototype(State& state, Value const& expression, String const& string, Character const& parameters, List const& defaults, int64_t dots);
	explicit Prototype(State& state, Value const& expression);

	explicit Prototype(Inner* inner=0) {
		Value::Init(*this, Type::HeapObject, 0);
		p = (void*)inner;
	}

	Value const& expression() const { return ((Inner const*)p)->expression; }
	String const& string() const { return ((Inner const*)p)->string; }
	Character const& parameters() const { return ((Inner const*)p)->parameters; }
	List const& defaults() const { return ((Inner const*)p)->defaults; }
	int64_t dots() const { return ((Inner const*)p)->dots; }

	int64_t& registers() { return ((Inner*)p)->registers; }
	int64_t const& registers() const { return ((Inner const*)p)->registers; }
	List& constants() { return ((Inner*)p)->constants; }
	List const& constants() const { return ((Inner const*)p)->constants; }
	Code& bc() { return ((Inner*)p)->bc; }
	Code const& bc() const { return ((Inner const*)p)->bc; }
};

/*
 * Riposte execution environments are split into two parts:
 * 1) Environment -- the static part, exposed to the R level as an R environment, these may not obey stack discipline, thus allocated on heap, try to reuse to decrease allocation overhead.
 *     -static link to enclosing environment
 *     -dynamic link to calling environment (necessary for promises which need to see original call stack)
 *     -slots storing variables that may be accessible by inner functions that may be returned 
 *		(for now, conservatively assume all variables are accessible)
 *     -names of slots
 *     -map storing overflow variables or variables that are created dynamically 
 * 		(e.g. assign() in a nested function call)
 *
 * 2) Stack Frame -- the dynamic part, not exposed at the R level, these obey stack discipline
 *     -pointer to associated Environment
 *     -pointer to Stack Frame of calling function (the dynamic link)
 *     -pointer to constant file
 *     -pointer to registers
 *     -return PC
 *     -result register pointer
 */

template<class T>
struct Ref : public HeapObject {
	T* t;

	Ref() : t(0) {}
	Ref(T* t) : t(t) {}

	virtual void walk(Heap* heap) {
		HeapObject* p = t;
		t = (T*)heap->mark(p);
	}

	T* operator->() { return t; }
	T const* operator->() const { return t; }
};

struct Environment : public HeapObject {
	static uint64_t globalRevision;
	uint64_t revision;

	Value lexical, dynamic;
	Value call;
	std::vector<String> dots;
	
	uint64_t size, load;
	struct Pair { String n; Value v; };
	Pair d[];

	explicit Environment(uint64_t size, Value lexical, Value dynamic, Value call) :
			revision(++globalRevision), lexical(lexical), dynamic(dynamic), call(call), size(size) {}

	void walk(Heap* heap) {
                lexical.p = (void*)heap->mark((HeapObject*)lexical.p);
                dynamic.p = (void*)heap->mark((HeapObject*)dynamic.p);

                for(uint64_t i = 0; i < size; i++) {
                        if(d[i].n != Strings::NA) {
				walkValue(heap, d[i].v);
                        }
                }
	}
};

class REnvironment : public Value {
public:

	static const uint64_t defaultSize = 8;

	REnvironment() {
		Value::Init(*this, Type::Environment, 0);
		this->p = 0;
	}

	REnvironment(State& state, REnvironment l, REnvironment d, Value call);

	static REnvironment Null() {
		return REnvironment();
	}

	bool isNull() { return this->p == 0; }

	explicit REnvironment(Ref<Environment>* env) {
		Value::Init(*this, Type::Environment, 0);
		this->p = env;
	}

	explicit REnvironment(Value const& v) {
		assert(v.type == Type::Environment);
		Value::Init(*this, Type::Environment, 0);
		this->p = v.p;
	}
	
	operator Value() const {
		return (Value&) *this;
	}

	// for now, do linear probing
	// this returns the location of the String s (or where it was stored before being deleted).
	// or, if s doesn't exist, the location at which s should be inserted.
	uint64_t find(String s) const {
		Ref<Environment> const& e = *(Ref<Environment> const*)p;
		uint64_t i = (uint64_t)s.i & (e->size-1);	// hash this?
		while(e->d[i].n != s && e->d[i].n != Strings::NA) i = (i+1) & (e->size-1);
		assert(i >= 0 && i < e->size);
		return i; 
	}

	Value const& get(uint64_t index) {
		Ref<Environment> const& e = *(Ref<Environment> const*)p;
		assert(index >= 0 && index < e->size);
		return e->d[index].v;
	}

	String const& getName(uint64_t index) {
		Ref<Environment> const& e = *(Ref<Environment> const*)p;
		assert(index >= 0 && index < e->size);
		return e->d[index].n;
	}

	Value const& get(String name) {
		Ref<Environment> const& e = *(Ref<Environment> const*)p;
		uint64_t i = find(name);
		if(e->d[i].n != Strings::NA) return e->d[i].v;
		else return Value::Nil();
	}

	static void rehash(State& state, REnvironment& env, uint64_t s) {
		Ref<Environment> e = *(Ref<Environment>*)env.p;

		s = nextPow2(s);
		if(s <= e->size) return; // should rehash on shrinking sometimes, when?

		uint64_t bytes = s * sizeof(Environment::Pair);
		Environment* n = new (state, bytes) Environment(s, e->lexical, e->dynamic, e->call);
		e = *(Ref<Environment>*)env.p;

		// now swap in...	
		((Ref<Environment>*)env.p)->t = n;

		env.clear();

		n->load = e->load;
		n->dots.swap(e->dots);

		// copy over previous populated values...
		for(uint64_t i = 0; i < e->size; i++) {
			if(e->d[i].n != Strings::NA) {
				n->d[env.find(e->d[i].n)] = e->d[i];
			}
		}
	}


	static uint64_t assign(State& state, REnvironment& env, String name, Value value) {
		Ref<Environment>& e = *(Ref<Environment>*)env.p;
		uint64_t i = env.find(name);
		if(value.isNil()) {
			// deleting a value changes the revision number! 
			if(e->d[i].n != Strings::NA) {
				e->load--; 
				e->d[i] = (Environment::Pair) { Strings::NA, Value::Nil() }; 
				e->revision = ++e->globalRevision; 
			}
		} else {
			if(e->d[i].n == Strings::NA) { 
				e->load++;
				if((e->load * 2) > e->size) {
					rehash(state, env, e->size * 2);
					e = *(Ref<Environment>*)env.p;
					i = env.find(name);
				}
				e->d[i] = (Environment::Pair) { name, value };
			} else {
				e->d[i].v = value;
			}
		}
		return i;
	}

	static void assign(REnvironment& env, uint64_t index, Value v) {
		Ref<Environment>& e = *(Ref<Environment>*)env.p;
		assert(index >= 0 && index < e->size);
		e->d[index].v = v;
	}

	void clear() {
		Ref<Environment>& e = *(Ref<Environment>*)p;
		e->load = 0; 
		for(uint64_t i = 0; i < e->size; i++) {
			// wiping v too makes sure we're not holding unnecessary pointers
			e->d[i] = (Environment::Pair) { Strings::NA, Value::Nil() };
		}
		e->revision = ++e->globalRevision;
	}

	REnvironment LexicalScope() const { return (REnvironment&)((*(Ref<Environment> const*)p)->lexical); }
	REnvironment DynamicScope() const { return (REnvironment&)((*(Ref<Environment> const*)p)->dynamic); }
	void SetLexicalScope(REnvironment env) { (*(Ref<Environment>*)p)->lexical = env; }
	std::vector<String>& dots() { return (*(Ref<Environment>*)p)->dots; }
	std::vector<String> const& dots() const { return (*(Ref<Environment> const*)p)->dots; }
	
	uint64_t getRevision() const { return (*(Ref<Environment> const*)p)->revision; }
	bool equalRevision(uint64_t i) const { return i == (*(Ref<Environment> const*)p)->revision; }

	struct Pointer {
		Value env;
		String name;
		uint64_t revision;
		uint64_t index;
	};

	Pointer makePointer(String name) {
		Ref<Environment>& e = *(Ref<Environment>*)p;
		uint64_t i = find(name);
		if(e->d[i].n == Strings::NA) _error("Making pointer to non-existant variable"); 
		return (Pointer) { *this, name, e->revision, i };
	}
	
	static Value const& get(Pointer const& p) {
		REnvironment& env = ((REnvironment&)p.env);
		if(env.equalRevision(p.revision)) return env.get(p.index);
		else return env.get(p.name);
	}

	static void assign(State& state, Pointer const& p, Value const& value) {
		REnvironment& env = ((REnvironment&)p.env);
		if(env.equalRevision(p.revision)) REnvironment::assign(env, p.index, value);
		else REnvironment::assign(state, env, p.name, value);
	}

	Value const& call() const { return (*(Ref<Environment> const*)p)->call; }
};

class Function : public Value {
public:
	explicit Function(Prototype proto, REnvironment env) {
		header = (int64_t)proto.p + Type::Function;
		p = env.p;
	}
	
	explicit Function(Value const& v) {
		header = v.header;
		p = v.p;
	}

	operator Value() const {
		return (Value&)*this;
	}

	Value AsPromise() const {
		Value v;
		v.header = (length<<4) + Type::Promise;
		v.p = p;
		return v;
	}

	Prototype prototype() const { return Prototype((Prototype::Inner*)(length<<4)); }
	REnvironment environment() const { return REnvironment((Ref<Environment>*)p); }
};

struct StackFrame {
	REnvironment environment;
	Prototype prototype;

	Instruction const* returnpc;
	Value* returnsp;
	Value* result;
};

#define TRACE_MAX_NODES (128)
#define TRACE_MAX_OUTPUTS (128)
#define TRACE_MAX_VECTOR_REGISTERS (32)
#define TRACE_VECTOR_WIDTH (64)
//maximum number of instructions to record before dropping out of the
//recording interpreter
#define TRACE_MAX_RECORDED (1024)

struct Trace {
	IRNode nodes[TRACE_MAX_NODES];

	size_t n_nodes;
	size_t n_pending;
	size_t n_recorded;

	int64_t length;

	struct Location {
		enum Type {REG, VAR};
		Type type;
		/*union { */ //union disabled because Pointer has a Symbol with constructor
			REnvironment::Pointer pointer; //fat pointer to environment location
			struct {
				Value * base;
				int64_t offset;
			} reg;
		/*};*/
	};

	struct Output {
		Location location; //location where an output might exist
		                   //if that location is live and contains a future then that is a live output
		Value * value; //pointer into output_values array
	};

	Output outputs[TRACE_MAX_OUTPUTS];
	size_t n_outputs;

	Value output_values[TRACE_MAX_OUTPUTS];
	size_t n_output_values;

	Value * max_live_register_base;
	int64_t max_live_register;

	bool Reserve(size_t num_nodes, size_t num_outputs) {
		if(n_pending + num_nodes >= TRACE_MAX_NODES)
			return false;
		else if(n_outputs + num_outputs >= TRACE_MAX_OUTPUTS)
			return false;
		else
			return true;
	}
	void Rollback() {
		n_pending = n_nodes;
	}
	void Commit() {
		n_nodes = n_pending;
	}
	IRef EmitBinary(IROpCode::Enum op, Type::Enum type, int64_t a, int64_t b) {
		IRNode & n = nodes[n_pending];
		n.enc = IRNode::BINARY;
		n.op = op;
		n.type = type;
		n.binary.a = a;
		n.binary.b = b;
		return n_pending++;
	}
	IRef EmitSpecial(IROpCode::Enum op, Type::Enum type, int64_t a, int64_t b) {
		IRNode & n = nodes[n_pending];
		n.enc = IRNode::SPECIAL;
		n.op = op;
		n.type = type;
		n.special.a = a;
		n.special.b = b;
		return n_pending++;
	}
	IRef EmitUnary(IROpCode::Enum op, Type::Enum type, int64_t a) {
		IRNode & n = nodes[n_pending];
		n.enc = IRNode::UNARY;
		n.op = op;
		n.type = type;
		n.unary.a = a;
		return n_pending++;
	}
	IRef EmitFold(IROpCode::Enum op, Type::Enum type, int64_t a, int64_t base) {
		IRNode & n = nodes[n_pending];
		n.enc = IRNode::FOLD;
		n.op = op;
		n.type = type;
		n.fold.a = a;
		n.fold.i = base;
		return n_pending++;
	}
	IRef EmitLoadC(Type::Enum type, int64_t c) {
		IRNode & n = nodes[n_pending];
		n.enc = IRNode::LOADC;
		n.op = IROpCode::loadc;
		n.type = type;
		n.loadc.i = c;
		return n_pending++;
	}
	IRef EmitLoadV(Type::Enum type,void * v) {
		IRNode & n = nodes[n_pending];
		n.enc = IRNode::LOADV;
		n.op = IROpCode::loadv;
		n.type = type;
		n.loadv.p = v;
		return n_pending++;
	}
	IRef EmitStoreV(Type::Enum type, Value * dst, int64_t a) {
		IRNode & n = nodes[n_pending];
		n.enc = IRNode::STORE;
		n.op = IROpCode::storev;
		n.type = type;
		n.store.a = a;
		n.store.dst = dst;
		return n_pending++;
	}
	IRef EmitStoreC(Type::Enum type, Value * dst, int64_t a) {
		IRNode & n = nodes[n_pending];
		n.enc = IRNode::STORE;
		n.op = IROpCode::storec;
		n.type = type;
		n.store.a = a;
		n.store.dst = dst;
		return n_pending++;
	}
	void EmitRegOutput(Value * base, int64_t id) {
		Trace::Output & out = outputs[n_outputs++];
		out.location.type = Location::REG;
		out.location.reg.base = base;
		out.location.reg.offset = id;
	}
	void EmitVarOutput(State & state, const REnvironment::Pointer & p) {
		Trace::Output & out = outputs[n_outputs++];
		out.location.type = Trace::Location::VAR;
		out.location.pointer = p;
	}
	void SetMaxLiveRegister(Value * base, int64_t r) {
		max_live_register_base = base;
		max_live_register = r;
	}
	void UnionWithMaxLiveRegister(Value * base, int64_t r) {
		if(base < max_live_register_base
		   || (base == max_live_register_base && r > max_live_register)) {
			SetMaxLiveRegister(base,r);
		}
	}
	void Reset();
	void InitializeOutputs(State & state);
	void WriteOutputs(State & state);
	void Execute(State & state);
	std::string toString(State & state);
private:
	void Interpret(State & state);
	void JIT(State & state);
};

//member of State, manages information for all traces
//and the currently recording trace (if any)

struct TraceState {
	TraceState() {
		active = false;
		config = DISABLED;
		verbose = false;
	}


	enum Mode {
		DISABLED,
		INTERPRET,
		COMPILE
	};
	Mode config;
	bool verbose;
	bool active;

	Trace current_trace;


	bool enabled() { return DISABLED != config; }
	bool is_tracing() const { return active; }

	Instruction const * begin_tracing(State & state, Instruction const * inst, size_t length) {
		if(active) {
			_error("recursive record\n");
		}
		current_trace.Reset();
		current_trace.length = length;
		active = true;
		return recording_interpret(state,inst);

	}

	void end_tracing(State & state) {
		if(active) {
			active = false;
			current_trace.Execute(state);
		}
	}
};

// TODO: Careful, args and result might overlap!
typedef void (*InternalFunctionPtr)(State& s, Value const* args, Value& result);

struct InternalFunction {
	InternalFunctionPtr ptr;
	int64_t params;
};

#define DEFAULT_NUM_REGISTERS 10000

struct State {
	Heap heap;
	Value* registers;
	Value* sp;
	Value* handleStack;
	Value* hsp;

	std::vector<StackFrame> stack;
	StackFrame frame;

	std::vector<REnvironment> path;
	REnvironment base, global;

	StringTable strings;
	
	std::vector<std::string> warnings;

	std::vector<InternalFunction> internalFunctions;
	std::map<String, int64_t> internalFunctionIndex;
	
	TraceState tracing; //all state related to tracing compiler


	State() : 	heap(this),
			registers(new Value[DEFAULT_NUM_REGISTERS]),
			sp(registers+DEFAULT_NUM_REGISTERS),
			handleStack(new Value[DEFAULT_NUM_REGISTERS]),
			hsp(handleStack),
			base(*this, REnvironment::Null(), REnvironment::Null(), Null::Singleton()), 
			global(*this, base, REnvironment::Null(), Null::Singleton()) {
		path.push_back(base);
	}

	~State() {
		delete [] registers;
		delete [] handleStack;
	}

	StackFrame& push() {
		stack.push_back(frame);
		return frame;
	}

	void pop() {
		frame = stack.back();
		stack.pop_back();
	}

	std::string stringify(Value const& v) const;
	std::string stringify(Trace const & t) const;
	std::string deparse(Value const& v) const;

	String internStr(std::string s) {
		return strings.in(s);
	}

	std::string externStr(String s) const {
		return strings.out(s);
	}

	void registerInternalFunction(String s, InternalFunctionPtr internalFunction, int64_t params) {
		InternalFunction i = { internalFunction, params };
		internalFunctions.push_back(i);
		internalFunctionIndex[s] = internalFunctions.size()-1;
	}

	Value* root(Value v) {
		*hsp = v;
		return hsp++;
	}

	void unroot() {
		hsp--;
	}
};

inline void* HeapObject::operator new(unsigned long bytes, State& state) {
	return state.heap.alloc(bytes);
}

inline void* HeapObject::operator new(unsigned long bytes, State& state, unsigned long extra) {
	return state.heap.varalloc(bytes+extra);
}

template<class T>
class Handle {
private:
	State& state;
	T& t;
	Handle(const Handle<T>&) {}
	void operator=(const Handle<T>&) {}
	void* operator new(size_t size) { return 0; }
	void operator delete(void* size_t) {}
public:
	Handle(State& state, T t) : state(state), t(*(T*)state.root(t)) {}
	~Handle() { state.unroot(); }
	operator T&() { return t; }
	operator T const&() const { return t; }
};

inline Object::Object(State& state, Value base, uint64_t length, uint64_t capacity) {
	Value::Init(*this, Type::Object, length);
	assert(!base.isObject());
	Inner* inner = new (state, sizeof(Pair) * capacity) Inner();
	inner->base = base;
	inner->capacity = capacity;
	for(uint64_t j = 0; j < capacity; j++)
		inner->attributes[j] = (Pair) { Strings::NA, Value::Nil() };
	p = (void*)inner;
}

inline REnvironment::REnvironment(State& state, REnvironment l, REnvironment d, Value call) {
	Value::Init(*this, Type::Environment, 0);
	uint64_t bytes = defaultSize * sizeof(Environment::Pair);
	Environment* env = new (state, bytes) Environment(defaultSize, l, d, call);
	Value v;
	v.type = Type::HeapObject;
	v.p = env;
	Handle<Value> h(state, v);
	this->p = new (state) Ref<Environment>();
	((Ref<Environment>*)(this->p))->t = (Environment*)(((Value)h).p);
	clear();
}

inline CompiledCall::CompiledCall(State& state, List const& call, List const& arguments, Character const& names, int64_t dots) {
	Value::Init(*this, Type::HeapObject, 0);
	Inner* inner = new (state) Inner(call, arguments, names, dots);
	p = (void*)inner;
}

inline Prototype::Prototype(State& state, Value const& expression, String const& string, Character const& parameters, List const& defaults, int64_t dots) {
	Value::Init(*this, Type::HeapObject, 0);
	Inner* inner = new (state) Inner(expression, string, parameters, defaults, dots);
	assert((uint64_t)inner % 16 == 0);
	p = (void*)inner;
}
inline Prototype:: Prototype(State& state, Value const& expression) {
	Value::Init(*this, Type::HeapObject, 0);
	Inner* inner = new (state) Inner(expression, Strings::empty, Character(state, 0), List(state, 0), 0);
	assert((uint64_t)inner % 16 == 0);
	p = (void*)inner;
}

template<Type::Enum VType, typename ElementType, bool Recursive, bool canPack>
inline void Vector<VType, ElementType, Recursive, canPack>::reserve(State& state, int64_t length) {
	if((canPack && length > 1) || (!canPack && length > 0)) {
		uint64_t bytes = sizeof(Element) * length;
		p = new (state, bytes) Inner(length); 
		assert((0xF & (int64_t)(((Inner*)p)->data)) == 0);
	}
}

Value eval(State& state, Function const& function);
Value eval(State& state, Prototype const prototype, REnvironment environment); 
Value eval(State& state, Prototype const prototype);
void interpreter_init(State& state);

#endif
