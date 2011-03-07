/*****************************************************************************
MiniSat -- Copyright (c) 2003-2006, Niklas Een, Niklas Sorensson
glucose -- Gilles Audemard, Laurent Simon (2008)
CryptoMiniSat -- Copyright (c) 2009 Mate Soos

Original code by MiniSat and glucose authors are under an MIT licence.
Modifications for CryptoMiniSat are under GPLv3 licence.
******************************************************************************/

#ifndef CLAUSE_H
#define CLAUSE_H

#ifdef _MSC_VER
#include <msvc/stdint.h>
#else
#include <stdint.h>
#endif //_MSC_VER

#include <cstdio>
#include <vector>
#include <sys/types.h>
#include <string.h>
#include <limits>

#include "SolverTypes.h"
#include "constants.h"
#include "Watched.h"
#include "Alg.h"
#include "constants.h"

template <class T>
uint32_t calcAbstraction(const T& ps) {
    uint32_t abstraction = 0;
    for (uint32_t i = 0; i != ps.size(); i++)
        abstraction |= 1 << (ps[i].var() & 31);
    return abstraction;
}

//=================================================================================================
// Clause -- a simple class for representing a clause:

class MatrixFinder;
class ClauseAllocator;

/**
@brief Holds a clause. Does not allocate space for literals

Literals are allocated by an external allocator that allocates enough space
for the class that it can hold the literals as well. I.e. it malloc()-s
    sizeof(Clause)+LENGHT*sizeof(Lit)
to hold the clause.
*/
struct Clause
{
protected:

    uint32_t isLearnt:1; ///<Is the clause a learnt clause?
    uint32_t strenghtened:1; ///<Has the clause been strenghtened since last SatELite-like work?
    uint32_t changed:1; ///<Var inside clause has been changed
    /**
    @brief Is the XOR equal to 1 or 0?

    i.e. "a + b" = TRUE or FALSE? -- we only have variables inside xor clauses,
    so this is important to know

    NOTE: ONLY set if the clause is an xor clause.
    */
    uint16_t isXorEqualFalse:1;
    uint16_t isXorClause:1; ///< Is the clause an XOR clause?
    uint16_t isRemoved:1; ///<Is this clause queued for removal because of usless binary removal?
    uint16_t isFreed:1; ///<Has this clause been marked as freed by the ClauseAllocator ?
    uint16_t glue:MAX_GLUE_BITS;    ///<Clause glue -- clause activity according to GLUCOSE
    uint16_t mySize; ///<The current size of the clause

    uint32_t num;

    #ifdef STATS_NEEDED
    uint32_t group;
    #endif

    /**
    @brief Stores the literals in the clause

    This is the reason why we cannot have an instance of this class as it is:
    it cannot hold any literals in that case! This is a trick so that the
    literals are at the same place as the data of the clause, i.e. its size,
    glue, etc. We allocate therefore the clause manually, taking care that
    there is enough space for data[] to hold the literals
    */
    #ifdef __GNUC__
    Lit     data[0];
    #else
    //NOTE: Dangerous packing. We love C++. More info: stackoverflow.com/questions/688471/variable-sized-struct-c
    Lit     data[1];
    #endif //__GNUC__

#ifdef _MSC_VER
public:
#endif //_MSC_VER
    template<class V>
    Clause(const V& ps, const uint32_t _group, const bool learnt, const uint32_t clauseNum)
    {
        num = clauseNum;
        isFreed = false;
        isXorClause = false;
        assert(ps.size() > 2);
        mySize = ps.size();
        isLearnt = learnt;
        isRemoved = false;
        setGroup(_group);

        for (uint32_t i = 0; i < ps.size(); i++) data[i] = ps[i];
        glue = MAX_THEORETICAL_GLUE;
        setChanged();
    }

public:
    friend class ClauseAllocator;

    const uint16_t size() const
    {
        return mySize;
    }

    const bool getChanged() const
    {
        return changed;
    }

    void setChanged()
    {
        setStrenghtened();
        changed = 1;
    }

    void unsetChanged()
    {
        changed = 0;
    }

    void shrink (const uint32_t i)
    {
        assert(i <= size());
        mySize -= i;
        if (i > 0) setStrenghtened();
    }

    void pop()
    {
        shrink(1);
    }

    const bool isXor() const
    {
        return isXorClause;
    }

    const bool learnt() const
    {
        return isLearnt;
    }

    const bool getStrenghtened() const
    {
        return strenghtened;
    }

    void setStrenghtened()
    {
        strenghtened = true;
    }

    void unsetStrenghtened()
    {
        strenghtened = false;
    }

    Lit& operator [] (uint32_t i)
    {
        return data[i];
    }

    const Lit& operator [] (uint32_t i) const
    {
        return data[i];
    }

