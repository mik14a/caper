using System;
using System.IO;
using Calc;

internal class Scanner
{
    public Scanner(TextReader @in) {
        _in = @in;
    }

    public Token Get(ref int value) {
        int n;
        do {
            n = _in.Read();
        } while (-1 != n && char.IsWhiteSpace((char)n));

        var c = (char)n;
        if (-1 == n || 0x0004 == c) {
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
            value = v;
            return Token.Number;
        }

        throw new Exception();
    }

    private readonly TextReader _in;
}

internal class SemanticAction : ISemanticAction
{
    public void DebugLog(string name, Token token, int value) {
        Console.WriteLine($"[{name}] Token: {token}, Value: {value}");
    }
    public void SyntaxError(string name, Token token, params Token[] tokens) {
        Console.WriteLine($"[{name}] Token: {token}, Value: {string.Join(", ", tokens)}");
    }

    public int Identity(int arg0) { return arg0; }

    public int MakeAdd(int arg0, int arg1) { Console.WriteLine($"expr {arg0} + {arg1}"); return arg0 + arg1; }
    public int MakeSub(int arg0, int arg1) { Console.WriteLine($"expr {arg0} - {arg1}"); return arg0 - arg1; }
    public int MakeMul(int arg0, int arg1) { Console.WriteLine($"expr {arg0} * {arg1}"); return arg0 * arg1; }
    public int MakeDiv(int arg0, int arg1) { Console.WriteLine($"expr {arg0} / {arg1}"); return arg0 / arg1; }
}

internal class Program
{
    public static void Main() {
        var s = new Scanner(Console.In);
        var sa = new SemanticAction();
        var parser = new Parser(sa);

        while (true) {
            var v = 0;
            var token = s.Get(ref v);
            if (parser.Post(token, v)) {
                break;
            }
        }
        if (parser.Accept(out var value)) {
            Console.WriteLine("accepted");
            Console.WriteLine($"{value}");
        }
    }
}
