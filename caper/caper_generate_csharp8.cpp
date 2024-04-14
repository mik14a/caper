// Copyright (C) 2008 Naoyuki Hirayama.
// All Rights Reserved.

// $Id$

#include "caper_ast.hpp"
#include "caper_generate_cpp.hpp"
#include "caper_format.hpp"
#include "caper_stencil.hpp"
#include "caper_finder.hpp"
#include <algorithm>
#include <boost/tuple/tuple.hpp>
#include <boost/tuple/tuple_comparison.hpp>

namespace {

struct semantic_action_entry {
    std::string              name;
    std::vector<std::string> args;

    bool operator<(const semantic_action_entry& x) const {
        if (name < x.name) { return true; }
        if (x.name < name) { return false; }
        return args < x.args;
    }
};


std::string make_type_name(const Type& x, const std::string& smart_pointer_tag) {
    std::string type;
    if (smart_pointer_tag.empty())
        type = x.name;
    else
        type = smart_pointer_tag + "<" + x.name + ">";

    switch(x.extension) {
        case Extension::None:
            return type;
        case Extension::Star:
        case Extension::Plus:
        case Extension::Slash:
            return "Sequence<" + type + ">";
        case Extension::Question:
            return "Optional<" + type + ">";
        default:
            assert(0);
            return "";
    }
}

std::string make_arg_decl(const Type& x, size_t l, const std::string& smart_pointer_tag) {
    std::string sl = std::to_string(l);
    std::string y = make_type_name(x, smart_pointer_tag) + " arg" + sl;
    switch (x.extension) {
        case Extension::None:
            assert(0);
            return "";
        case Extension::Star:
        case Extension::Plus:
        case Extension::Question:
        case Extension::Slash:
            return
                y + "(sa_, stack_, seq_get_range(base, arg_index" + sl + "))";
        default:
            assert(0);
            return "";
    }
}

void make_signature(
    const std::map<std::string, Type>&      nonterminal_types,
    const tgt::parsing_table::rule_type&    rule,
    const SemanticAction&                   sa,
    std::vector<std::string>&               signature,
    const std::string&                      smart_pointer_tag) {
    // function name
    signature.push_back(sa.name);

    // return value
    signature.push_back(
        make_type_name(*finder(nonterminal_types, rule.left().name()),
                       smart_pointer_tag));

    // arguments
    for (const auto& arg: sa.args) {
        signature.push_back(make_type_name(arg.type, smart_pointer_tag));
    }
}

std::string normalize_internal_sa_name(const std::string& s) {
    std::string r;
    for(auto c: s) {
        if (c == '<' || c == '>') {
            r += "__";
        } else {
            r += c;
        }
    }
    return r;
}

std::string normalize_sa_call(const std::string& s) {
    std::string prefix;
    for(auto c: s) {
        if (c == '<' || c == '>') {
            prefix = "template ";
            break;
        }
    }
    return prefix + s;
}

} // unnamed namespace

