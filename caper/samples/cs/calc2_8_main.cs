using System;
using System.IO;
using calc;

internal class Calculator
{
    public Calculator() { }

    public void Visit<T>(BinOpExpr<T> op) where T : IOp, new() {
        Console.WriteLine($"* {op._lhs} <{op._op}> {op._rhs}");
        op._lhs.Accept(this);
        op._rhs.Accept(this);
    }
    public void Visit<T>(BinOpTerm<T> op) where T : IOp, new() {
        Console.WriteLine($"* {op._lhs} <{op._op}> {op._rhs}");
        op._lhs.Accept(this);
        op._rhs.Accept(this);
    }

    public void Visit(Node node) { }
    //public void Visit(Expr node) { }
    //public void Visit(Term node) { }
    public void Visit(TermExpr node) { node._term.Accept(this); }
    //public void Visit(Number node) { }
    //public void Visit(NumberTerm node) { node._number.Accept(this); }
}

internal class Scanner
{
    public Scanner(TextReader @in) {
        _in = @in;
    }

    public Token Get(out Node node) {
        node = null;
        int n;
        do {
            n = _in.Read();
        } while (-1 != n && char.IsWhiteSpace((char)n));

        var c = (char)n;
        if (-1 == n || 0x0004 == c) {   // 0x0004 for ^D from Console
            return Token.token_eof;
        } else {
            switch (c) {
            case '+': return Token.token_Add;
            case '-': return Token.token_Sub;
            case '*': return Token.token_Mul;
            case '/': return Token.token_Div;
            }
        }

        if (char.IsDigit(c)) {
            var v = (char)n - '0';
            n = _in.Peek();
            while (-1 != n && char.IsDigit((char)n)) {
                _ = _in.Read(); // drop
                v *= 10;
                v += (char)n - '0';
                n = _in.Peek();
            }
            node = new Number(v);
            return Token.token_Number;
        }

        throw new Exception();
    }

    private readonly TextReader _in;
}

internal class SemanticAction : ISemanticAction<Node>
{
    public void StackOverflow() { throw new NotImplementedException(); }
    public void SyntaxError() { throw new NotImplementedException(); }

    public Node FromExpr(Expr value) { return value; }
    public Node Fromint(int value) { return new Number(value); }
    public Node FromTerm(Term value) { return value; }
    public Expr ToExpr(Node value) { return (Expr)value; }
    public int Toint(Node value) { return value.Calc(); }
    public Term ToTerm(Node value) { return (Term)value; }

    public Expr MakeExpr(Term arg0) { return new TermExpr(arg0); }
    public Expr MakeAdd(Expr arg0, Term arg1) {
        Console.WriteLine($"expr {arg0} + {arg1}");
        return new BinOpExpr<Add>(arg0, new TermExpr(arg1));
    }
    public Expr MakeSub(Expr arg0, Term arg1) {
        Console.WriteLine($"expr {arg0} - {arg1}");
        return new BinOpExpr<Sub>(arg0, new TermExpr(arg1));
    }

    public Term MakeTerm(int arg0) { return new NumberTerm(new Number(arg0)); }
    public Term MakeMul(Term arg0, int arg1) {
        Console.WriteLine($"expr {arg0} * {arg1}");
        return new BinOpTerm<Mul>(arg0, new NumberTerm(new Number(arg1)));
    }
    public Term MakeDiv(Term arg0, int arg1) {
        Console.WriteLine($"expr {arg0} / {arg1}");
        return new BinOpTerm<Div>(arg0, new NumberTerm(new Number(arg1)));
    }
}

internal class Program
{
    public static void Main() {
        var s = new Scanner(Console.In);
        var sa = new SemanticAction();
        var parser = new Parser<Node>(sa);

        while (true) {
            var token = s.Get(out var v);
            if (parser.Post(token, v)) {
                break;
            }
        }
        if (parser.Accept(out var value)) {
            Console.WriteLine("accepted");
            value.Accept(new Calculator());
        }
    }
}
