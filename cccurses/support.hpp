#ifndef CCCURSES_SUPPORT_HPP
#define CCCURSES_SUPPORT_HPP

namespace cccurses {

struct LocnSize {
	int col, row;
	int cols, rows;
};

struct Size {
	int cols, rows;
};

enum class Toggle { ON, OFF };

}
#endif // CCCURSES_SUPPORT_HPP
