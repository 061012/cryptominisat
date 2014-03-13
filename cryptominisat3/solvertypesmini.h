#ifndef __SOLVERTYPESMINI_H__
#define __SOLVERTYPESMINI_H__

#include <limits>

namespace CMSat {

typedef uint32_t Var;
static const Var var_Undef(std::numeric_limits<Var>::max()>>1);
class Lit
{
    uint32_t x;
    explicit Lit(uint32_t i) : x(i) { };
public:
    Lit() : x(2*var_Undef) {}   // (lit_Undef)
    explicit Lit(Var var, bool sign) :
        x((2*var) | (uint32_t)sign)
    {}

    const uint32_t& toInt() const { // Guarantees small, positive integers suitable for array indexing.
        return x;
    }
    Lit  operator~() const {
        return Lit(x ^ 1);
    }
    Lit  operator^(const bool b) const {
        return Lit(x ^ (uint32_t)b);
    }
    Lit& operator^=(const bool b) {
        x ^= (uint32_t)b;
        return *this;
    }
    bool sign() const {
        return x & 1;
    }
    Var  var() const {
        return x >> 1;
    }
    Lit  unsign() const {
        return Lit(x & ~1);
    }
    bool operator==(const Lit& p) const {
        return x == p.x;
    }
    bool operator!= (const Lit& p) const {
        return x != p.x;
    }
    /**
    @brief ONLY to be used for ordering such as: a, b, ~b, etc.
    */
    bool operator <  (const Lit& p) const {
        return x < p.x;     // '<' guarantees that p, ~p are adjacent in the ordering.
    }
    bool operator >  (const Lit& p) const {
        return x > p.x;
    }
    bool operator >=  (const Lit& p) const {
        return x >= p.x;
    }
    static Lit toLit(uint32_t data)
    {
        return Lit(data);
    }
};

static const Lit lit_Undef(var_Undef, false);  // Useful special constants.
static const Lit lit_Error(var_Undef, true );  //

inline std::ostream& operator<<(std::ostream& os, const Lit lit)
{
    if (lit == lit_Undef) {
        os << "lit_Undef";
    } else {
        os << (lit.sign() ? "-" : "") << (lit.var() + 1);
    }
    return os;
}

inline std::ostream& operator<<(std::ostream& co, const std::vector<Lit>& lits)
{
    for (uint32_t i = 0; i < lits.size(); i++) {
        co << lits[i];

        if (i != lits.size()-1)
            co << " ";
    }

    return co;
}

///Class that can hold: True, False, Undef
class lbool
{
    char     value;
    explicit lbool(const char v) : value(v) { }

public:
    lbool() :
        value(0)
    {
    };

    bool operator==(lbool b) const
    {
        return value == b.value;
    }

    bool operator!=(lbool b) const
    {
        return value != b.value;
    }

    lbool operator^(const bool b) const
    {
        //return b ? lbool(-value) : lbool(value);
        return lbool(value * (-2*(char)b + 1));
    }

    friend lbool toLbool(const char v);
    friend lbool boolToLBool(const bool b);
    friend class llbool;
};
inline lbool toLbool(const char   v)
{
    return lbool(v);
}

const lbool l_True  = toLbool( 1);
const lbool l_False = toLbool(-1);
const lbool l_Undef = toLbool( 0);

inline lbool boolToLBool(const bool b)
{
    if (b)
        return l_True;
    else
        return l_False;
}

inline std::ostream& operator<<(std::ostream& cout, const lbool val)
{
    if (val == l_True) cout << "l_True";
    if (val == l_False) cout << "l_False";
    if (val == l_Undef) cout << "l_Undef";
    return cout;
}

}

#endif //__SOLVERTYPESMINI_H__
