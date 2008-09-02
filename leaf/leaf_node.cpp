// 2008/08/13 Naoyuki Hirayama

#include <llvm/Module.h>
#include <llvm/DerivedTypes.h>
#include <llvm/Constants.h>
#include <llvm/Instructions.h>
#include <llvm/Bitcode/ReaderWriter.h>
#include <llvm/Assembly/Writer.h>

#include <iostream>
#include <algorithm>
#include <map>
#include <string>
#include "leaf_node.hpp"
#include "leaf_ast.hpp"
#include "leaf_error.hpp"

namespace leaf {

leaf::Symbol* intern( heap_cage& cage, SymDic& sd, const std::string& s )
{
    leaf::Symbol* sym;

    SymDic::const_iterator i = sd.find( s );
    if( i != sd.end() ) {
        sym = (*i).second;
    } else {
        sym = cage.allocate<leaf::Symbol>( s );
        sd[s] = sym;
    }
    return sym;
}

class GenSym {
public:
    GenSym() { id_ = 1; }
    ~GenSym() {}

    std::string operator()()
    {
        char buffer[256];
        sprintf( buffer, "$gensym%d", id_++ );
        return buffer;
    }

private:
    int id_;
};


template < class T >
class Environment {
public:
    typedef std::map< Symbol*, T > dic_type;

public:
    Environment(){}
    ~Environment(){}

    void push()
    {
        scope_.resize( scope_.size()+1 );
        //std::cerr << "push" << std::endl;
    }
    void pop()
    {
        scope_.pop_back();
        //std::cerr << "pop" << std::endl;
    }
    void fix()
    {
        scope_.back().modified = false;
    }
    bool modified()
    {
        return scope_.back().modified;
    }
    void update( Symbol* ident, const T& v )
    {
        //std::cerr << "update<" << scope_.size() << ">" << std::endl;
        for( int i = int(scope_.size()) - 1 ; 0 <= i ; i-- ) {
            typename dic_type::iterator j = scope_[i].m.find( ident );
            if( j != scope_[i].m.end() ) {
                if( (*j).second != v ) {
                    (*j).second = v;
                    scope_[i].modified = true;
                }
                return;
            }
        }

        scope_.back().m[ident] = v;
    }
    void bind( Symbol* ident, const T& v )
    {
        scope_.back().m[ident] = v;
    }
    T find( Symbol* ident )
    {
        //std::cerr << "find<" << scope_.size() << ">" << std::endl;
        for( int i = int(scope_.size()) - 1 ; 0 <= i ; i-- ) {
            typename dic_type::const_iterator j = scope_[i].m.find( ident );
            if( j != scope_[i].m.end() ) {
                return (*j).second;
            }
        }
        return T();
    }
    T find_in_toplevel( Symbol* ident )
    {
        typename dic_type::const_iterator j = scope_[0].m.find( ident );
        if( j != scope_[0].m.end() ) {
            return (*j).second;
        }
        return T();
    }

    void print( std::ostream& os )
    {
        for( int i = 0 ; i < int(scope_.size()) ; ++i ) {
            //os << "scope_ " << i << ":" << std::endl;
            for( typename dic_type::const_iterator j = scope_[i].m.begin() ;
                 j != scope_[i].m.end() ;
                 ++j ) {
                os << "  " << (*j).first->s << "(" << (*j).first << ")"
                   << std::endl;
            }
        }
    }

    void refer( Symbol* ident, type_t t )
    {
        // 自由変数の参照宣言
        // findと統一したほうが性能はよいが、
        // わかりやすさのため別メソッドにする

        for( int i = int(scope_.size()) - 1 ; 0 <= i ; i-- ) {
            typename dic_type::const_iterator j = scope_[i].m.find( ident );
            if( j != scope_[i].m.end() ) {
                return;
            } else {
                scope_[i].freevars[ident] = t;
            }
        }
    }

    symmap_t& freevars()
    {
        return scope_.back().freevars;
    }

private:
    struct Scope {
        std::map< Symbol*, T >  m;
        symmap_t                freevars;
        bool                    modified;
    };
    std::vector< Scope > scope_;

    void dump()
    {
        for( int i = 0 ; i < int( scope_.size() ) ; i++ ) {
            //std::cerr << "level " << (i+1) << std::endl;
            for( typename dic_type::const_iterator j = scope_[i].m.begin() ;
                 j != scope_[i].m.end() ;
                 ++j ) {
                //std::cerr << "  " << (*j).first->s << " => " << (*j).second
                //        << std::endl;
                
            }
        }

    }

};

struct Value {
    llvm::Value* v;
    leaf::Type*  t;
    symmap_t     c;

    Value() { v = 0; t = 0; }
    Value( llvm::Value* av, leaf::Type* at, const symmap_t& ac )
    {
        v = av;
        t = at;
        c = ac;
    }

    bool operator==( const Value& x )
    {
        return v == x.v && t == x.t && c == x.c;
    }
    bool operator!=( const Value& x ) { return !(*this==x); }

};

std::ostream& operator<<( std::ostream& os, const Value& v )
{
    os << "{ v = " << v.v << ", t = " << Type::getDisplay( v.t )  << " }";
    return os;
}

struct EncodeContext {
    llvm::Module*       m;
    llvm::Function*     f;
    llvm::BasicBlock*   bb;

