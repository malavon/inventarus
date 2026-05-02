#ifndef CCURSES_INPUT_HPP
#define CCURSES_INPUT_HPP

#include "window.hpp"

namespace cccurses {

enum KeyFeedback { FEEDBACK_CONTINUE, FEEDBACK_STOP };

template<class CTX>
class KeyEventConsumer {
	/* Using CTX& still allows defining CTX as a pointer, however odd this may be.*/

  public:
	static KeyFeedback keyBackTab(CTX &) {
		return FEEDBACK_CONTINUE;
	}

	static KeyFeedback keyBackspace(CTX &) {
		return FEEDBACK_CONTINUE;
	}

	// TODO: char or int?
	static KeyFeedback keyCharacter(CTX &, const int) {
		return FEEDBACK_CONTINUE;
	}

	static KeyFeedback keyDelete(CTX &) {
		return FEEDBACK_CONTINUE;
	}

	static KeyFeedback keyDownArrow(CTX &) {
		return FEEDBACK_CONTINUE;
	}

	static KeyFeedback keyEnd(CTX &) {
		return FEEDBACK_CONTINUE;
	}

	static KeyFeedback keyEnter(CTX &) {
		return FEEDBACK_CONTINUE;
	}

	static KeyFeedback keyEscape(CTX &) {
		return FEEDBACK_CONTINUE;
	}

	static KeyFeedback keyFunctionN(CTX &, int) {
		return FEEDBACK_CONTINUE;
	}

	static KeyFeedback keyHome(CTX &) {
		return FEEDBACK_CONTINUE;
	}

	static KeyFeedback keyLeftArrow(CTX &) {
		return FEEDBACK_CONTINUE;
	}

	static KeyFeedback keyRightArrow(CTX &) {
		return FEEDBACK_CONTINUE;
	}

	static KeyFeedback keyTab(CTX &) {
		return FEEDBACK_CONTINUE;
	}

	static KeyFeedback keyUpArrow(CTX &) {
		return FEEDBACK_CONTINUE;
	}

	// TODO: char or int?
	static KeyFeedback keyWhitespace(CTX &, const int) {
		return FEEDBACK_CONTINUE;
	}
}; // namespace cccurses

template<class CONSUMER, class CTX>
class KeyEventProducer {
  public:
	static void captureAndConsume(const Window &window, CTX ctx) {
		int ch;
		KeyFeedback fb = FEEDBACK_CONTINUE;
		// check feedback first, otherwise won't be leaving form until pressed twice
		while ( fb == FEEDBACK_CONTINUE && (ch = wgetch(window)) ) {
			switch ( ch ) {
				case -1:
					return; // at -1, leave loop
				// TODO: TAB doesn't work, BTAB (back-tab does)...
				// using code 9 does too, no key value? odd
				case 9:
					fb = CONSUMER::keyTab(ctx);
					break;
				case KEY_BTAB:
					fb = CONSUMER::keyBackTab(ctx);
					break;
				case KEY_ENTER:
				case 10:
					fb = CONSUMER::keyEnter(ctx);
					break;
				case 27 /* escape */:
					fb = CONSUMER::keyEscape(ctx);
					break;
				case KEY_DOWN:
					fb = CONSUMER::keyDownArrow(ctx);
					break;
				case KEY_UP:
					fb = CONSUMER::keyUpArrow(ctx);
					break;
				case KEY_LEFT:
					fb = CONSUMER::keyLeftArrow(ctx);
					break;
				case KEY_RIGHT:
					fb = CONSUMER::keyRightArrow(ctx);
					break;
				case KEY_BACKSPACE:
				case 127: // 127 = 0177; returned for backspace in Konsole? why?
					fb = CONSUMER::keyBackspace(ctx);
					break;
					// if no navigation (left/right), delete still needed for last character in field!
				case KEY_DC: // delete character
					fb = CONSUMER::keyDelete(ctx);
					break;
				case KEY_END:
					fb = CONSUMER::keyEnd(ctx);
					break;
				case KEY_HOME:
					fb = CONSUMER::keyHome(ctx);
					break;
				case ' ': // do not allow space in some forms
					fb = CONSUMER::keyWhitespace(ctx, ch);
					break;
				case KEY_F(1):
				case KEY_F(2):
				case KEY_F(3):
				case KEY_F(4):
				case KEY_F(5):
				case KEY_F(6):
				case KEY_F(7):
				case KEY_F(8):
				case KEY_F(9):
				case KEY_F(10):
				case KEY_F(11):
				case KEY_F(12):
				case KEY_F(13):
					fb = CONSUMER::keyFunctionN(ctx, ch - KEY_F0);
					break;
				default:
					fb = CONSUMER::keyCharacter(ctx, ch);
					break;
			}
		}
	}
};
} // namespace cccurses
#endif // CCURSES_INPUT_HPP
