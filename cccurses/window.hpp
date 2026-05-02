#ifndef CCURSES_WINDOW_HPP
#define CCURSES_WINDOW_HPP

#include <cassert>
#include <cstdarg>
#include <string>

#include "ncurses.h"
#include "support.hpp"

namespace cccurses {
using std::string;

/**
 * @brief The Window class is a very simple wrapper around curses WINDOW. It's far from useful in all cases right now.
 */
class Window {
  private:
	Window() { }

  public:
	Window(unsigned int height, unsigned int width, unsigned int row, unsigned int col) {
		ptr = newwin(height, width, row, col);
		assert(nullptr != ptr);
		keypad(ptr, true);
	}

	Window(const Window &other) : ptr(other.ptr) { }

	// shouldn't really be here, it only exists for wrapping stdscr
	// also constructor to accomodate derived & subwindows, same signature for all Window classes
	Window(WINDOW *win) : ptr(win) { }

  public:
	~Window() {
		erase();
		if ( ptr != nullptr ) {
			delwin(ptr); // might be dangerous with stdscr?
		}
	}

  public:
	void add(const char character) {
		int rc = waddch(ptr, character);
		assert(OK == rc);
	}

	void add(const chtype raw) {
		int rc = waddch(ptr, raw);
		assert(OK == rc);
	}

	void add(const string &text) {
		if ( !text.empty() ) {
			int rc = waddnstr(ptr, text.c_str(), text.length());
			assert(OK == rc);
		}
	}

	void add(const char character, int attrs) {
		int rc = waddch(ptr, character | attrs);
		assert(OK == rc);
	}

	void add(const string &text, int attrs) {
		if ( !text.empty() ) {
			int rc = wattron(ptr, attrs);
			assert(OK == rc);
			rc = waddnstr(ptr, text.c_str(), text.length());
			assert(OK == rc);
			rc = wattroff(ptr, attrs);
			assert(OK == rc);
		}
	}

	void add(const int line, const int col, const char character) {
		int rc = mvwaddch(ptr, line, col, character);
		assert(OK == rc);
	}

	void add(const int line, const int col, const chtype raw) {
		int rc = mvwaddch(ptr, line, col, raw);
		assert(OK == rc);
	}

	void add(const int line, const int col, const string &text) {
		if ( !text.empty() ) {
			int rc = mvwaddnstr(ptr, line, col, text.c_str(), text.length());
			assert(OK == rc);
		}
	}

	void add(const int line, const int col, const char character, int attrs) {
		int rc = mvwaddch(ptr, line, col, character | attrs);
		assert(OK == rc);
	}

	void add(const int line, const int col, const string &text, int attrs) {
		if ( !text.empty() ) {
			int rc = wattron(ptr, attrs);
			assert(OK == rc);
			rc = mvwaddnstr(ptr, line, col, text.c_str(), text.length());
			assert(OK == rc);
			rc = wattroff(ptr, attrs);
			assert(OK == rc);
		}
	}

	void clear() {
		int re = wclear(ptr);
		assert(re != ERR);
	}

	/*
	 * Clears an entire line from the start, moves the cursor to do so.
	 */
	void clearLine(const int line) {
		int rm = wmove(ptr, line, 0);
		assert(OK == rm);
		wclrtoeol(ptr);
	}

	void clearToEndOfLine() {
		wclrtoeol(ptr);
	}

	void clearToEndOfScreen() {
		wclrtobot(ptr);
	}

	void enableAttributes(const int attrs) {
		int rc = wattron(ptr, attrs);
		assert(OK == rc);
	}

	void disableAttributes(const int attrs) {
		int rc = wattroff(ptr, attrs);
		assert(OK == rc);
	}

	template<class _Win>
	_Win deriveWindow(unsigned int height, unsigned int width, unsigned int line, unsigned int col) {
		WINDOW *der = derwin(ptr, height, width, line, col);
		assert(der != nullptr);
		keypad(der, true); // TODO? part of form? or not?
		return _Win{der};
	}

