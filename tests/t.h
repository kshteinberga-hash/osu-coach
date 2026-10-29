#pragma once
#include <cmath>
#include <cstdio>
inline int t_fail = 0, t_run = 0;
#define CHECK(c) do { ++t_run; if (!(c)) { ++t_fail; std::printf("%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c); } } while (0)
#define NEAR(a, b, e) do { ++t_run; double _a = (a), _b = (b); if (!(std::fabs(_a - _b) <= (e))) { ++t_fail; \
    std::printf("%s:%d: NEAR(%s, %s): %g vs %g\n", __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)
