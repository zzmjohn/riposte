
#ifndef PARSER_H
#define PARSER_H

#include <string>
#include <stdlib.h>
#include <iostream>
#include <stack>
#include <algorithm>
#include <locale>

#include "../value.h"
#include "../frontend.h"

// trim from end
static inline std::string& rtrim(std::string &s) {
        s.erase(std::find_if(s.rbegin(), s.rend(), std::not1(std::ptr_fun<int, int>(std::isspace))).base(), s.end());
        return s;
}

static inline bool is_numeric(char a) {
    return a >= '0' && a <= '9';
}

static inline bool is_hex(char a) {
    return (a >= '0' && a <= '9') 
        || (a >= 'A' && a <= 'F')
        || (a >= 'a' && a <= 'f');
}

static inline std::string& unescape(std::string& s) {
	std::string::const_iterator i = s.begin();

	// first do fast check for any escape sequences...
	bool escaped = false;
	while(i != s.end())
		if(*i++ == '\\') escaped = true;

	if(!escaped) return s;

	std::string r;
	i = s.begin();
	while(i != s.end())
	{
		char c = *i++;
		if(c == '\\' && i != s.end())
		{
			switch(*i++) {
				case 'a': r += '\a'; break;
				case 'b': r += '\b'; break;
				case 'f': r += '\f'; break;
				case 'n': r += '\n'; break;
				case 'r': r += '\r'; break;
				case 't': r += '\t'; break;
				case 'v': r += '\v'; break;
				case 'x': {
                    if(is_hex(*i) && is_hex(*(i+1))) {
                        r += (char)(hexStrToInt(std::string(i, i+2))); i+=2;
                    } else if(is_hex(*i)) {
                        r += (char)(hexStrToInt(std::string(i, i+1))); i+=1;
                    } else
				        throw CompileError(std::string("Unrecognized hex escape in \"") + s + "\"");
                } break;
				case '\\': r += '\\'; break;
				case '"': r += '"'; break;
				case '\'': r += '\''; break;
				case ' ': r += ' '; break;
				case '\n': r += '\n'; break;
                case '0': 
                case '1': 
                case '2': 
                case '3': 
                case '4': 
                case '5': 
                case '6': 
                case '7': 
                case '8': 
                case '9': {
                    if(is_numeric(*(i-1)) && is_numeric(*(i)) && is_numeric(*(i+1))) {
                        r += (char)(octStrToInt(std::string(i-1, i+2))); i+=2;
                    } else if(is_numeric(*(i-1)) && is_numeric(*i)) {
                        r += (char)(octStrToInt(std::string(i-1, i+1))); i+=1;
                    } else if(is_numeric(*(i-1))) {
                        r += (char)(octStrToInt(std::string(i-1, i))); i+=0;
                    } else
				        throw CompileError(std::string("Unrecognized oct escape in \"") + s + "\"");
                }    break;
                case 'u': {
                    if(is_hex(*i) && is_hex(*(i+1)) && is_hex(*(i+2)) && is_hex(*(i+3))) {
                        r += (char)(hexStrToInt(std::string(i, i+3))); i+=4;
                    } else if(is_hex(*i) && is_hex(*(i+1)) && is_hex(*(i+2))) {
                        r += (char)(hexStrToInt(std::string(i, i+2))); i+=3;
                    } else if(is_hex(*(i)) && is_hex(*i+1)) {
                        r += (char)(octStrToInt(std::string(i, i+2))); i+=2;
                    } else if(is_hex(*(i))) {
                        r += (char)(octStrToInt(std::string(i, i+1))); i+=1;
                    } else
				        throw CompileError(std::string("Unrecognized multibyte escape in \"") + s + "\"");
                }    break;
				default: throw CompileError(std::string("Unrecognized escape in \"") + s + "\""); break;
			}
		}
		else r += c;
	}
	s = r;
	return s;
}

struct Parser {

	int line, col;
	State& state;
	char const* filename;
    void* pParser;
	const char *ts, *te, *le;
	
	Value result;
	int errors;
	bool complete;

	// R language needs more than 1 lookahead to resolve dangling else
	// if we see a newline, have to wait to send to parser. if next token is an else, discard newline and emit else.
	// otherwise, emit newline and next token
	bool lastTokenWasNL;

	// If we're inside parentheses or square braces, 
    //   we should discard all newlines
	// If we're in the top level or in curly braces, we have to preserve newlines
	std::stack<int> nesting;

	// To provide user with function source have to track beginning locations
	// Parser pops when function rule is reduced
	std::stack<const char*> source;
	String popSource();

	void token( int tok, Value v=Value::Nil() );
	int execute( const char* data, int len, bool isEof, Value& result, FILE* trace=NULL );
	Parser(State& state, char const* filename); 
};

struct Pairs {
    struct Pair { String n; Value v; };
	std::deque<Pair> p;
	int64_t length() const { return p.size(); }        
	void push_front(String n, Value const& v) { Pair t; t.n = n; t.v = v; p.push_front(t); } 
	void push_back(String n, Value const& v)  { Pair t; t.n = n; t.v = v; p.push_back(t); }        
	const Value& value(int64_t i) const { return p[i].v; }
	const String& name(int64_t i) const { return p[i].n; }

	List values() const {
		List l(length());
		for(int64_t i = 0; i < length(); i++)
			l[i] = value(i);
		return l;
	}

	Value names(bool forceNames) const {
		bool named = false;
		for(int64_t i = 0; i < length(); i++) {                        
			if(name(i) != Strings::empty) {
				named = true;
				break;                        
			}                
		}                
		if(named || forceNames) {
			Character n(length());                        
			for(int64_t i = 0; i < length(); i++)
				n[i] = name(i);
			return n;
		}
		else return Value::Nil();
	}
};


int parse(State& state, char const* filename,
    char const* code, size_t len, bool isEof, Value& result, FILE* trace=NULL);

#endif
