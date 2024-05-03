internal abstract class Node
{
    public abstract int Calc();
}

internal abstract class Expr : Node
{
}

internal abstract class Term : Node
{
}

internal class Number : Node
{
    public Number(int n) { _number = n; }
    public override int Calc() { return _number; }
    public override string ToString() { return $"{_number}"; }
    private readonly int _number;
}

internal class AddExpr : Expr
{
    public AddExpr(Expr x, Expr y) { _lhs = x; _rhs = y; }
    public override int Calc() { return _lhs.Calc() + _rhs.Calc(); }
    public override string ToString() { return $"{_lhs} + {_rhs}"; }
    private readonly Expr _lhs;
    private readonly Expr _rhs;
}

internal class SubExpr : Expr
{
    public SubExpr(Expr x, Expr y) { _lhs = x; _rhs = y; }
    public override int Calc() { return _lhs.Calc() - _rhs.Calc(); }
    public override string ToString() { return $"{_lhs} - {_rhs}"; }
    private readonly Expr _lhs;
    private readonly Expr _rhs;
}

internal class TermExpr : Expr
{
    public TermExpr(Term x) { _term = x; }
    public override int Calc() { return _term.Calc(); }
    public override string ToString() { return $"{_term}"; }
    private readonly Term _term;
}

internal class MulTerm : Term
{
    public MulTerm(Term x, Term y) { _lhs = x; _rhs = y; }
    public override int Calc() { return _lhs.Calc() * _rhs.Calc(); }
    public override string ToString() { return $"{_lhs} * {_rhs}"; }
    private readonly Term _lhs;
    private readonly Term _rhs;
}

internal class DivTerm : Term
{
    public DivTerm(Term x, Term y) { _lhs = x; _rhs = y; }
    public override int Calc() { return _lhs.Calc() / _rhs.Calc(); }
    public override string ToString() { return $"{_lhs} / {_rhs}"; }
    private readonly Term _lhs;
    private readonly Term _rhs;
}

internal class NumberTerm : Term
{
    public NumberTerm(Number x) { _number = x; }
    public override int Calc() { return _number.Calc(); }
    public override string ToString() { return $"{_number}"; }
    private readonly Number _number;
}