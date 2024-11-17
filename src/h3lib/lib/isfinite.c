#include "isfinite.h"

bool isXfinite(double f) { return !isnan(f - f); }
