#ifndef GBAEMU_RATE_CONTROL_H
#define GBAEMU_RATE_CONTROL_H

#define RATE_CONTROL_MAX_ADJUST 0.005 /* at most ±0.5 % away from the exact ratio */

/* Output frames per GBA frame (32,768 Hz input) for a stream at `rate` Hz, given how full the sound
 * buffer is: fill = queued / (2 × target), clamped to [0, 1]. Half full → the exact ratio; emptier →
 * slightly more output, fuller → slightly less. */
double rate_control_ratio(int rate, double fill);

#endif
