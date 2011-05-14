#include <iosfwd>

int test = 0;

#define check(A) { \
 ++test; \
 if (A) \
  std::cout << "PASS test #" << test << " (" << __FILE__ << ", " << __LINE__ << ')' << std::endl; \
 else { \
  std::cout << "FAIL test #" << test << " (" << __FILE__ << ", " << __LINE__ << ") \"" << #A << '"' << std::endl; \
  return test; \
 } \
}

