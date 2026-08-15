#pragma once

// Error-checking helper for CGNS API calls.
//
// IMPORTANT: <cgnslib.h> must be included BEFORE this header in any
// translation unit that uses CHECK_CG (the macro expands to code that
// references CG_OK and cg_get_error). It is deliberately isolated in its own
// header so that pure data headers (e.g. common/types.hpp) never pull in
// CGNS.

#include <stdexcept>
#include <string>

// Checks the return code of a CGNS call and throws std::runtime_error with
// the CGNS error string on failure.
#define CHECK_CG(call)                                                        \
  do {                                                                        \
    const int cg_rc_ = (call);                                                \
    if (cg_rc_ != CG_OK) {                                                    \
      const char* cg_msg_ = cg_get_error();                                   \
      throw std::runtime_error(std::string("CGNS error (code ") +             \
                               std::to_string(cg_rc_) + ") at " +             \
                               std::string(__FILE__) + ":" +                  \
                               std::to_string(__LINE__) + ": " +              \
                               (cg_msg_ != nullptr ? cg_msg_ : "unknown"));   \
    }                                                                         \
  } while (0)
