#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct c_Lit { uint32_t x; } c_Lit;
typedef struct c_lbool { uint8_t x; } c_lbool;
typedef struct slice_Lit { const c_Lit* vals; size_t num_vals; } slice_Lit;
typedef struct slice_lbool { const c_lbool* vals; size_t num_vals; } slice_lbool;

#ifdef __cplusplus
    #define NOEXCEPT noexcept

    namespace CMSat{ struct SATSolver; }
    using CMSat::SATSolver;

    extern "C" {
#else
    // c stuff
    #include <stdbool.h>
    #define NOEXCEPT

    #define L_TRUE (0u)
    #define L_FALSE (0u)
    #define L_UNDEF (0u)

    // forward declaration
    typedef struct SATSolver SATSolver;
#endif

extern SATSolver* cmsat_new(void) NOEXCEPT;
extern void cmsat_free(SATSolver* s) NOEXCEPT;

extern unsigned cmsat_nvars(const SATSolver* self) NOEXCEPT;
extern bool cmsat_add_clause(SATSolver* self, const c_Lit* lits, size_t num_lits) NOEXCEPT;
extern bool cmsat_add_xor_clause(SATSolver* self, const unsigned* vars, size_t num_vars, bool rhs) NOEXCEPT;
extern void cmsat_new_vars(SATSolver* self, const size_t n) NOEXCEPT;

extern c_lbool cmsat_solve(SATSolver* self) NOEXCEPT;
extern c_lbool cmsat_solve_with_assumptions(SATSolver* self, const c_Lit* assumptions, size_t num_assumptions) NOEXCEPT;
extern slice_lbool cmsat_get_model(const SATSolver* self) NOEXCEPT;
extern slice_Lit cmsat_get_conflict(const SATSolver* self) NOEXCEPT;

extern void cmsat_set_num_threads(SATSolver* self, unsigned n) NOEXCEPT;

#ifdef __cplusplus
} // end extern c
#endif
