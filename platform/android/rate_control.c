#include "rate_control.h"

double rate_control_ratio(int rate, double fill) {
    if (fill < 0.0) fill = 0.0;
    if (fill > 1.0) fill = 1.0;
    return (rate / 32768.0) * (1.0 + RATE_CONTROL_MAX_ADJUST * (0.5 - fill) / 0.5);
}
