using System;
using System.IO;
using Calc;

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
    public void StackOverflow() { Console.WriteLine(nameof(StackOverflow)); }
    public void SyntaxError() { Console.WriteLine(nameof(SyntaxError)); }

    public Node FromExpr(Expr value) { return value; }
    public Node FromNumber(Number value) { return value; }
    public Node FromTerm(Term value) { return value; }
    public Expr ToExpr(Node value) { return (Expr)value; }
    public Number ToNumber(Node value) { return (Number)value; }
    public Term ToTerm(Node value) { return (Term)value; }

    public Expr MakeExpr(Term arg0) { return new TermExpr(arg0); }
    public Expr MakeAdd(Expr arg0, Term arg1) {
        Console.WriteLine($"expr {arg0} + {arg1}");
        return new AddExpr(arg0, new TermExpr(arg1));
    }
    public Expr MakeSub(Expr arg0, Term arg1) {
        Console.WriteLine($"expr {arg0} - {arg1}");
        return new SubExpr(arg0, new TermExpr(arg1));
    }

    public Term MakeTerm(Number arg0) { return new NumberTerm(arg0); }
    public Term MakeMul(Term arg0, Number arg1) {
        Console.WriteLine($"expr {arg0} * {arg1}");
        return new MulTerm(arg0, new NumberTerm(arg1));
    }
    public Term MakeDiv(Term arg0, Number arg1) {
        Console.WriteLine($"expr {arg0} / {arg1}");
        return new DivTerm(arg0, new NumberTerm(arg1));
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
            Console.WriteLine($"{value.Calc()}");
        }
    }
}
