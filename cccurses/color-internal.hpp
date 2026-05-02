#ifndef CCURSES_COLOR_INTERNAL_HPP
#define CCURSES_COLOR_INTERNAL_HPP

#include "colors.hpp"
#include "ncurses.h"

namespace cccurses { namespace internal {

void defineColors() {
	// TODO: FreeBSD terminals do not allow changing colors? :(
	if ( has_colors() && can_change_color() ) {
		init_color(8, 0xed, 0xee, 0x1e);
	}
}

void defineColorPairs() {
	/* Initialize few hard-coded color pairs */
	init_pair(COLOR_PAIR_FORM_INVALID, COLOR_BLACK, COLOR_RED);
	init_pair(COLOR_PAIR_FORM_SELECTED, COLOR_BLACK, COLOR_YELLOW);
	init_pair(COLOR_PAIR_FORM_VALID, COLOR_BLACK, COLOR_BLUE);

	init_pair(COLOR_PAIR_ALTFORM_INVALID, COLOR_BLACK, COLOR_RED);
	init_pair(COLOR_PAIR_ALTFORM_SELECTED, COLOR_BLACK, COLOR_MAGENTA);
	init_pair(COLOR_PAIR_ALTFORM_VALID, COLOR_BLACK, COLOR_CYAN);

	init_pair(COLOR_PAIR_BUTTON_DEFAULT, COLOR_BLACK, COLOR_BLUE);
	init_pair(COLOR_PAIR_BUTTON_SELECTED, COLOR_WHITE, COLOR_BLUE);
}

void initColors() {
	if ( has_colors() ) {
		start_color();
	}
	defineColors();
	defineColorPairs();
}

}} // namespace cccurses::internal

#endif // CCURSES_COLOR_INTERNAL_HPP