	Window deriveWindow(unsigned int height, unsigned int width, unsigned int line, unsigned int col) {
		return deriveWindow<Window>(height, width, line, col);
	}

	void erase() {
		int re = werase(ptr);
		assert(re != ERR);
	}

	int maxCols() {
		return getmaxx(ptr);
	}

	int maxRows() {
		return getmaxy(ptr);
	}

	void moveCursor(const int line, const int col) {
		int rc = wmove(ptr, line, col);
		assert(OK == rc);
	}

	void optionScrollable(Toggle toggle) {
		scrollok(ptr, toggle == Toggle::ON);
	}

	void paint() const {
		int rr = wrefresh(ptr);
		assert(rr != ERR);
	}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wvarargs"
	void print(string fmt, ...) {
		va_list args;
		va_start(args, fmt.c_str());
		vw_printw(ptr, fmt.c_str(), args);
		va_end(args);
	}

	void print(const int line, const int col, string fmt, ...) {
		wmove(ptr, line, col);
		va_list args;
		va_start(args, fmt.c_str());
		vw_printw(ptr, fmt.c_str(), args);
		va_end(args);
	}
#pragma GCC diagnostic pop

#undef scroll // scroll is a macro ... needs to be undef'ed in order to define this function
	void scroll(int lines) {
		wscrl(ptr, lines);
	}

	Size size() const {
		Size s;
		s.cols = getmaxx(ptr);
		s.rows = getmaxy(ptr);
		return s;
	}

	void resize(int rows, int cols) {
		wresize(ptr, rows, cols);
	}

	operator WINDOW *() const {
		return ptr;
	}

  protected:
	WINDOW *ptr;
};

// Window that keeps a separate subwindow (or derived window) pointer
/* A Window that has a statically defined border which can be enabled/disabled when required.
 * In order to fix wrapping a subwindow (or more correct: a derived window) is created as
 * the target for everything to be added. */
class BorderedWindow : public Window {
  public:
	BorderedWindow(unsigned int height, unsigned int width, unsigned int line, unsigned int col) :
	    Window(height, width, line, col) {
		outer = ptr;
		// replace original pointer with the derived window inside the border
		ptr = derwin(outer, height - 2, width - 2, 1, 1);
		assert(nullptr != ptr);
		keypad(ptr, true);
	}

	BorderedWindow(const BorderedWindow &o) : Window(o) {
		outer = o.outer;
		title = o.title;
	}

  private:
	// constructor to accomodate derived & subwindows, same signature for all Window classes
	BorderedWindow(WINDOW *sub) : Window(sub), outer(sub) {
		ptr = derwin(sub, Window::maxRows() - 2, Window::maxCols() - 2, 1, 1);
		assert(nullptr != ptr);
		keypad(ptr, true);
	}

  public:
	~BorderedWindow() {
		erase();
		if ( outer != nullptr ) {
			delwin(outer);
		}
	}

  public:
	void erase() {
		touchwin(outer);
		Window::erase();
		int re = werase(outer);
		assert(re != ERR);
		int rr = wrefresh(outer);
		assert(rr != ERR);
	}

	void paint() const {
		touchwin(outer);
		int rc = wborder(outer, 0, 0, 0, 0, 0, 0, 0, 0);
		assert(OK == rc);
		if ( !title.empty() ) {
			rc = mvwaddnstr(outer, 0, 2, title.c_str(), title.length());
			assert(OK == rc);
		}
		rc = wrefresh(outer);
		assert(OK == rc);
		Window::paint();
	}

	void setTitle(const string &title) {
		this->title = title;
	}

  private:
	/* outer Window including the border;
	 * Window::ptr is the inner because it's the main target */
	WINDOW *outer;
	string title;
	// border chars?
	friend class Window;
};

} // namespace cccurses

#endif // WINDOW_HPP
