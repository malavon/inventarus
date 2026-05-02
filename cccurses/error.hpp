#ifndef CCURSES_ERROR_HPP
#define CCURSES_ERROR_HPP

#include "eti.h"

namespace cccurses {

// clang-format off
static const char *CURSES_ERRORS[15] = {
	"OK",
	"System error",
	"Bad argument",
	"Posted",
	"Connected",
	"Bad state",
	"No room",
	"Not posted",
	"Unknown command",
	"No match",
	"Not selectable",
	"Not connected",
	"Request denied",
	"Invalid field",
	"Current",
};

// clang-format on

const char *printRC(int rc) {
	rc = -rc;
	int errCount = sizeof(CURSES_ERRORS);
	if ( rc >= 0 && rc < errCount ) {
		return CURSES_ERRORS[rc];
	} else {
		return "Unknown error";
	}
}
} // namespace cccurses
#endif // ERROR_HPP
