/* The save manager (Saving > Save manager, and the game list's menu on a
 * game): a game's in-game save and save states with their sizes, to
 * compress, uncompress or delete one, several marked ones, or all. */
#ifndef SAVEMANAGER_H
#define SAVEMANAGER_H

/* For the game states.cpp has open (states_open_game); title goes in the
 * header. */
void save_manager_run(const char *title);

#endif
