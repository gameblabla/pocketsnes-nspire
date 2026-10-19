/* The overclock test: the highest CPU speed this calculator runs without
 * errors. */
#ifndef OVERCLOCK_H
#define OVERCLOCK_H

/* Plays the game that is open for a few seconds at normal speed, then tries
 * 408, 420, ... 504 MHz in turn. Each speed gets two rounds, each after its
 * own clock switch: the same seconds of the game again, which must leave
 * the same in the SNES's memory frame by frame, then memory and CPU checks.
 * (Checks alone passed speeds at which games froze: the game is what has to
 * work.) The test stops at the first speed with an error, a switch that
 * didn't get there, or esc, and the highest speed that passed becomes the
 * "highest tested" CPU speed (config_tested_mhz). A marker file names the
 * speed being tried, so a freeze is noticed at the next start
 * (overclock_check_freeze). The results are shown and written to
 * pocketsnes_overclock.txt.tns. The game is put back where it was. Without a
 * game open (in_game 0) it only says to open one. */
void overclock_test_run(int in_game);

/* At startup: if the test froze the calculator, keeps the highest speed that
 * passed before the freeze and says so. Returns 1 if it did. */
int overclock_check_freeze(void);

#endif