void generate_csharp8(
    const std::string&                  src_filename,
    std::ostream&                       os,
    const GenerateOptions&              options,
    const std::map<std::string, Type>&  terminal_types,
    const std::map<std::string, Type>&  nonterminal_types,
    const std::vector<std::string>&     tokens,
    const action_map_type&              actions,
    const tgt::parsing_table&           table) {

#ifdef _WIN32
    char basename[_MAX_PATH];
    char extension[_MAX_PATH];
    _splitpath(src_filename.c_str(), NULL, NULL, basename, extension);
    std::string filename = std::string(basename)+ extension;
# else
    std::string filename = src_filename;
#endif

    std::string headername = filename;
    for (auto& x: headername){
        if (!isalpha(x) && !isdigit(x)) {
            x = '_';
        } else {
            x = toupper(x);
        }
    }

    // once header / notice / URL / includes / namespace header
    stencil(
        os, R"(
// This file was automatically generated by Caper.
// (http://jonigata.github.io/caper/caper.html)

using System.Diagnostics;

namespace ${namespace_name}
{
)",
        {"namespace_name", options.namespace_name}
    );

    if (!options.external_token) {
        // token enumeration
        stencil(
            os, R"(
    enum Token {
$${tokens}
    }

)",
            { "tokens", [&](std::ostream& os) {
                for(const auto& token: tokens) {
                    stencil(
                        os, R"(
        ${prefix}${token},
)",
                        {"prefix", options.token_prefix},
                        {"token", token}
                    );
                }
            }}
        );
    }

    // stack class header
    stencil(
        os, R"(
    class Stack<T>
    {
        public Stack(uint stackSize) { _stackSize = stackSize; }

        public void RollbackTmp() {
            _gap = _stack.Count;
            _tmp.Clear();
        }

        public void CommitTmp() {
            _stack.RemoveRange(_gap, _stack.Count - _gap);
            _stack.AddRange(_tmp);
            _tmp.Clear();
        }

        public bool Push(T value) {
            if (_stackSize != 0 && _stackSize <= _stack.Count + _tmp.Count) {
                return false;
            }
            _tmp.Add(value);
            return true;
        }

        public void Pop(int count) {
            if (_tmp.Count < count) {
                _tmp.Clear();
                _gap -= count - _tmp.Count;
            } else {
                _tmp.RemoveRange(_tmp.Count - count, count);
            }
        }

        public T Top() {
            Debug.Assert(0 < Depth());
            return this[Depth() - 1];
        }

        public T this[int index] {
            get => _gap <= index ? _tmp[index - _gap] : _stack[index];
            private set {
                if (_gap <= index) {
                    _tmp[index - _gap] = value;
                } else {
                    _stack[index] = value;
                }
            }
        }

        public T this[int @base, int index] => this[@base - index];

        public void Clear() {
            _stack.Clear();
            _tmp.Clear();
            _gap = 0;
        }

        public bool Empty() {
            return _tmp.Count == 0 && _gap == 0;
        }

        public int Depth() {
            return _gap + _tmp.Count;
        }

        public void SwapTopAndSecond() {
            var d = Depth();
            Debug.Assert(2 <= d);
            (this[d - 2], this[d - 1]) = (this[d - 1], this[d - 2]);
        }

        readonly uint _stackSize;
        readonly List<T> _stack = new List<T>();
        readonly List<T> _tmp = new List<T>();
        int _gap = 0;
    }

)");

    // semantic action interface
    std::set<semantic_action_entry> ss;
    for (auto it = actions.begin(); it != actions.end(); ++it) {
        const tgt::parsing_table::rule_type& rule = it->first;
        const SemanticAction& sa = it->second;
        semantic_action_entry sae;
        sae.name = sa.name;
        // 1st argument = out parameter
        sae.args.push_back((*nonterminal_types.find(rule.left().name())).second.name);
        for (size_t l = 0; l < sa.args.size(); l++) {
            sae.args.push_back(sa.args[l].type.name);
        }
        ss.insert(sae);
    }

    stencil(os, R"(
    interface ISemanticAction
    {
        void SyntaxError();
        void StackOverflow();
$${methods}
    }

)",
        { "methods", [&](std::ostream& os) {
            for (auto it = ss.begin(); it != ss.end(); ++it) {
                std::stringstream st;
                st << "void " << (*it).name << "(out ";
                bool first = true;
                for (size_t l = 0; l < (*it).args.size(); l++) {
                    if (first) { first = false; } else { st << ", "; }
                    st << ((*it).args[l]) << " " << "arg" << l;
                }
                st << ");";
                stencil(
                    os, R"(
        ${method}
)",
                    { "method", st.str() }
                );
            }
        }}
    );

    // parser class header
    stencil(
        os, R"(
    class Parser<${token_parameter}TValue>
    {
        enum NonTerminal {
)",
        {"token_parameter", options.external_token ? "TToken, " : ""}
    );

    for (const auto& nonterminal_type: nonterminal_types) {
        stencil(
            os, R"(
            NonTerminal_${nonterminal_name},
)",
            {"nonterminal_name", nonterminal_type.first}
        );
    }

    stencil(
        os, R"(
        }

        public Parser(ISemanticAction sa, uint stackSize) {
            _stack = new Stack<StackFrame>(stackSize);
            _action = sa;
            Reset();
        }

        public void Reset() {
            _error = false;
            _accepted = false;
            ClearStack();
            RollbackTmpStack();
            if (PushStack(0, default)) {
                CommitTmpStack();
            } else {
                _action.StackOverflow();
                _error = true;
            }
        }

        public bool Post(Token token, TValue value) {
            RollbackTmpStack();
            _error = false;
            while (StackTop().Entry.State(token, value))
                ; // may throw
            if (!_error) {
                CommitTmpStack();
            } else {
                Recover(token, value);
            }
            return _accepted || _error;
        }

        public bool Accept(out TValue value) {
            Debug.Assert(_accepted);
            value = default;
            if (_error) { return false; }
            value = _acceptedValue;
            return true;
        }

        public bool Error() { return _error; }

)",
        { "first_state", table.first_state() }
    );

    // implementation
    stencil(
        os, R"(
        delegate bool StateType(Token token, TValue Value);
        delegate int GotofType(NonTerminal nonterminal);

        private readonly uint _stackSize;
        private readonly ISemanticAction _action;
        private bool _accepted;
        private bool _error;
        private TValue _acceptedValue;

        private readonly struct TableEntry
        {
            public readonly StateType State;
            public readonly GotofType Gotof;
            public readonly bool HandleError;
            public TableEntry(StateType state, GotofType gotof, bool handleError) {
                State = state;
                Gotof = gotof;
                HandleError = handleError;
            }
        }

        private readonly struct StackFrame
        {
            public readonly TableEntry Entry;
            public readonly TValue Value;
            public readonly int SequenceLength;
            public StackFrame(TableEntry entry, TValue value, int sequenceLength) {
                Entry = entry;
                Value = value;
                SequenceLength = sequenceLength;
            }
        }

)"
    );

    // stack operation
    stencil(
        os, R"(
        readonly Stack<StackFrame> _stack;

        bool PushStack(int stateIndex, TValue value, int sequenceLength = 0) {
            bool f = _stack.Push(new StackFrame(Entry(stateIndex), value, sequenceLength));
            Debug.Assert(!_error);
            if (!f) {
                _error = true;
                _action.StackOverflow();
            }
            return f;
        }

        void PopStack(int n) {
$${pop_stack_implementation}
        }

        StackFrame StackTop() {
            return _stack.Top();
        }

        TValue GetArg(int @base, int index) {
            return _stack[@base, index].Value;
        }

        void ClearStack() {
            _stack.Clear();
        }

        void RollbackTmpStack() {
            _stack.RollbackTmp();
        }

        void CommitTmpStack() {
            _stack.CommitTmp();
        }

)",
        {"pop_stack_implementation", [&](std::ostream& os) {
                if (options.allow_ebnf) {
                    stencil(
                        os, R"(
            var nn = n;
            while(nn--) {
                _stack.Pop(1 + _stack.Top().SequenceLength);
            }
)"
                        );
                } else {
                    stencil(
                        os, R"(
            _stack.Pop(n);
)"
                        );
                }
            }}
        );

    if (options.recovery) {
        stencil(
            os, R"(
    void Recover(Token token, TValue value) {
        RollbackTmpStack();
        _error = false;
$${debmes:start}
        while(!StackTop().Entry.HandleError) {
            PopStack(1);
            if (_stack.Empty()) {
$${debmes:failed}
                _error = true;
                return;
            }
        }
$${debmes:done}
        // post error_token;
$${debmes:post_error_start}
        while ((this->*(StackTop().Entry.State))(${recovery_token}, default));
$${debmes:post_error_done}
        CommitTmpStack();
        // repost original token
        // if it still causes error, discard it;
$${debmes:repost_start}
        while ((this->*(StackTop().Entry.State))(token, Value));
$${debmes:repost_done}
        if (!_error) {
            CommitTmpStack();
        }
        if (token != ${token_eof}) {
            _error = false;
        }
    }

)",
            {"recovery_token", options.token_prefix + options.recovery_token},
            {"token_eof", options.token_prefix + "eof"},
            {"debmes:start", {
                    options.debug_parser ?
                        R"(        std::cerr << "recover rewinding start: stack depth = " << stack_.depth() << "\n";
)" :
                        ""}},
            {"debmes:failed", {
                    options.debug_parser ?
                        R"(        std::cerr << "recover rewinding failed\n";
)" :
                        ""}},
            {"debmes:done", {
                    options.debug_parser ?
                        R"(        std::cerr << "recover rewinding done: stack depth = " << stack_.depth() << "\n";
)":
                        ""}},
            {"debmes:post_error_start", {
                    options.debug_parser ?
                        R"(        std::cerr << "posting error token\n";
)" :
                        ""}},
            {"debmes:post_error_done", {
                    options.debug_parser ?
                        R"(        std::cerr << "posting error token done\n";
)" :
                        ""}},
            {"debmes:repost_start", {
                    options.debug_parser ?
                        R"(        std::cerr << "reposting original token\n";
)" :
                        ""}},
            {"debmes:repost_done", {
                    options.debug_parser ?
                        R"(        std::cerr << "reposting original token done\n";
)" :
                        ""}}
            );
    } else {
        stencil(
            os, R"(
        void Recover(Token token, TValue value) {
        }

)"
            );
    }

    if (options.allow_ebnf) {
        stencil(
            os, R"(
    // EBNF support class
    struct Range {
        int beg;
        int end;
        Range() : beg(-1), end(-1) {}
        Range(int b, int e) : beg(b), end(e) {}
    };

    template <class T>
    class Optional {
    public:
        typedef Stack<stack_frame, _StackSize> stack_type;

    public:
        Optional(_SemanticAction& sa, stack_type& s, const Range& r)
            : sa_(&sa), s_(&s), p_(r.beg == r.end ? -1 : r.beg){}

        operator bool() const {
            return 0 <= p_;
        }
        bool operator!() const {
            return !bool(*this);
        }
        T operator*() const {
            T v;
            sa_->downcast(v, s_->nth(p_).value);
            return v;
        }

    private:
        _SemanticAction* sa_;
        stack_type*     s_;
        int             p_;

    };

    template <class T>
    class Sequence {
    public:
        typedef Stack<stack_frame, _StackSize> stack_type;

        class const_iterator {
        public:
            typedef T                       value_type;
            typedef std::input_iterator_tag iterator_category;
            typedef value_type              reference;
            typedef value_type*             pointer;
            typedef size_t                  difference_type;

        public:
            const_iterator(_SemanticAction& sa, stack_type& s, int p)
                : sa_(&sa), s_(&s), p_(p){}
            const_iterator(const const_iterator& x) : s_(x.s_), p_(x.p_){}
            const_iterator& operator=(const const_iterator& x) {
                sa_ = x.sa_;
                s_ = x.s_;
                p_ = x.p_;
                return *this;
            }
            value_type operator*() const {
                value_type v;
                sa_->downcast(v, s_->nth(p_).value);
                return v;
            }
            const_iterator& operator++() {
                ++p_;
                return *this;
            }
            bool operator==(const const_iterator& x) const {
                return p_ == x.p_;
            }
            bool operator!=(const const_iterator& x) const {
                return !((*this)==x);
            }
        private:
            _SemanticAction* sa_;
            stack_type*     s_;
            int             p_;

        };

    public:
        Sequence(_SemanticAction& sa, stack_type& stack, const Range& r)
            : sa_(sa), stack_(stack), range_(r) {
        }

        const_iterator begin() const {
            return const_iterator(sa_, stack_, range_.beg);
        }
        const_iterator end() const {
            return const_iterator(sa_, stack_, range_.end);
        }

    private:
        _SemanticAction& sa_;
        stack_type&     stack_;
        Range           range_;

    };

    // EBNF support member functions
    bool seq_head(Nonterminal nonterminal, int base) {
        // case '*': base == 0
        // case '+': base == 1
        int dest = (this->*(stack_nth_top(base)->entry->gotof))(nonterminal);
        return push_stack(dest, value_type(), base);
    }

    bool seq_trail(Nonterminal, int base) {
        // '*', '+' trailer
        assert(base == 2);
        stack_.swap_top_and_second();
        stack_top()->sequence_length++;
        return true;
    }

    bool seq_trail2(Nonterminal, int base) {
        // '/' trailer
        assert(base == 3);
        stack_.swap_top_and_second();
        pop_stack(1); // erase delimiter
        stack_.swap_top_and_second();
        stack_top()->sequence_length++;
        return true;
    }

    bool opt_nothing(Nonterminal nonterminal, int base) {
        // same as head of '*'
        assert(base == 0);
        return seq_head(nonterminal, base);
    }

    bool opt_just(Nonterminal nonterminal, int base) {
        // same as head of '+'
        assert(base == 1);
        return seq_head(nonterminal, base);
    }

    Range seq_get_range(size_t base, size_t index) {
        // returns beg = end if length = 0 (includes scalar value)
        // distinguishing 0-length-vector against scalar value is
        // caller's responsibility
        int n = int(base - index);
        assert(0 < n);
        int prev_actual_index;
        int actual_index  = stack_.depth();
        while(n--) {
            actual_index--;
            prev_actual_index = actual_index;
            actual_index -= stack_.nth(actual_index).sequence_length;
        }
        return Range(actual_index, prev_actual_index);
    }

    const value_type& seq_get_arg(size_t base, size_t index) {
        Range r = seq_get_range(base, index);
        // multiple value appearing here is not supported now
        assert(r.end - r.beg == 0);
        return stack_.nth(r.beg).value;
    }

    stack_frame* stack_nth_top(int n) {
        Range r = seq_get_range(n + 1, 0);
        // multiple value appearing here is not supported now
        assert(r.end - r.beg == 0);
        return &stack_.nth(r.beg);
    }
)"
            );
    }

    stencil(
        os, R"(
        bool CallNothing(NonTerminal nonTerminal, int @base) {
            PopStack(@base);
            var destIndex = StackTop().Entry.Gotof(nonTerminal);
            return PushStack(destIndex, default);
        }

)"
        );

    // member function signature -> index
    std::map<std::vector<std::string>, int> stub_indices;
    {
        // member function name -> count
        std::unordered_map<std::string, int> stub_counts;

        // action handler stub
        for (const auto& pair: actions) {
            const auto& rule = pair.first;
            const auto& sa = pair.second;

            if (sa.special) {
                continue;
            }

            const auto& rule_type =
                *finder(nonterminal_types, rule.left().name());

            // make signature
            std::vector<std::string> signature;
            make_signature(
                nonterminal_types,
                rule,
                sa,
                signature,
                options.smart_pointer_tag);

            // skip duplicated
            if (0 < stub_indices.count(signature)) {
                continue;
            }

            // make function name
            if (stub_counts.count(sa.name) == 0) {
                stub_counts[sa.name] = 0;
            }
            int stub_index = stub_counts[sa.name];
            stub_indices[signature] = stub_index;
            stub_counts[sa.name] = stub_index+1;

            // header
            stencil(
                os, R"(
    bool call_${stub_index}_${sa_name}(Nonterminal nonterminal, int base${args}) {
)",
                {"stub_index", stub_index},
                {"sa_name", normalize_internal_sa_name(sa.name)},
                {"args", [&](std::ostream& os) {
                        for (size_t l = 0 ; l < sa.args.size() ; l++) {
                            os << ", int arg_index" << l;
                        }
                    }}
                );

            // check sequence conciousness
            std::string get_arg = "get_arg";
            for (const auto& arg: sa.args) {
                if (arg.type.extension != Extension::None) {
                    get_arg = "seq_get_arg";
                    break;
                }
            }

            // automatic argument conversion
            for (size_t l = 0 ; l < sa.args.size() ; l++) {
                const auto& arg = sa.args[l];
                if (arg.type.extension == Extension::None) {
                    stencil(
                        os, R"(
        ${arg_type} arg${index}; sa_.downcast(arg${index}, ${get_arg}(base, arg_index${index}));
)",
                        {"arg_type", make_type_name(arg.type, options.smart_pointer_tag)},
                        {"get_arg", get_arg},
                        {"index", l}
                        );
                } else {
                    stencil(
                        os, R"(
        ${arg_decl};
)",
                        {"arg_decl", make_arg_decl(arg.type, l, options.smart_pointer_tag)}
                        );
                }
            }

            // semantic action / automatic value conversion
            stencil(
                os, R"(
        ${nonterminal_type} r = sa_.${semantic_action_name}(${args});
        value_type v; sa_.upcast(v, r);
        pop_stack(base);
        int dest_index = (this->*(stack_top()->entry->gotof))(nonterminal);
        return push_stack(dest_index, v);
    }

)",
                {"nonterminal_type", make_type_name(rule_type, options.smart_pointer_tag)},
                {"semantic_action_name", normalize_sa_call(sa.name)},
                {"args", [&](std::ostream& os) {
                        bool first = true;
                        for (size_t l = 0 ; l < sa.args.size() ; l++) {
                            if (first) { first = false; }
                            else { os << ", "; }
                            os << "arg" << l;
                        }
                    }}
                );
        }
    }

    // states handler
    for (const auto& state: table.states()) {
        // state header
        stencil(
            os, R"(
    bool state_${state_no}(token_type token, const value_type& value) {
$${debmes:state}
        switch(token) {
)",
            {"state_no", state.no},
            {"debmes:state", [&](std::ostream& os){
                    if (options.debug_parser) {
                        stencil(
                            os, R"(
        std::cerr << "state_${state_no} << " << token_label(token) << "\n";
)",
                            {"state_no", state.no}
                            );
                    }}}
            );

        // reduce action cache
        typedef boost::tuple<
            std::vector<std::string>,
            std::string,
            size_t,
            std::vector<int>>
            reduce_action_cache_key_type;
        typedef
            std::map<reduce_action_cache_key_type,
                     std::vector<std::string>>
            reduce_action_cache_type;
        reduce_action_cache_type reduce_action_cache;

        // action table
        for (const auto& pair: state.action_table) {
            const auto& token = pair.first;
            const auto& action = pair.second;

            const auto& rule = action.rule;

            // action header
            std::string case_tag = options.token_prefix + tokens[token];

            // action
            switch (action.type) {
                case zw::gr::action_shift:
                    stencil(
                        os, R"(
        case ${case_tag}:
            // shift
            push_stack(/*state*/ ${dest_index}, value);
            return false;
)",
                        {"case_tag", case_tag},
                        {"dest_index", action.dest_index}
                        );
                    break;
                case zw::gr::action_reduce: {
                    size_t base = rule.right().size();
                    const std::string& rule_name = rule.left().name();

                    auto k = finder(actions, rule);
                    if (k && !(*k).special) {
                        const auto& sa = *k;

                        std::vector<std::string> signature;
                        make_signature(
                            nonterminal_types,
                            rule,
                            sa,
                            signature,
                            options.smart_pointer_tag);

                        reduce_action_cache_key_type key =
                            boost::make_tuple(
                                signature,
                                rule_name,
                                base,
                                sa.source_indices);

                        reduce_action_cache[key].push_back(case_tag);
                    } else {
                        stencil(
                            os, R"(
        case ${case_tag}:
)",
                            {"case_tag", case_tag}
                            );
                        std::string funcname = "call_nothing";
                        if (k) {
                            const auto& sa = *k;
                            assert(sa.special);
                            funcname = sa.name;
                        }
                        stencil(
                            os, R"(
            // reduce
            return ${funcname}(Nonterminal_${nonterminal}, /*pop*/ ${base});
)",
                            {"funcname", funcname},
                            {"nonterminal", rule.left().name()},
                            {"base", base}
                            );
                    }
                }
                    break;
                case zw::gr::action_accept:
                    stencil(
                        os, R"(
        case ${case_tag}:
            // accept
            accepted_ = true;
            accepted_value_ = get_arg(1, 0);
            return false;
)",
                        {"case_tag", case_tag}
                        );
                    break;
                case zw::gr::action_error:
                    stencil(
                        os, R"(
        case ${case_tag}:
            sa_.syntax_error();
            error_ = true;
            return false;
)",
                        {"case_tag", case_tag}
                        );
                    break;
            }

            // action footer
        }

        // flush reduce action cache
        for(const auto& pair: reduce_action_cache) {
            const reduce_action_cache_key_type& key = pair.first;
            const std::vector<std::string>& cases = pair.second;

            const std::vector<std::string>& signature = key.get<0>();
            const std::string& nonterminal_name = key.get<1>();
            size_t base = key.get<2>();
            const std::vector<int>& arg_indices = key.get<3>();

            for (size_t j = 0 ; j < cases.size() ; j++){
                // fall through, be aware when port to other language
                stencil(
                    os, R"(
        case ${case}:
)",
                    {"case", cases[j]}
                    );
            }

            int index = stub_indices[signature];

            stencil(
                os, R"(
            // reduce
            return call_${index}_${sa_name}(Nonterminal_${nonterminal}, /*pop*/ ${base}${args});
)",
                {"index", index},
                {"sa_name", normalize_internal_sa_name(signature[0])},
                {"nonterminal", nonterminal_name},
                {"base", base},
                {"args", [&](std::ostream& os) {
                        for(const auto& x: arg_indices) {
                            os  << ", " << x;
                        }
                    }}
                );
        }

        // dispatcher footer / state footer
        stencil(
            os, R"(
        default:
            sa_.syntax_error();
            error_ = true;
            return false;
        }
    }

)"
            );

        // gotof header
        stencil(
            os, R"(
    int gotof_${state_no}(Nonterminal nonterminal) {
)",
            {"state_no", state.no}
            );

        // gotof dispatcher
        std::stringstream ss;
        stencil(
            ss, R"(
        switch(nonterminal) {
)"
            );
        bool output_switch = false;
        for (const auto& pair: state.goto_table) {
            stencil(
                ss, R"(
        case Nonterminal_${nonterminal}: return ${state_index};
)",
                {"nonterminal", pair.first.name()},
                {"state_index", pair.second}
                );
            output_switch = true;
        }

        // gotof footer
        stencil(
            ss, R"(
        default: assert(0); return false;
        }
)"
            );
        if (output_switch) {
            os << ss.str();
        } else {
            stencil(
                os, R"(
        assert(0);
        return true;
)"
                );
        }
        stencil(os, R"(
    }

)"
            );


    }

    // table
    stencil(
        os, R"(
    const table_entry* entry(int n) const {
        static const table_entry entries[] = {
$${entries}
        };
        return &entries[n];
    }

)",
        {"entries", [&](std::ostream& os) {
                int i = 0;
                for (const auto& state: table.states()) {
                    stencil(
                        os, R"(
            { &Parser::state_${i}, &Parser::gotof_${i}, ${handle_error} },
)",

                        {"i", i},
                        {"handle_error", state.handle_error}
                        );
                    ++i;
                }
            }}
        );

    // parser class footer
    // namespace footer
    // once footer
    stencil(
        os,
        R"(
};

}

)"
        );
}
