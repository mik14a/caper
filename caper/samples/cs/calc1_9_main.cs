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
            return Token.Eof;
        } else {
            switch (c) {
            case '+': return Token.Add;
            case '-': return Token.Sub;
            case '*': return Token.Mul;
            case '/': return Token.Div;
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
            return Token.Number;
        }

        throw new Exception();
    }

    private readonly TextReader _in;
}

internal class SemanticAction : ISemanticAction
{
    public void DebugLog(string name, Token token, Node value) {
        Console.WriteLine($"[{name}] Token: {token}, Value: {value}");
    }
    public void SyntaxError(string name, Token token, params Token[] tokens) {
        Console.WriteLine($"[{name}] Token: {token}, Value: {string.Join(", ", tokens)}");
    }

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
        var parser = new Parser(sa);

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