    void setGlue(const uint32_t newGlue)
    {
        assert(newGlue <= MAX_THEORETICAL_GLUE);
        glue = newGlue;
    }

    const uint32_t getGlue() const
    {
        return glue;
    }

    void makeNonLearnt()
    {
        assert(isLearnt);
        isLearnt = false;
    }

    void makeLearnt(const uint32_t newGlue)
    {
        glue = newGlue;
        isLearnt = true;
    }

    inline void strengthen(const Lit p)
    {
        remove(*this, p);
        setStrenghtened();
    }

    inline void add(const Lit p)
    {
        mySize++;
        data[mySize-1] = p;
        setChanged();
    }

    const Lit* getData() const
    {
        return data;
    }

    Lit* getData()
    {
        return data;
    }

    const Lit* getDataEnd() const
    {
        return data+size();
    }

    Lit* getDataEnd()
    {
        return data+size();
    }

    void print(FILE* to = stdout) const
    {
        plainPrint(to);
        fprintf(to, "c clause learnt %s glue %d group %d\n", (learnt() ? "yes" : "no"), getGlue(), getGroup());
    }

    void plainPrint(FILE* to = stdout) const
    {
        for (uint32_t i = 0; i < size(); i++) {
            data[i].print(to);
        }
        fprintf(to, "0\n");
    }

    #ifdef STATS_NEEDED
    const uint32_t getGroup() const
    {
        return group;
    }
    void setGroup(const uint32_t _group)
    {
        group = _group;
    }
    #else
    const uint32_t getGroup() const
    {
        return 0;
    }
    void setGroup(const uint32_t _group)
    {
        return;
    }
    #endif //STATS_NEEDED
    void setRemoved()
    {
        isRemoved = true;
    }

    const bool getRemoved() const
    {
        return isRemoved;
    }

    void setFreed()
    {
        isFreed = true;
    }

    const bool getFreed() const
    {
        return isFreed;
    }

    void takeMaxOfStats(Clause& other)
    {
        if (other.getGlue() < getGlue())
            setGlue(other.getGlue());
    }

    const uint32_t getNum() const
    {
        return num;
    }

    void setNum(const uint32_t newNum)
    {
        num = newNum;
    }
};

/**
@brief Holds an xor clause. Similarly to Clause, it cannot be directly used

The space is not allocated for the literals. See Clause for details
*/
class XorClause : public Clause
{
protected:
    // NOTE: This constructor cannot be used directly (doesn't allocate enough memory).
    template<class V>
    XorClause(const V& ps, const bool xorEqualFalse, const uint32_t _group, const uint32_t clauseNum) :
        Clause(ps, _group, false, clauseNum)
    {
        isXorEqualFalse = xorEqualFalse;
        isXorClause = true;
    }

public:
    friend class ClauseAllocator;

    inline const bool xorEqualFalse() const
    {
        return isXorEqualFalse;
    }

    inline void invert(const bool b)
    {
        isXorEqualFalse ^= b;
    }

    void print(FILE* to = stdout) const
    {
        plainPrint(to);
        fprintf(to, "c clause learnt %s glue %d group %d\n", (learnt() ? "yes" : "no"), getGlue(), getGroup());
    }

    void plainPrint(FILE* to = stdout) const
    {
        fprintf(to, "x");
        if (xorEqualFalse())
            fprintf(to, "-");
        for (uint32_t i = 0; i < size(); i++) {
            fprintf(to, "%d ", data[i].var() + 1);
        }
        fprintf(to, "0\n");
    }

    friend class MatrixFinder;
};

inline std::ostream& operator<<(std::ostream& cout, const Clause& cl)
{
    for (uint32_t i = 0; i < cl.size(); i++) {
        cout << cl[i] << " ";
    }
    return cout;
}

inline std::ostream& operator<<(std::ostream& cout, const XorClause& cl)
{
    cout << "x";
    for (uint32_t i = 0; i < cl.size(); i++) {
        cout << cl[i].var() + 1 << " ";
    }
    if (cl.xorEqualFalse()) cout << " =  false";
    else cout << " = true";

    return cout;
}

struct ClauseData
{
    ClauseData()
    {
        litPos[0] = std::numeric_limits<uint16_t>::max();
        litPos[1] = std::numeric_limits<uint16_t>::max();
    };
    ClauseData(const uint16_t lit1Pos, const uint16_t lit2Pos)
    {
        litPos[0] = lit1Pos;
        litPos[1] = lit2Pos;
    };

    const uint16_t operator[](const bool which) const
    {
        return litPos[which];
    }

    uint16_t& operator[](const bool which)
    {
        return litPos[which];
    }

    void operator=(const ClauseData& other)
    {
        litPos[0] = other.litPos[0];
        litPos[1] = other.litPos[1];
    }

    uint16_t litPos[2];
};

#endif //CLAUSE_H
