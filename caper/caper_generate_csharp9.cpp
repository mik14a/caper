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

std::string capitalize_token(const std::string s) {
    if (s == "eof") {
        return "Eof";
    } else if (s == "error") {
        return "Error";
    } else {
        return s;
    }
}

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

void generate_csharp9(
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

using System.Collections.Generic;
using System.Diagnostics;

${value_type_namespace}

namespace ${namespace_name}
{
)",
        {"namespace_name", options.namespace_name},
        {"value_type_namespace",
            !options.value_type_namespace.empty() ? "using " + options.value_type_namespace + ";" : ""
        }
    );

    if (!options.external_token) {
        // token enumeration
        stencil(
            os, R"(
    enum Token
    {
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
                        {"token", capitalize_token(token)}
                    );
                }
            }}
        );
    }

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

    std::unordered_set<std::string> types;
    for (const auto& x : terminal_types) {
        if (x.second.name != "") {
            types.insert(x.second.name);
        }
    }
    for (const auto& x : nonterminal_types) {
        if (x.second.name != "") {
            types.insert(x.second.name);
        }
    }

    stencil(os, R"(
    interface ISemanticAction${value_type_template}
    {
        void Log(string name, Token token, ${value_type} value);
        void SyntaxError(string name, Token token, params Token[] tokens);
$${casts}
$${methods}
    }

)",
        { "value_type_template", options.value_type.empty() ? "<T>" : ""},
        { "value_type", options.value_type.empty() ? "T" : options.value_type },
        { "casts", [&](std::ostream& os) {
            if (options.value_type.empty()) {
                for (auto i = types.begin(); i != types.end(); ++i) {
                    stencil(os, R"(
        ${cast}
)",
                        { "cast", [&](std::ostream& os) {
                            os << "T From" << (*i) << "(" << (*i) << " value);";
                        } }
                    );
                }
                for (auto i = types.begin(); i != types.end(); ++i) {
                    stencil(os, R"(
        ${cast}
)",
                        { "cast", [&](std::ostream& os) {
                            os << (*i) << " To" << (*i) << "(T value);";
                        } }
                    );
                }
            }
        } },
        { "methods", [&](std::ostream& os) {
            for (auto it = ss.begin(); it != ss.end(); ++it) {
                std::stringstream st;
                st << (*it).args[0] << " " << (*it).name << "(";
                bool first = true;
                for (size_t l = 1; l < (*it).args.size(); l++) {
                    if (first) { first = false; } else { st << ", "; }
                    st << ((*it).args[l]) << " " << "arg" << (l - 1);
                }
                st << ");";
                stencil(
                    os, R"(
        ${method}
)",
                    { "method", st.str() }
                );
            }
        } }
    );

    // stack class header
    stencil(
        os, R"(
    class Stack<T>
    {
        public Stack() { }

        public void RollbackTmp() {
            _gap = _stack.Count;
            _tmp.Clear();
        }

        public void CommitTmp() {
            _stack.RemoveRange(_gap, _stack.Count - _gap);
            _stack.AddRange(_tmp);
            _tmp.Clear();
        }

        public void Push(T value) {
            _tmp.Add(value);
        }

        public void Pop(int count) {
            if (_tmp.Count < count) {
                _gap -= count - _tmp.Count;
                _tmp.Clear();
            } else {
                _tmp.RemoveRange(_tmp.Count - count, count);
            }
        }

        public T Top() {
            return this[1];
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

        T this[int index] {
            get => index <= _tmp.Count ? _tmp[^index] : _stack[_gap - index + _tmp.Count];
            set {
                if (index <= _tmp.Count) {
                    _tmp[^index] = value;
                } else {
                    _stack[_gap - index + _tmp.Count] = value;
                }
            }
        }

        public void SwapTopAndSecond() {
            Debug.Assert(2 <= Depth());
            (this[2], this[1]) = (this[1], this[2]);
        }

        readonly List<T> _stack = new();
        readonly List<T> _tmp = new();
        int _gap = 0;
    }

)"
);

    const auto& root_name = table.get_grammar().begin()->right().begin()->name();
    const auto& accept_value_type = !options.value_type.empty()
        ? nonterminal_types.find(root_name)->second.name
        : options.value_type.empty() ? "TValue" : options.value_type;

    // parser class header
    stencil(
        os, R"(
    class Parser${value_type_template}
    {
        enum NonTerminal
        {
)",
        {"value_type_template", options.value_type.empty() ? "<TValue>" : ""}
    );

    for (const auto& nonterminal_type: nonterminal_types) {
        stencil(
            os, R"(
            ${nonterminal_name},
)",
            {"nonterminal_name", nonterminal_type.first}
        );
    }

    stencil(
        os, R"(
        }

        public Parser(ISemanticAction${value_type_template} sa) {
            _entries = new List<TableEntry> {
$${entries}
            };
            _stack = new Stack<StackFrame>();
            _action = sa;
            Reset();
        }

        public void Reset() {
            _error = false;
            _accepted = false;
            ClearStack();
            RollbackTmpStack();
            PushStack(0, default);
            CommitTmpStack();
        }

        public bool Post(${token_name} token) {
            Debug.Assert(token == ${token_eof}${post_accepted_tokens});
            return Post(token, default);
        }

        public bool Post(${token_name} token, ${value_type} value) {
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

        public bool Accept(out ${accept_value_type} value) {
            Debug.Assert(_accepted);
            value = !_error ? _acceptedValue : default;
            return !_error;
        }

        public bool Error() { return _error; }

)",
        { "first_state", table.first_state() },
        { "token_name", options.external_token ? options.token_name : "Token" },
        { "value_type_template", options.value_type.empty() ? "<TValue>" : "" },
        { "value_type", options.value_type.empty() ? "TValue" : options.value_type },
        { "accept_value_type", accept_value_type },
        { "token_eof",
            (options.external_token ? options.token_name : "Token") + "." + options.token_prefix + capitalize_token("eof")
        },
        { "post_accepted_tokens",[&](std::ostream& os) {
            for (const auto& t : terminal_types) {
                if (t.second.name == "") {
                    stencil(os, R"( || token == ${token})",
                        { "token", (options.external_token ? options.token_name : "Token") + "." + options.token_prefix + t.first }
                    );
                }
            }
        } },
        { "entries", [&](std::ostream& os) {
                int i = 0;
                for (const auto& state : table.states()) {
                    stencil(
                        os, R"(
                new(State${i}, Goto${i}, ${handle_error}),
)",
                        {"i", i},
                        {"handle_error", state.handle_error}
                    );
                    ++i;
                }
            }
        }
    );

    // implementation
    stencil(
        os, R"(
        delegate bool StateType(${token_name} token, ${value_type} value);
        delegate int GotoType(NonTerminal nonTerminal);

        readonly ISemanticAction${value_type_template} _action;
        bool _accepted;
        bool _error;
        ${accept_value_type} _acceptedValue;

        readonly struct TableEntry
        {
            public readonly StateType State;
            public readonly GotoType Goto;
            public readonly bool HandleError;
            public TableEntry(StateType statef, GotoType gotof, bool handleError) {
                State = statef;
                Goto = gotof;
                HandleError = handleError;
            }
        }

        readonly struct StackFrame
        {
            public readonly TableEntry Entry;
            public readonly ${value_type} Value;
            public readonly int SequenceLength;
            public StackFrame(TableEntry entry, ${value_type} value, int sequenceLength) {
                Entry = entry;
                Value = value;
                SequenceLength = sequenceLength;
            }
        }

)",
        { "token_name", options.external_token ? options.token_name : "Token" },
        { "value_type_template", options.value_type.empty() ? "<TValue>" : "" },
        { "value_type", options.value_type.empty() ? "TValue" : options.value_type },
        { "accept_value_type", accept_value_type }
    );

    // stack operation
    stencil(
        os, R"(
        readonly List<TableEntry> _entries;
        readonly Stack<StackFrame> _stack;

        void PushStack(int stateIndex, ${value_type} value, int sequenceLength = 0) {
            _stack.Push(new StackFrame(_entries[stateIndex], value, sequenceLength));
        }

        void PopStack(int n) {
$${pop_stack_implementation}
        }

        StackFrame StackTop() {
            return _stack.Top();
        }

${get_arg}
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
        { "value_type", options.value_type.empty() ? "TValue" : options.value_type },
        { "get_arg" , [&](std::ostream& os) {
            if (options.value_type.empty()) {
                stencil(os, R"(
        TValue GetArg(int @base, int index) {
            return _stack[@base, index].Value;
        }
)");
            } else if (options.value_type == accept_value_type) {
                stencil(os, R"(
        ${value_type} GetArg(int @base, int index) {
            return _stack[@base, index].Value;
        }
)", 
                    {"value_type", options.value_type });
            } else {
                stencil(os, R"(
        T GetArg<T>(int @base, int index) where T : ${value_type} {
            return (T)_stack[@base, index].Value;
        }
)",
                    {"value_type", options.value_type}
                );
            }
        } },
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
        void Recover(${token_name} token, ${value_type} value) {
        }

)", 
            { "token_name", options.external_token ? options.token_name : "Token" },
            { "value_type", options.value_type.empty() ? "TValue" : options.value_type }
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
        void CallNothing(NonTerminal nonTerminal, int @base) {
            PopStack(@base);
            var destIndex = StackTop().Entry.Goto(nonTerminal);
            PushStack(destIndex, default);
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
        void Call${stub_index}${sa_name}(NonTerminal nonTerminal, int @base${args}) {
)",
                {"stub_index", stub_index},
                {"sa_name", normalize_internal_sa_name(sa.name)},
                {"args", [&](std::ostream& os) {
                        for (size_t l = 0 ; l < sa.args.size() ; l++) {
                            os << ", int arg" << l << "Index";
                        }
                    }}
                );

            // check sequence conciousness
            std::string get_arg = "GetArg";
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
                    if (options.value_type.empty()) {
                        stencil(
                            os, R"(
            var arg${index} = _action.To${arg_type}(${get_arg}(@base, arg${index}Index));
)",
                            { "arg_type", make_type_name(arg.type, options.smart_pointer_tag) },
                            { "get_arg", get_arg },
                            { "index", l }
                        );
                    } else if (make_type_name(arg.type, options.smart_pointer_tag) != options.value_type) {
                        stencil(
                            os, R"(
            var arg${index} = ${get_arg}<${arg_type}>(@base, arg${index}Index);
)",
                            { "arg_type", make_type_name(arg.type, options.smart_pointer_tag) },
                            { "get_arg", get_arg },
                            { "index", l }
                        );
                    } else {
                        stencil(
                            os, R"(
            var arg${index} = ${get_arg}(@base, arg${index}Index);
)",
                            { "get_arg", get_arg },
                            { "index", l }
                        );
                    }
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
            if (options.value_type.empty()) {
                stencil(
                    os, R"(
            var r = _action.${semantic_action_name}(${args});
            var v = _action.From${nonterminal_type}(r);
            PopStack(@base);
            var destIndex = StackTop().Entry.Goto(nonTerminal);
            PushStack(destIndex, v);
        }

)",
                    { "nonterminal_type", make_type_name(rule_type, options.smart_pointer_tag) },
                    { "semantic_action_name", normalize_sa_call(sa.name) },
                    { "args", [&](std::ostream& os) {
                        bool first = true;
                        for (size_t l = 0; l < sa.args.size(); l++) {
                            if (first) { first = false; } else { os << ", "; }
                            os << "arg" << l;
                        }
                    } }
                );
            } else {
                stencil(
                    os, R"(
            var r = _action.${semantic_action_name}(${args});
            PopStack(@base);
            var destIndex = StackTop().Entry.Goto(nonTerminal);
            PushStack(destIndex, r);
        }

)",
                    { "nonterminal_type", make_type_name(rule_type, options.smart_pointer_tag) },
                    { "semantic_action_name", normalize_sa_call(sa.name) },
                    { "args", [&](std::ostream& os) {
                        bool first = true;
                        for (size_t l = 0; l < sa.args.size(); l++) {
                            if (first) { first = false; } else { os << ", "; }
                            os << "arg" << l;
                        }
                    } }
                );
            }
        }
    }

    // states handler
    for (const auto& state: table.states()) {
        // state header
        stencil(
            os, R"(
        bool State${state_no}(${token_name} token, ${value_type} value) {
$${debmes:state}
            switch (token) {
)",
            {"state_no", state.no},
            {"token_name", options.external_token ? options.token_name : "Token"},
            {"value_type", options.value_type.empty() ? "TValue" : options.value_type},
            {"debmes:state", [&](std::ostream& os) {
                    if (options.debug_parser) {
                        stencil(
                            os, R"(
            _action.Log(nameof(State${state_no}), token, value);
)",
                            {"state_no", state.no},
                            {"d", "$"}
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
            std::string case_tag = options.token_prefix + capitalize_token(tokens[token]);

            // action
            switch (action.type) {
                case zw::gr::action_shift:
                    stencil(
                        os, R"(
            case ${case_tag}:
                // Shift
                PushStack(/*State*/ ${dest_index}, value);
                return false;
)",
                        {"case_tag", (options.external_token ? options.token_name + "." : "Token.") + case_tag},
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
                            { "case_tag", (options.external_token ? options.token_name + "." : "Token.") + capitalize_token(case_tag) }
                            );
                        std::string funcname = "CallNothing";
                        if (k) {
                            const auto& sa = *k;
                            assert(sa.special);
                            funcname = sa.name;
                        }
                        stencil(
                            os, R"(
                // Reduce
                ${funcname}(NonTerminal.${nonterminal}, /*Pop*/ ${base});
                return true;
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
                // Accept
                _accepted = true;
                ${accepted_op}
                return false;
)",
                        { "case_tag", (options.external_token ? options.token_name + "." : "Token.") + capitalize_token(case_tag) },
                        { "accepted_op", 
                            rule.right().size() == 1 && !options.value_type.empty()
                                ? (nonterminal_types.find(rule.right().begin()->name())->second.name != options.value_type
                                    ?  "_acceptedValue = GetArg<" + rule.right().begin()->name() + ">(1, 0);" : "_acceptedValue = GetArg(1, 0);")
                                : "_acceptedValue = GetArg(1, 0);"
                        }
                    );
                    break;
                case zw::gr::action_error:
                    stencil(
                        os, R"(
            case ${case_tag}:
                _action.SyntaxError(nameof(State${state_no});
                _error = true;
                return false;
)",
                        {"case_tag", case_tag},
                        { "state_no", state.no }
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
                    {"case", (options.external_token ? options.token_name + "." : "Token.") + capitalize_token(cases[j])}
                    );
            }

            int index = stub_indices[signature];

            stencil(
                os, R"(
                // Reduce
                Call${index}${sa_name}(NonTerminal.${nonterminal}, /*Pop*/ ${base}${args});
                return true;
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
                _action.SyntaxError(nameof(State${state_no}), token${actions});
                _error = true;
                return false;
            }
        }

)",
            { "state_no", state.no },
            { "actions", [&](std::ostream& os) {
                for (const auto& pair : state.action_table) {
                    const auto& token = pair.first;
                    const auto& action = pair.second;
                    const auto& rule = action.rule;
                    // action header
                    std::string case_tag = options.token_prefix + capitalize_token(tokens[token]);
                    os << ", " << (options.external_token ? options.token_name + "." : "Token.") + case_tag;
                }
            }}
            );

        // gotof header
        stencil(
            os, R"(
        int Goto${state_no}(NonTerminal nonTerminal) {
)",
            {"state_no", state.no}
            );

        // gotof dispatcher
        std::stringstream ss;
        stencil(
            ss, R"(
            switch (nonTerminal) {
)"
            );
        bool output_switch = false;
        for (const auto& pair: state.goto_table) {
            stencil(
                ss, R"(
            case NonTerminal.${nonterminal}: return ${state_index};
)",
                {"nonterminal", pair.first.name()},
                {"state_index", pair.second}
                );
            output_switch = true;
        }

        // gotof footer
        stencil(
            ss, R"(
            default: Debug.Assert(false); return 0;
            }
)"
            );
        if (output_switch) {
            os << ss.str();
        } else {
            stencil(
                os, R"(
            Debug.Assert(false);
            return 0;
)"
                );
        }
        stencil(os, R"(
        }

)"
            );
    }

    // parser class footer
    // namespace footer
    // once footer
    stencil(
        os,
        R"(
    }
}
)"
        );
}
