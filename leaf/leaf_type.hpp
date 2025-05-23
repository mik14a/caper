// 2008/08/15 Naoyuki Hirayama

/*!
    @file     leaf_type.hpp
    @brief    <�T�v>

    <����>
*/

#ifndef LEAF_TYPE_HPP_
#define LEAF_TYPE_HPP_

#include <string>
#include <vector>
#include <map>
#include <boost/tuple/tuple.hpp>
#include <boost/tuple/tuple_comparison.hpp>
#include "leaf_core.hpp"

namespace leaf {

class Type;

struct Slot {
	Symbol*	name;
	Type*	type;
	Slot(){}
	Slot( Symbol* x, Type* y ) { name = x; type = y; }
};


class Type {
public:
    enum Tag {
        TAG_BOOL,
        TAG_CHAR,
        TAG_SHORT,
        TAG_INT,
        TAG_LONG,
        TAG_FUNCTION,
        TAG_CLOSURE,
        TAG_TUPLE,
		TAG_STRUCT,
    };
    
public:
    static Type* getVoidType();
    static Type* getBoolType();
    static Type* getCharType();
    static Type* getShortType();
    static Type* getIntType();
    static Type* getLongType();
    static Type* getFunctionType( Type* rtypes, Type* atypes );
    static Type* getClosureType( Type* function );
    static Type* getTupleType( const std::vector< Type* >& elems );
    static Type* getElementType( Type* t, int index );
	static Type* getStructType( const std::vector< Slot >& slots ); 

    static std::string getDisplay( Type* );
    static bool isFunction( Type* );
    static bool isClosure( Type* );
	static bool isStruct( Type* );
    static bool isCallable( Type* );
    static bool isComplete( Type* );
    static int getTupleSize( Type* );
	static int getSlotCount( Type* );
    static Type* unify( Type*, Type* );
	static bool match( Type* x, Type* y );

protected:
    struct FunSig {
        Type* rtypes;
        Type* atypes;

        bool operator<( const FunSig& x ) const
        {
			return
				boost::make_tuple( rtypes, atypes ) <
				boost::make_tuple( x.rtypes, x.atypes );
		}
    };

public:
    Tag tag() { return tag_; }

    Type* getReturnType();
    Type* getArgumentType();
    Type* getElement( int index );
    Type* getRawFunc();
	int getSlotCount();
	int getSlotIndex( Symbol* );
	const Slot& getSlot( int i );
	

protected:
    Type( Tag t ) : tag_(t) {}
    Type( const FunSig& s ) : tag_(TAG_FUNCTION), funsig_(s) {}
    Type( const std::vector< Type* >& s ) : tag_(TAG_TUPLE), elems_(s) {}
    Type( const std::vector< Slot >& s ) : tag_(TAG_STRUCT), slots_(s) {}
    Type( Type* f ) : tag_(TAG_CLOSURE), rawfunc_(f) {}

private:
    Tag						tag_;
    FunSig					funsig_;
    std::vector< Type* >	elems_;
    std::vector< Slot >		slots_;
    Type*					rawfunc_;

private:
    static std::map< FunSig, Type* >                function_types_;
    static std::map< Type*, Type* >                 closure_types_;
    static std::map< std::vector< Type* >, Type* >  tuple_types_;

    static Type* normalize( Type* );

};

} // namespace leaf

#endif // LEAF_TYPE_HPPh_