    Environment< Value > env;
};

struct EntypeContext {
    Environment< Type* >    env;
    heap_cage*              cage;
    SymDic*                 symdic;
    GenSym                  gensym;
};

inline
void make_reg( char* s, int id )
{
    sprintf( s, "reg%d", id );
}

template < class LHS, class RHS >
inline
llvm::Value*
binary_operator(
    int                             id,
    EncodeContext&                  cc,
    LHS*                            lhs,
    RHS*                            rhs,
    llvm::Instruction::BinaryOps    op )
{
    char reg[256]; make_reg( reg, id );
    llvm::Value* v0 = lhs->encode( cc, false );
    llvm::Value* v1 = rhs->encode( cc, false );

    llvm::Instruction* inst = llvm::BinaryOperator::create(
        op, v0, v1, reg );
    cc.bb->getInstList().push_back(inst);
    return inst;
}

inline
void update_type( EntypeContext& tc, Header& h, type_t t )
{
    if( h.t != t ) { h.t = t; }
}

inline
void check_bool_expr_type( const Header& h, type_t t )
{
    if( t && t != Type::getBoolType() ) {
        throw context_mismatch( h.beg, Type::getDisplay( t ), "<bool>" );
    }
}

inline
void check_int_expr_type( const Header& h, type_t t )
{
    if( t && t != Type::getIntType() ) {
        throw context_mismatch( h.beg, Type::getDisplay( t ), "<int>" );
    }
}

template < class T > inline
void entype_int_compare( EntypeContext& tc, T& x )
{
    x.lhs->entype( tc, false, Type::getIntType() );
    x.rhs->entype( tc, false, Type::getIntType() );
    update_type( tc, x.h, Type::getBoolType() );
}

template < class T > inline
void entype_int_binary_arithmetic( EntypeContext& te, T& x )
{
    x.lhs->entype( te, false, Type::getIntType() );
    x.rhs->entype( te, false, Type::getIntType() );
    update_type( te, x.h, Type::getIntType() );
}

inline
const llvm::Type* getLLVMType( Type* t, bool add_env = false )
{
    assert( t );

    switch( t->tag() ) {
    case Type::TAG_BOOL: return llvm::Type::Int1Ty;
    case Type::TAG_CHAR: return llvm::Type::Int8Ty;
    case Type::TAG_SHORT: return llvm::Type::Int16Ty;
    case Type::TAG_INT: return llvm::Type::Int32Ty;
    case Type::TAG_LONG: return llvm::Type::Int64Ty;
    case Type::TAG_TUPLE:
        {
            int n = Type::getTupleSize( t );
            if( n == 0 ) {
                return llvm::Type::VoidTy;
            } else if( n == 1 ) {
                assert(0); // ありえない
            } else {
                std::vector< const llvm::Type* > v;
                for( int i = 0 ; i < n ; i++ ) {
                    v.push_back( getLLVMType( t->getElement(i) ) );
                }
                return llvm::StructType::get( v );
            }
        }
    case Type::TAG_FUNCTION:
        {
            Type* a = t->getArgumentType();
            int n = Type::getTupleSize( a );
            std::vector< const llvm::Type* > v;
            if( add_env ) {
                v.push_back( llvm::OpaqueType::get() );
            }
            if( 0 < n ) {
                for( int i = 0 ; i < n ; i++ ) {
                    v.push_back( getLLVMType( a->getElement( i ) ) );
                }
            }

            return llvm::FunctionType::get(
                getLLVMType( t->getReturnType() ), v, /* not vararg */ false );
        }
    case Type::TAG_CLOSURE:
        {
            
            std::vector< const llvm::Type* > v;
            v.push_back( llvm::PointerType::getUnqual(
                             getLLVMType( t->getRawFunc(), true ) ) );
            v.push_back( llvm::OpaqueType::get() );
            return llvm::PointerType::getUnqual( llvm::StructType::get( v ) );
        }
    default:
        return llvm::Type::Int32Ty;
    }
}

inline
void types_to_typevec( Types* types, typevec_t& v )
{
    for( size_t i = 0 ; i < types->v.size() ; i++ ) {
        v.push_back( types->v[i]->t );
    }
}

inline
void formalargs_to_typevec( FormalArgs* formalargs, typevec_t& v, int addr )
{
    for( size_t i = 0 ; i < formalargs->v.size() ; i++ ) {
        if( !formalargs->v[i]->t ) {
            throw inexplicit_argument_type( addr );
        }
        v.push_back( formalargs->v[i]->t->t );
    }
}

inline
void freevars_to_typevec( const symmap_t& freevars, typevec_t& v )
{
    for( symmap_t::const_iterator i = freevars.begin() ;
         i != freevars.end() ;
         ++i ) {
        v.push_back( (*i).second );
    }
}

inline llvm::Function*
encode_function(
    EncodeContext&  cc,
    bool,
    const Header&   h,
    Symbol*         funcname,
    FormalArgs*     formal_args,
    Types*          result_type,
    Block*          body,
    const symmap_t& freevars )
{
    // signature

    // ...arguments
    std::vector< const llvm::Type* > atypes;
    for( symmap_t::const_iterator i = freevars.begin() ;
         i != freevars.end() ;
         ++i ) {
        atypes.push_back( getLLVMType( (*i).second ) );
    }
    for( size_t i = 0 ; i < formal_args->v.size() ; i++ ) {
        atypes.push_back( getLLVMType( formal_args->v[i]->t->t ) );
    }

    // ...result
    typevec_t rtypes;
    types_to_typevec( result_type, rtypes );
    type_t leaf_rtype = Type::getTupleType( rtypes );
    if( Type::isFunction( leaf_rtype ) ) {
        leaf_rtype = Type::getClosureType( leaf_rtype );
    }
    const llvm::Type* rtype = getLLVMType( leaf_rtype );

    // function type
    llvm::FunctionType* ft =
        llvm::FunctionType::get(
            rtype, atypes, /* not vararg */ false );

    std::cerr << "fundef encode: " << *ft << std::endl;

    // function
    llvm::Function* f =
        llvm::Function::Create(
            ft, llvm::Function::ExternalLinkage,
            funcname->s,
            cc.m );

    //std::cerr << "fundef bind: " << h.t << std::endl;
    cc.env.bind( funcname, Value( NULL, h.t, freevars ) );

    // get actual arguments
    cc.env.push();
    {
        symmap_t::const_iterator env_iterator = freevars.begin();
        std::vector< FormalArg* >::const_iterator arg_iterator =
            formal_args->v.begin();
        for( llvm::Function::arg_iterator i = f->arg_begin();
             i != f->arg_end() ;
             ++i ) {
            if( env_iterator != freevars.end() ) {
                i->setName( "env_" + (*env_iterator).first->s );
                cc.env.bind( (*env_iterator).first,
                             Value( i, (*env_iterator).second, symmap_t() ) );
                env_iterator++;
            } else {
                i->setName( "arg_" + (*arg_iterator)->name->s->s );
                cc.env.bind( (*arg_iterator)->name->s,
                             Value( i, (*arg_iterator)->h.t, symmap_t() ) );
                arg_iterator++;
            }
        }
    }

    // basic block
    llvm::BasicBlock* bb = llvm::BasicBlock::Create("ENTRY", f);

    std::swap( cc.bb, bb );

    cc.f = f;
    llvm::Value* v = body->encode( cc, false );

    cc.env.pop();

    if( h.t->getReturnType() != Type::getVoidType() && !v ) {
        throw noreturn( h.beg, Type::getDisplay( h.t->getReturnType() ) );
    }
    cc.bb->getInstList().push_back(llvm::ReturnInst::Create(v));

    std::swap( cc.bb, bb );

    return f;
}

inline void
entype_function(
    EntypeContext&  tc,
    bool,
    type_t          t,
    Header&         h,
    Symbol*         funcname,
    FormalArgs*     formal_args,
    Types*          result_type,
    Block*          body,
    symmap_t&       freevars )
{
    // 関数定義ではコンテキスト型を無視

    // 戻り値の型
    if( !result_type ) {
        throw inexplicit_return_type( h.beg );
    }
    for( size_t i = 0 ; i < result_type->v.size() ; i++ ) {
        if( !result_type->v[i] ) {
            throw imcomplete_return_type( h.beg );
        }
    }

    typevec_t rtypes;
    types_to_typevec( result_type, rtypes );
    type_t rttype = Type::getTupleType( rtypes );
    if( Type::isFunction( rttype ) ) {
        rttype = Type::getClosureType( rttype );
    }

    // 引数の型
    typevec_t atypes;
    formalargs_to_typevec( formal_args, atypes, h.beg );
    type_t attype = Type::getTupleType( atypes );

    // 再帰関数のために本体より先にbind
    update_type( tc, h, Type::getFunctionType( rttype, attype ) );
    tc.env.bind( funcname, h.t );

    tc.env.push();
    for( size_t i = 0 ; i < formal_args->v.size() ; i++ ) {
        tc.env.bind( formal_args->v[i]->name->s, formal_args->v[i]->t->t );
    }
    tc.env.fix();

  retry:
    // 関数の戻り値になるコンテキストでは正規化
    body->entype( tc, false, rttype );

    // 環境が変更されていたらリトライ
    if( tc.env.modified() ) {
        tc.env.fix();
        goto retry;
    }

    // 自由変数
    //std::cerr << "freevars: ";
    for( symmap_t::iterator i = tc.env.freevars().begin();
         i != tc.env.freevars().end() ;
         ++i ) {
        Symbol* s = (*i).first;
        if( !tc.env.find_in_toplevel( s ) ) {
            freevars[s] = (*i).second;
            //std::cerr << (*i)->s << ", ";
        }
    }
    //std::cerr << std::endl;

    tc.env.pop();
}

////////////////////////////////////////////////////////////////
// Node
void Node::encode( llvm::Module* m )
{
    leaf::EncodeContext cc;
    cc.m = m;
    cc.f = NULL;
    cc.bb = NULL;
    cc.env.push();
    encode( cc, false );
    cc.env.pop();
}
void Node::entype( heap_cage& cage, SymDic& sd )
{
    EntypeContext tc;
    tc.cage = &cage;
    tc.symdic = &sd;
    tc.env.push();
    entype( tc, false, NULL );
    tc.env.pop();
}

////////////////////////////////////////////////////////////////
// Module
llvm::Value* Module::encode( EncodeContext& cc, bool )
{
    return topelems->encode( cc, false );
}
void Module::entype( EntypeContext& tc, bool, type_t t )
{
    topelems->entype( tc, false, t );
}

////////////////////////////////////////////////////////////////
// TopElems
llvm::Value* TopElems::encode( EncodeContext& cc, bool )
{
    for( size_t i = 0 ; i < v.size() ; i++ ) {
        v[i]->encode( cc, false );
    }
    return NULL;
}
void TopElems::entype( EntypeContext& tc, bool, type_t t )
{
    for( size_t i = 0 ; i < v.size() ; i++ ) {
        v[i]->entype( tc, false, t );
    }
}

////////////////////////////////////////////////////////////////
// TopElem
llvm::Value* TopElem::encode( EncodeContext& cc, bool )
{
    assert(0);
    return NULL;
}
void TopElem::entype( EntypeContext& tc, bool, type_t t )
{
    assert(0);
}

////////////////////////////////////////////////////////////////
// Require
llvm::Value* Require::encode( EncodeContext& cc, bool )
{
    module->encode( cc, false );
    return NULL;
}
void Require::entype( EntypeContext& tc, bool, type_t t )
{
    module->entype( tc, false, t );
}

////////////////////////////////////////////////////////////////
// TopLevelFunDecl
llvm::Value* TopLevelFunDecl::encode( EncodeContext& cc, bool )
{
    fundecl->encode( cc, false );
    return NULL;
}
void TopLevelFunDecl::entype( EntypeContext& tc, bool, type_t t )
{
    fundecl->entype( tc, false, t );
}

////////////////////////////////////////////////////////////////
// TopLevelFunDef
llvm::Value* TopLevelFunDef::encode( EncodeContext& cc, bool )
{
    fundef->encode( cc, false );
    return NULL;
}
void TopLevelFunDef::entype( EntypeContext& tc, bool, type_t t )
{
    fundef->entype( tc, false, t );
}

////////////////////////////////////////////////////////////////
// Block
llvm::Value* Block::encode( EncodeContext& cc, bool )
{
    return statements->encode( cc, false );
}
void Block::entype( EntypeContext& tc, bool, type_t t )
{
    //std::cerr << "block " << Type::getDisplay( t ) << std::endl;
    statements->entype( tc, false, t );

    if( !statements->v.empty() ) {
        update_type( tc, h, statements->v.back()->h.t );
    }
}

////////////////////////////////////////////////////////////////
// Statements
llvm::Value* Statements::encode( EncodeContext& cc, bool )
{
    llvm::Value* vv = NULL;
    for( size_t i = 0 ; i < v.size() ; i++ ) {
        vv = v[i]->encode( cc, i != v.size() - 1  );
    }
    return vv;
}
void Statements::entype( EntypeContext& tc, bool, type_t t )
{
    for( size_t i = 0 ; i < v.size() ; i++ ) {
        if( i != v.size() - 1 ) {
            v[i]->entype( tc, true, NULL );
        } else {
            v[i]->entype( tc, false, t );
        }
    }
}

////////////////////////////////////////////////////////////////
// Statement
llvm::Value* Statement::encode( EncodeContext& cc, bool )
{
    assert(0);
 return NULL;
}
void Statement::entype( EntypeContext& tc, bool, type_t t )
{
    assert(0);
}

////////////////////////////////////////////////////////////////
// FunDecl
llvm::Value* FunDecl::encode( EncodeContext& cc, bool )
{
    // signature
    std::vector< const llvm::Type* > types;
    for( size_t i = 0 ; i < sig->fargs->v.size() ; i++ ) {
        types.push_back( llvm::Type::Int32Ty );
    }

    llvm::FunctionType* ft =
        llvm::FunctionType::get(
            llvm::Type::Int32Ty, types, /* not vararg */ false );

    // function
    llvm::Function::Create(
        ft, llvm::Function::ExternalLinkage,
        sig->name->s->s,
        cc.m );

    //std::cerr << "fundecl bind: " << h.t << std::endl;
    cc.env.bind( sig->name->s, Value( NULL, h.t, symmap_t() ) );

    return NULL;
}
void FunDecl::entype( EntypeContext& tc, bool, type_t t )
{
    // 戻り値の型
    if( !sig->result_type ) {
        throw inexplicit_return_type( h.beg );
    }
    for( size_t i = 0 ; i < sig->result_type->v.size() ; i++ ) {
        if( !sig->result_type->v[i] ) {
            throw imcomplete_return_type( h.beg );
        }
    }

    typevec_t rtypes;
    for( size_t i = 0 ; i < sig->result_type->v.size() ; i++ ) {
        rtypes.push_back( sig->result_type->v[i]->t );
    }
    type_t result_type = Type::getTupleType( rtypes );

    // 引数の型
    typevec_t atypes;
    for( size_t i = 0 ; i < sig->fargs->v.size() ; i++ ) {
        if( !sig->fargs->v[i]->t ) {
            throw inexplicit_argument_type( h.beg );
        }
        atypes.push_back( sig->fargs->v[i]->t->t );
    }

    type_t args_type = Type::getTupleType( atypes );

    update_type( tc, h, Type::getFunctionType( result_type, args_type ) );
    tc.env.bind( sig->name->s, h.t );
}

////////////////////////////////////////////////////////////////
// FunDef
llvm::Value* FunDef::encode( EncodeContext& cc, bool drop_value )
{
    encode_function(
        cc,
        drop_value,
        h,
        sig->name->s,
        sig->fargs,
        sig->result_type,
        body,
        freevars );
    return NULL;
}
void FunDef::entype( EntypeContext& tc, bool drop_value, type_t t )
{
    entype_function(
        tc,
        drop_value,
        t,
        h,
        sig->name->s,
        sig->fargs,
        sig->result_type,
        body,
        freevars );
}

////////////////////////////////////////////////////////////////
// FunSig
llvm::Value* FunSig::encode( EncodeContext& cc, bool )
{
    assert(0);
    return NULL;
}
void FunSig::entype( EntypeContext& tc, bool, type_t t )
{
    assert(0);
}

////////////////////////////////////////////////////////////////
// FormalArgs
llvm::Value* FormalArgs::encode( EncodeContext& cc, bool )
{
    assert(0);
    return NULL;
}
void FormalArgs::entype( EntypeContext& tc, bool, type_t t )
{
    assert(0);
}

////////////////////////////////////////////////////////////////
// FormalArg
llvm::Value* FormalArg::encode( EncodeContext& cc, bool )
{
    assert(0);
    return NULL;
}
void FormalArg::entype( EntypeContext& tc, bool, type_t t )
{
    assert(0);
}

////////////////////////////////////////////////////////////////
// VarDecl
llvm::Value* VarDecl::encode( EncodeContext& cc, bool )
{
    Value v;
    v.v = value->encode( cc, false );
    v.t = value->h.t;
    cc.env.bind( name->s, v );
    return NULL;
}
void VarDecl::entype( EntypeContext& tc, bool drop_value, type_t )
{
    if( !drop_value ) {
        throw unused_variable( h.beg, name->s->s );
    }

    if( this->t ) {
        value->entype( tc, false, this->t->t );
    } else {
        value->entype( tc, false, NULL );
    }

    std::cerr << "VarDecl: " << name->s->s << " => "
              << Type::getDisplay( value->h.t ) << std::endl;
    tc.env.bind( name->s, value->h.t );
}

////////////////////////////////////////////////////////////////
// IfThenElse
llvm::Value* IfThenElse::encode( EncodeContext& cc, bool drop_value )
{
    llvm::Value* cond_value = cond->encode( cc, false );

    char if_r[256]; sprintf( if_r, "if_r%d", h.id );
    char if_v[256]; sprintf( if_v, "if_v%d", h.id );

    llvm::Instruction* ifresult = NULL;

    if( !drop_value ) {
        ifresult =
            new llvm::AllocaInst( 
                llvm::Type::Int32Ty,
                if_r,
                cc.bb );
    }

    char if_t_label[256]; sprintf( if_t_label, "IF_T%d", h.id );
    char if_f_label[256]; sprintf( if_f_label, "IF_F%d", h.id );
    char if_j_label[256]; sprintf( if_j_label, "IF_J%d", h.id );

    llvm::BasicBlock* tbb = llvm::BasicBlock::Create(
        if_t_label, cc.f );
    llvm::BasicBlock* fbb = llvm::BasicBlock::Create(
        if_f_label, cc.f );
    llvm::BasicBlock* jbb = llvm::BasicBlock::Create(
        if_j_label, cc.f );

    llvm::BranchInst::Create( tbb, fbb, cond_value, cc.bb );

    cc.bb = tbb;

    llvm::Value* tvalue = iftrue->encode( cc, false );
    if( ifresult ) {
        new llvm::StoreInst( 
            tvalue,
            ifresult,
            cc.bb );
    }
    llvm::BranchInst::Create( jbb, cc.bb );
    
    cc.bb = fbb;

    llvm::Value* fvalue = iffalse->encode( cc, false );
    if( ifresult ) {
        new llvm::StoreInst(
            fvalue,
            ifresult,
            cc.bb );
    }
    llvm::BranchInst::Create( jbb, cc.bb );

    cc.bb = jbb;

    llvm::Instruction* loaded_result = NULL;
    
    if( ifresult ) {
        loaded_result = new llvm::LoadInst( ifresult, if_v, cc.bb );
    }
    
    return loaded_result;
}
void IfThenElse::entype( EntypeContext& tc, bool drop_value, type_t t )
{
    cond->entype( tc, false, Type::getBoolType() );

    iftrue->entype( tc, false, t );
    iffalse->entype( tc, false, t );

    if( !drop_value && !t ) {
        if( !iftrue->h.t && !iffalse->h.t ) {

        } else if( iftrue->h.t && iffalse->h.t ) {
            if( iftrue->h.t != iffalse->h.t ) {
                throw type_mismatch(
                    h.beg,
                    Type::getDisplay( iftrue->h.t ) + " at true-clause" ,
                    Type::getDisplay( iffalse->h.t ) + " at false-clause" );
            }
        } else if( iftrue->h.t && !iffalse->h.t ) {
            iffalse->entype( tc, false, iftrue->h.t );
        } else if( !iftrue->h.t && iffalse->h.t ) {
            iftrue->entype( tc, false, iffalse->h.t );
        }
    }

    update_type( tc, h, iftrue->h.t );
}

////////////////////////////////////////////////////////////////
// Types
llvm::Value* Types::encode( EncodeContext& cc, bool )
{
    assert(0);
    return NULL;
}
void Types::entype( EntypeContext& tc, bool, type_t t )
{
    assert(0);
}

////////////////////////////////////////////////////////////////
// TypeRef
llvm::Value* TypeRef::encode( EncodeContext& cc, bool )
{
    assert(0);
    return NULL;
}
void TypeRef::entype( EntypeContext& tc, bool, type_t t )
{
    assert(0);
}

////////////////////////////////////////////////////////////////
// Expr
llvm::Value* Expr::encode( EncodeContext& cc, bool )
{
    assert(0);
    return NULL;
}
void Expr::entype( EntypeContext& tc, bool, type_t t )
{
    assert(0);
}

////////////////////////////////////////////////////////////////
// LogicalOr
llvm::Value* LogicalOr::encode( EncodeContext& cc, bool )
{
    if( v.size() == 1 ) {
        return v[0]->encode( cc, false );
    }

    // 結果レジスタ
    char label[256];
    sprintf( label, "or_s%d_result", h.id );
    llvm::Instruction* orresult =
        new llvm::AllocaInst( 
            llvm::Type::Int1Ty,
            label,
            cc.bb );

    // 式の数だけBasicBlockを作成(最後のブロックは失敗ブロック)
    std::vector< llvm::BasicBlock* > bb;
    for( size_t i = 0 ; i < v.size() ; i++ ) {
        sprintf( label, "or_s%d_%d", h.id, int(i) );
        bb.push_back( llvm::BasicBlock::Create( label, cc.f ) );
    }

    // 失敗ブロック
    new llvm::StoreInst( llvm::ConstantInt::getFalse(), orresult, bb.back() );

    // 成功ブロック
    sprintf( label, "or_s%d_ok", h.id );
    llvm::BasicBlock* success = llvm::BasicBlock::Create( label, cc.f );
    new llvm::StoreInst( llvm::ConstantInt::getTrue(), orresult, success );

    // 最終ブロック
    sprintf( label, "or_s%d_final", h.id );
    llvm::BasicBlock* final = llvm::BasicBlock::Create( label, cc.f );
    llvm::BranchInst::Create( final, bb.back() );
    llvm::BranchInst::Create( final, success );

    for( size_t i = 0 ; i < v.size() ; i++ ) {
        llvm::Value* vv = v[i]->encode( cc, false );
        
        llvm::BranchInst::Create( success, bb[i], vv, cc.bb );
        cc.bb = bb[i];
    }

    cc.bb = final;
    sprintf( label, "or_s%d_value", h.id );
    return new llvm::LoadInst( orresult, label, final );
}
void LogicalOr::entype( EntypeContext& tc, bool, type_t t )
{
    if( v.size() == 1 ) {
        v[0]->entype( tc, false, t );
        h.t = v[0]->h.t;
    } else {
        check_bool_expr_type( h, t );
        for( size_t i = 0 ; i < v.size(); i++ ) {
            v[i]->entype( tc, false, Type::getBoolType() );
        }
    }
}

////////////////////////////////////////////////////////////////
// LogicalAnd
llvm::Value* LogicalAnd::encode( EncodeContext& cc, bool )
{
    if( v.size() == 1 ) {
        return v[0]->encode( cc, false );
    }

    // 結果レジスタ
    char label[256];
    sprintf( label, "and_s%d_result", h.id );
    llvm::Instruction* andresult =
        new llvm::AllocaInst( 
            llvm::Type::Int1Ty,
            label,
            cc.bb );

    // 式の数だけBasicBlockを作成(最後のブロックは成功ブロック)
    std::vector< llvm::BasicBlock* > bb;
    for( size_t i = 0 ; i < v.size() ; i++ ) {
        sprintf( label, "and_s%d_%d", h.id, int(i) );
        bb.push_back( llvm::BasicBlock::Create( label, cc.f ) );
    }

    // 成功ブロック
    new llvm::StoreInst( llvm::ConstantInt::getTrue(), andresult, bb.back() );

    // 失敗ブロック
    sprintf( label, "and_s%d_ng", h.id );
    llvm::BasicBlock* failure = llvm::BasicBlock::Create( label, cc.f );
    new llvm::StoreInst( llvm::ConstantInt::getFalse(), andresult, failure );

    // 最終ブロック
    sprintf( label, "and_s%d_final", h.id );
    llvm::BasicBlock* final = llvm::BasicBlock::Create( label, cc.f );
    llvm::BranchInst::Create( final, bb.back() );
    llvm::BranchInst::Create( final, failure );

    for( size_t i = 0 ; i < v.size() ; i++ ) {
        llvm::Value* vv = v[i]->encode( cc, false );
        
        llvm::BranchInst::Create( bb[i], failure, vv, cc.bb );
        cc.bb = bb[i];
    }

    cc.bb = final;
    sprintf( label, "and_s%d_value", h.id );
    return new llvm::LoadInst( andresult, label, final );
}
void LogicalAnd::entype( EntypeContext& tc, bool, type_t t )
{
    if( v.size() == 1 ) {
        v[0]->entype( tc, false, t );
        h.t = v[0]->h.t;
    } else {
        check_bool_expr_type( h, t );
        for( size_t i = 0 ; i < v.size(); i++ ) {
            v[i]->entype( tc, false, Type::getBoolType() );
        }
    }
}

////////////////////////////////////////////////////////////////
// Equality
llvm::Value* Equality::encode( EncodeContext& cc, bool )
{
    assert(0);
    return NULL;
}
void Equality::entype( EntypeContext& tc, bool, type_t t )
{
    assert(0);
}

////////////////////////////////////////////////////////////////
// EqualityEq
llvm::Value* EqualityEq::encode( EncodeContext& cc, bool )
{
    return 
        new llvm::ICmpInst(
            llvm::ICmpInst::ICMP_EQ,
            lhs->encode( cc, false ),
            rhs->encode( cc, false ),
            "eq",
            cc.bb );
}
void EqualityEq::entype( EntypeContext& tc, bool, type_t t )
{
    check_bool_expr_type( h, t );
    entype_int_compare( tc, *this );
}

////////////////////////////////////////////////////////////////
// EqualityNe
llvm::Value* EqualityNe::encode( EncodeContext& cc, bool )
{
    return 
        new llvm::ICmpInst(
            llvm::ICmpInst::ICMP_NE,
            lhs->encode( cc, false ),
            rhs->encode( cc, false ),
            "ne",
            cc.bb );
}
void EqualityNe::entype( EntypeContext& tc, bool, type_t t )
{
    check_bool_expr_type( h, t );
    entype_int_compare( tc, *this );
}

////////////////////////////////////////////////////////////////
// Relational
llvm::Value* Relational::encode( EncodeContext& cc, bool )
{
    assert(0);
    return NULL;
}
void Relational::entype( EntypeContext& tc, bool, type_t t )
{
    assert(0);
}

////////////////////////////////////////////////////////////////
// RelationalLt
llvm::Value* RelationalLt::encode( EncodeContext& cc, bool )
{
    return 
        new llvm::ICmpInst(
            llvm::ICmpInst::ICMP_SLT,
            lhs->encode( cc, false ),
            rhs->encode( cc, false ),
            "lt",
            cc.bb );
}
void RelationalLt::entype( EntypeContext& tc, bool, type_t t )
{
    check_bool_expr_type( h, t );
    entype_int_compare( tc, *this );
}

////////////////////////////////////////////////////////////////
// RelationalGt
llvm::Value* RelationalGt::encode( EncodeContext& cc, bool )
{
    return 
        new llvm::ICmpInst(
            llvm::ICmpInst::ICMP_SGT,
            lhs->encode( cc, false ),
            rhs->encode( cc, false ),
            "gt",
            cc.bb );
}
void RelationalGt::entype( EntypeContext& tc, bool, type_t t )
{
    check_bool_expr_type( h, t );
    entype_int_compare( tc, *this );
}

////////////////////////////////////////////////////////////////
// RelationalLe
llvm::Value* RelationalLe::encode( EncodeContext& cc, bool )
{
    return 
        new llvm::ICmpInst(
            llvm::ICmpInst::ICMP_SLE,
            lhs->encode( cc, false ),
            rhs->encode( cc, false ),
            "le",
            cc.bb );
}
void RelationalLe::entype( EntypeContext& tc, bool, type_t t )
{
    check_bool_expr_type( h, t );
    entype_int_compare( tc, *this );
}

////////////////////////////////////////////////////////////////
// RelationalGe
llvm::Value* RelationalGe::encode( EncodeContext& cc, bool )
{
    return 
        new llvm::ICmpInst(
            llvm::ICmpInst::ICMP_SGE,
            lhs->encode( cc, false ),
            rhs->encode( cc, false ),
            "ge",
            cc.bb );
}
void RelationalGe::entype( EntypeContext& tc, bool, type_t t )
{
    check_bool_expr_type( h, t );
    entype_int_compare( tc, *this );
}

////////////////////////////////////////////////////////////////
// Additive
llvm::Value* Additive::encode( EncodeContext& cc, bool )
{
    assert(0);
    return NULL;
}
void Additive::entype( EntypeContext& tc, bool, type_t t )
{
    assert(0);
}

////////////////////////////////////////////////////////////////
// AddExpr
llvm::Value* AddExpr::encode( EncodeContext& cc, bool )
{
    return binary_operator( h.id, cc, lhs, rhs, llvm::Instruction::Add );
}
void AddExpr::entype( EntypeContext& tc, bool, type_t t )
{
    check_int_expr_type( h, t );
    entype_int_binary_arithmetic( tc, *this );
}

////////////////////////////////////////////////////////////////
// SubExpr
llvm::Value* SubExpr::encode( EncodeContext& cc, bool )
{
    return binary_operator( h.id, cc, lhs, rhs, llvm::Instruction::Sub );
}
void SubExpr::entype( EntypeContext& tc, bool, type_t t )
{
    check_int_expr_type( h, t );
    entype_int_binary_arithmetic( tc, *this );
}

////////////////////////////////////////////////////////////////
// Multiplicative
llvm::Value* Multiplicative::encode( EncodeContext& cc, bool )
{
    assert(0);
    return NULL;
}
void Multiplicative::entype( EntypeContext& tc, bool, type_t t )
{
    assert(0);
}

////////////////////////////////////////////////////////////////
// MulExpr
llvm::Value* MulExpr::encode( EncodeContext& cc, bool )
{
    return binary_operator( h.id, cc, lhs, rhs, llvm::Instruction::Mul );
}
void MulExpr::entype( EntypeContext& tc, bool, type_t t )
{
    check_int_expr_type( h, t );
    entype_int_binary_arithmetic( tc, *this );
}

////////////////////////////////////////////////////////////////
// DivExpr
llvm::Value* DivExpr::encode( EncodeContext& cc, bool )
{
    return binary_operator( h.id, cc, lhs, rhs, llvm::Instruction::SDiv );
}
void DivExpr::entype( EntypeContext& tc, bool, type_t t )
{
    check_int_expr_type( h, t );
    entype_int_binary_arithmetic( tc, *this );
}

////////////////////////////////////////////////////////////////
// PrimExpr
llvm::Value* PrimExpr::encode( EncodeContext& cc, bool )
{
    assert(0);
    return NULL;
}
void PrimExpr::entype( EntypeContext& tc, bool, type_t t )
{
    assert(0);
}

////////////////////////////////////////////////////////////////
// LiteralBoolean
llvm::Value* LiteralBoolean::encode( EncodeContext& cc, bool )
{
    return value ?
        llvm::ConstantInt::getTrue() :
        llvm::ConstantInt::getFalse();
}
void LiteralBoolean::entype( EntypeContext& tc, bool, type_t t )
{
    if( t && t != Type::getBoolType() ) {
        throw context_mismatch( h.beg, "<bool>", Type::getDisplay( t ) );
    }
    update_type( tc, h, Type::getBoolType() );
}

////////////////////////////////////////////////////////////////
// LiteralInteger
llvm::Value* LiteralInteger::encode( EncodeContext& cc, bool )
{
    return llvm::ConstantInt::get( llvm::Type::Int32Ty, value );
}
void LiteralInteger::entype( EntypeContext& tc, bool, type_t t )
{
    if( t && t != Type::getIntType() ) {
        throw context_mismatch( h.beg, "<int>", Type::getDisplay( t ) );
    }
    update_type( tc, h, Type::getIntType() );
}

////////////////////////////////////////////////////////////////
// LiteralChar
llvm::Value* LiteralChar::encode( EncodeContext& cc, bool )
{
    return llvm::ConstantInt::get( llvm::Type::Int32Ty, value );
}
void LiteralChar::entype( EntypeContext& tc, bool, type_t t )
{
    if( t && t != Type::getIntType() ) {
        throw context_mismatch( h.beg, "<int>", Type::getDisplay( t ) );
    }
    update_type( tc, h, Type::getIntType() );
}

////////////////////////////////////////////////////////////////
// VarRef
llvm::Value* VarRef::encode( EncodeContext& cc, bool )
{
    Value v = cc.env.find( name->s );;
    if( !v.v ) {
        //cc.print( std::cerr );
        throw no_such_variable( h.beg, name->s->s );
    }

    return v.v;
}
void VarRef::entype( EntypeContext& tc, bool, type_t t )
{
    type_t vt = tc.env.find( name->s );

    if( vt ) {
        // すでに変数の型が決まっている
        if( t ) {
            // コンテキスト型がanyでない
            if( vt != t ) {
                throw context_mismatch(
                    h.beg, Type::getDisplay( vt ), Type::getDisplay( t ) );
            }
        }
        update_type( tc, h, vt );
    } else {
        // 変数の型がまだ決まってない
        if( t ) {
            // コンテキスト型が決まっている
            tc.env.update( name->s, t );
            update_type( tc, h, t );
        }
    }        

    tc.env.refer( name->s, h.t );
}

////////////////////////////////////////////////////////////////
// Parenthized
llvm::Value* Parenthized::encode( EncodeContext& cc, bool )
{
    return expr->encode( cc, false );
}
void Parenthized::entype( EntypeContext& tc, bool, type_t t )
{
    expr->entype( tc, false, t );
    update_type( tc, h, expr->h.t );
}

////////////////////////////////////////////////////////////////
// CastExpr
llvm::Value* CastExpr::encode( EncodeContext& cc, bool )
{
    assert(0);
    return NULL;
}
void CastExpr::entype( EntypeContext& tc, bool, type_t t )
{
    assert(0);
}

////////////////////////////////////////////////////////////////
// Cast
llvm::Value* Cast::encode( EncodeContext& cc, bool )
{
    char reg[256];
    sprintf( reg, "cast%d", h.id );

    return llvm::CastInst::createIntegerCast(
        expr->encode( cc, false ),
        getLLVMType( this->t->t ),
        true,
        reg,
        cc.bb );
}
void Cast::entype( EntypeContext& tc, bool, type_t t )
{
    if( t != this->t->t ) {
        throw context_mismatch(
            h.beg,
            Type::getDisplay( this->t->t ),
            Type::getDisplay( t ) );
    }
    expr->entype( tc, false, NULL );
    update_type( tc, h, this->t->t );
}

////////////////////////////////////////////////////////////////
// FunCall
llvm::Value* FunCall::encode( EncodeContext& cc, bool )
{
    Value v = cc.env.find( func->s );
    //std::cerr << "funcall: " << func->s->s << std::endl;
    if( !v.t ) {
        throw no_such_function( h.beg, func->s->s );
    }

    std::cerr << func->s->s << ": " << Type::getDisplay( v.t ) << std::endl;
    if( Type::isFunction( v.t ) ) {
        llvm::Function* f = cc.m->getFunction( func->s->s );

        std::cerr << "funcall type(" << func->s->s << "): "
                  << *f->getType() << std::endl;

        std::vector< llvm::Value* > args;
        for( symmap_t::const_iterator i = v.c.begin() ;
             i != v.c.end() ;
             ++i ) {
            Value vv = cc.env.find( (*i).first );
            assert( vv.v );
            //std::cerr << "env: " << vv.v << std::endl;
            args.push_back( vv.v );
        }
        for( size_t i = 0 ; i < aargs->v.size() ; i++ ) {
            llvm::Value* vv = aargs->v[i]->encode( cc, false );
            assert( vv );
            //std::cerr << "arg: " << vv << std::endl;
            args.push_back( vv );
        }

        char reg[256];
        sprintf( reg, "ret%d", h.id );

        //std::cerr << "args: " << args.size() << std::endl;

        return llvm::CallInst::Create(
            f, args.begin(), args.end(), reg, cc.bb );
    } else if( Type::isClosure( v.t ) ) {
        char reg[256];
        sprintf( reg, "stub_f%d", h.id );

        std::cerr << "closure call: " << *v.v->getType() << std::endl;
        
        llvm::Value* indices[2];
        indices[0] = llvm::ConstantInt::get( llvm::Type::Int32Ty, 0 );
        indices[1] = llvm::ConstantInt::get( llvm::Type::Int32Ty, 0 );
        llvm::Value* f = llvm::GetElementPtrInst::Create(
            v.v, indices, indices+2, reg, cc.bb );
        std::cerr << "closure fun: " << *f->getType() << std::endl;

        sprintf( reg, "stub_env%d", h.id );

        indices[1] = llvm::ConstantInt::get( llvm::Type::Int32Ty, 1 );
        llvm::Value* env = llvm::GetElementPtrInst::Create(
            v.v, indices, indices+2, reg, cc.bb );
        std::cerr << "closure env: " << *env->getType() << std::endl;
        
        std::vector< llvm::Value* > args;
        args.push_back( env );
        for( size_t i = 0 ; i < aargs->v.size() ; i++ ) {
            llvm::Value* vv = aargs->v[i]->encode( cc, false );
            assert( vv );
            //std::cerr << "arg: " << vv << std::endl;
            args.push_back( vv );
        }
        
        sprintf( reg, "ret%d", h.id );

        return llvm::CallInst::Create(
            f, args.begin(), args.end(), reg, cc.bb );
    } else {
        //std::cerr << Type::getDisplay( v.t ) << std::endl;
        assert(0);
    }

}
void FunCall::entype( EntypeContext& tc, bool, type_t t )
{
    type_t ft = tc.env.find( func->s );
    std::cerr << "Funcall::entype " << func->s->s << " => "
              << Type::getDisplay( ft->getReturnType() ) << std::endl;
    if( !Type::isCallable( ft ) ) {
        throw uncallable(
            h.beg, func->s->s + "(" + Type::getDisplay( ft ) + ")" );
    }

    // 戻り値とコンテキスト型がミスマッチ
    if( t && ft->getReturnType() != t ) {
        throw context_mismatch(
            h.beg,
            func->s->s + "(" + Type::getDisplay( ft->getReturnType() ) + ")",
            Type::getDisplay( t ) );
    }

    // 引数の個数が合わない
    int farity = Type::getTupleSize( ft->getArgumentType() );
    if( int( aargs->v.size() ) != farity ) {
        throw wrong_arity(
            h.beg,
            aargs->v.size(),
            farity,
            func->s->s );
    }

    // 実引数の型付け
    for( size_t i = 0 ; i < aargs->v.size() ; i++ ) {
        aargs->v[i]->entype(
            tc, false, ft->getArgumentType()->getElement(i) );
    }

    update_type( tc, h, ft->getReturnType() );

    tc.env.refer( func->s, h.t );
}

////////////////////////////////////////////////////////////////
// Lambda
llvm::Value* Lambda::encode( EncodeContext& cc, bool drop_value )
{
    // lambda-lifted functionの作成
    llvm::Function* raw_function = encode_function(
        cc,
        drop_value,
        h,
        name,
        fargs,
        result_type,
        body,
        freevars );

    // closure-env型の作成
    std::vector< const llvm::Type* > v;
    for( symmap_t::const_iterator i = freevars.begin() ;
         i != freevars.end() ;
         ++i ) {
        v.push_back( getLLVMType( (*i).second ) );
    }
    llvm::Type* closure_env_type = llvm::StructType::get( v );
    std::cerr << "closure_env_type: " << *closure_env_type << std::endl;

    // stub function型の作成
    
    // ...arguments
    std::vector< const llvm::Type* > atypes;
    atypes.push_back( llvm::PointerType::getUnqual( closure_env_type ) );
    for( size_t i = 0 ; i < fargs->v.size() ; i++ ) {
        atypes.push_back( getLLVMType( fargs->v[i]->t->t ) );
    }

    // ...result
    const llvm::Type* rtype = raw_function->getReturnType();

    // ...function type
    llvm::FunctionType* stub_ft =
        llvm::FunctionType::get(
            rtype, atypes, /* not vararg */ false );
    std::cerr << "stub_ft: " << *stub_ft << std::endl;

    // closure型の作成
    v.clear();
    v.push_back( llvm::PointerType::getUnqual( stub_ft ) );
    v.push_back( closure_env_type );
    llvm::Type* closure_type = llvm::StructType::get( v );
    std::cerr << "closure_type: " << *closure_type << std::endl;

    // ...function
    llvm::Function* stub_f =
        llvm::Function::Create(
            stub_ft,
            llvm::Function::ExternalLinkage,
            name->s,
            cc.m );
    
    // ...basic block
    llvm::BasicBlock* bb = llvm::BasicBlock::Create("ENTRY", stub_f);

    // ... stub => raw arguments
    llvm::Function::arg_iterator ai = stub_f->arg_begin();
    std::vector< llvm::Value* > args;

    llvm::Value* cls = ai++;

    char reg[256];
    sprintf( reg, "closure%d", h.id );

    llvm::Value* index = llvm::ConstantInt::get( llvm::Type::Int32Ty, 1 );
    llvm::Value* env = llvm::GetElementPtrInst::Create(
        cls, index, reg, bb );

    for( size_t i = 0 ; i < freevars.size() ; i++ ) {
        char label[256];
        sprintf( label, "freevar_a%d_%d", h.id, int(i) );

        llvm::Value* index = llvm::ConstantInt::get( llvm::Type::Int32Ty, i );
        llvm::Value* fv = llvm::GetElementPtrInst::Create(
            env, index, label, bb );

        sprintf( label, "freevar_v%d_%d", h.id, int(i) );

        llvm::Value* v = new llvm::LoadInst( fv, label, bb );
        args.push_back( v );
    }
    for( llvm::Function::arg_iterator i = ai; i != stub_f->arg_end() ; ++i ) {
        args.push_back( i );
    }
#if 0
    llvm::Value* stub_v = llvm::CallInst::Create(
        raw_function, args.begin(), args.end(), reg, bb );

    bb->getInstList().push_back( llvm::ReturnInst::Create(stub_v) );
#else
    bb->getInstList().push_back( llvm::ReturnInst::Create( index ) );
#endif


    char closure_name[256];
    sprintf( closure_name, "closure_%d", h.id );

    llvm::Value* closure =
        new llvm::MallocInst( 
            closure_type,
            closure_name,
            cc.bb );

    return closure;
}
void Lambda::entype( EntypeContext& tc, bool drop_value, type_t t )
{
    if( !name ) {
        name = intern( *tc.cage, *tc.symdic, tc.gensym() );
    }

    entype_function(
        tc,
        drop_value,
        t,
        h,
        name,
        fargs,
        result_type,
        body,
        freevars );

    assert( Type::isFunction( h.t ) );

    update_type( tc, h, Type::getClosureType( h.t ) );
}

////////////////////////////////////////////////////////////////
// ActualArgs
llvm::Value* ActualArgs::encode( EncodeContext& cc, bool )
{
    assert(0);
    return NULL;
}
void ActualArgs::entype( EntypeContext& tc, bool, type_t t )
{
    assert(0);
}

////////////////////////////////////////////////////////////////
// ActualArg
llvm::Value* ActualArg::encode( EncodeContext& cc, bool )
{
    return expr->encode( cc, false );
}
void ActualArg::entype( EntypeContext& tc, bool, type_t t )
{
    expr->entype( tc, false, t );
    update_type( tc, h, expr->h.t );
}

////////////////////////////////////////////////////////////////
// Identifier
llvm::Value* Identifier::encode( EncodeContext& cc, bool )
{
    assert(0);
    return NULL;
}
void Identifier::entype( EntypeContext& tc, bool, type_t t )
{
    assert(0);
}

} // namespace leaf
