#include <iosfwd>
#include <iostream>

int test = 0;

#define check(A)                                                               \
  {                                                                            \
    ++test;                                                                    \
    if (A)                                                                     \
    {                                                                          \
      std::cout << "PASS test #" << test << " (" << __FILE__ << ", "           \
                << __LINE__ << ")\n";                                          \
    }                                                                          \
    else                                                                       \
    {                                                                          \
      std::cout << "FAIL test #" << test << " (" << __FILE__ << ", "           \
                << __LINE__ << ") \"" << #A << "\"\n";                         \
      return test;                                                             \
    }                                                                          \
  }

#define check_op(A, OP, B)                                                     \
  {                                                                            \
    ++test;                                                                    \
    if ((A) OP (B))                                                              \
    {                                                                          \
      std::cout << "PASS test #" << test << " (" << __FILE__ << ", "           \
                << __LINE__ << ")\n";                                          \
    }                                                                          \
    else                                                                       \
    {                                                                          \
      std::cout << "FAIL test #" << test << " (" << __FILE__ << ", "           \
                << __LINE__ << ") \"" << (A) << ' ' << #OP << ' ' << (B)       \
                << "\"\n";                                                     \
      return test;                                                             \
    }                                                                          \
  }

#define check_eq(A, B) check_op(A, ==, B)
