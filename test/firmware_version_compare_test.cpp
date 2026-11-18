#include "firmware_version_compare.h"

using firmware_version::compare;

static_assert(compare("3.0.4", "3.0.3") > 0, "newer patch");
static_assert(compare("3.0.3", "3.0.4") < 0, "older patch");
static_assert(compare("3.0.3", "3.0.3") == 0, "equal version");
static_assert(compare("3.0.10", "3.0.9") > 0, "numeric patch order");
static_assert(compare("3.10.0", "3.9.999") > 0, "numeric minor order");
static_assert(compare("4.0.0", "3.99.99") > 0, "major order");
static_assert(compare("03.000.003", "3.0.3") == 0, "leading zeros");
static_assert(compare("99999999999.0.0", "10.0.0") > 0, "large component");
static_assert(compare("0.0.0", "0.0.1") < 0, "zero components");
