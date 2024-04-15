internal interface IOperator
{
    public abstract void Accept(Calculator calculator);
}

internal abstract class Node : IOperator
{
    public abstract int Calc();
    public virtual void Accept(Calculator calculator) { calculator.Visit(this); }
}

internal abstract class Expr : Node
{
    public override void Accept(Calculator calculator) { calculator.Visit(this); }
}

internal abstract class Term : Node
{
    public override void Accept(Calculator calculator) { calculator.Visit(this); }
}

internal class Number : Node
{
    public Number(int n) { _number = n; }
    public override int Calc() { return _number; }
    public override void Accept(Calculator calculator) { calculator.Visit(this); }
    public override string ToString() { return $"{_number}"; }
    private readonly int _number;
}

internal class TermExpr : Expr
{
    public TermExpr(Term x) { _term = x; }
    public override int Calc() { return _term.Calc(); }
    public override void Accept(Calculator calculator) { calculator.Visit(this); }
    public override string ToString() { return $"{_term}"; }
    internal readonly Term _term;
}

internal class NumberTerm : Term
{
    public NumberTerm(Number x) { _number = x; }
    public override int Calc() { return _number.Calc(); }
    public override void Accept(Calculator calculator) { calculator.Visit(this); }
    public override string ToString() { return $"{_number}"; }
    internal readonly Number _number;
}

internal interface IOp
{
    int Op(Node x, Node y);
}

internal class Add : IOp { public Add() { } public int Op(Node x, Node y) { return x.Calc() + y.Calc(); } }
internal class Sub : IOp { public Sub() { } public int Op(Node x, Node y) { return x.Calc() - y.Calc(); } }
internal class Mul : IOp { public Mul() { } public int Op(Node x, Node y) { return x.Calc() * y.Calc(); } }
internal class Div : IOp { public Div() { } public int Op(Node x, Node y) { return x.Calc() / y.Calc(); } }

internal class BinOpTerm<TOp> : Term where TOp : IOp, new()
{
    public BinOpTerm() { }
    public BinOpTerm(Term x, Term y) { _lhs = x; _rhs = y; }
    public override int Calc() { return _op.Op(_lhs, _rhs); }
    public override void Accept(Calculator calculator) { calculator.Visit(this); }
    public override string ToString() { return $"{Calc()}"; }
    internal readonly TOp _op = new TOp();
    internal readonly Term _lhs;
    internal readonly Term _rhs;
};

internal class BinOpExpr<TOp> : Expr where TOp : IOp, new()
{
    public BinOpExpr() { }
    public BinOpExpr(Expr x, Expr y) { _lhs = x; _rhs = y; }
    public override int Calc() { return _op.Op(_lhs, _rhs); }
    public override void Accept(Calculator calculator) { calculator.Visit(this); }
    public override string ToString() { return $"{Calc()}"; }
    internal readonly TOp _op = new TOp();
    internal readonly Expr _lhs;
    internal readonly Expr _rhs;
};
