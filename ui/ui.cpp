#include "ui.hpp"

#include <cstdio>
#include <set>

namespace electarus { namespace ui {

using namespace cccurses;
using namespace electarus::db;

// hard-coded max # of signals required for window size
static const int MAX_SIGNALS = 10;
static const int MAX_CONFIGSETS = 4;

static const char CHAR_CONFIGSET = '*';
static const char *SIGNAL_HDR("SIGNAL");
static const char *DESCRIPTION_HDR("DESCRIPTION");

// 7 is fixed; all datasheets are 7 wide
static const int FIELD_WIDTH_DATASHEET = 7;
// 3 characters is enough for pin numbers, even BGA
// using 4 is however logical, esthetic purposes 1 empty character always
static const int FIELD_WIDTH_PIN = 4;
// packages (drawing + pins) are up to 6 wide, so always format them at 6
static const int FIELD_WIDTH_PKG = 6;
// as far as I know, signal is max 8 EXCEPT for PM_<signal> signals; then it's 11
static const int FIELD_WIDTH_SIGNAL = 11 + MAX_CONFIGSETS * sizeof(CHAR_CONFIGSET) / sizeof(typeof CHAR_CONFIGSET);
static const int FIELD_WIDTH_DESC = 60; // is resized dynamically if too large

// internally used (partial window) functions
void drawPinSetHeader(Window &, const int row, const PinSetView &vw);
void drawPinSet(Window &, int &row, const PinSetView &vw, const PinView &pv);
void editPinSet(Window &, int &row, PinSetView &vw, PinView &pv);

// helper functions
// scrolls window if cannot accomodate {rows}, adds amount to currentRow, returns bare amount as well
int scrollToAccomodate(Window &win, int rows, int &currentRow);

// hotkey displays
void displayBrowseHotkeys(Window &, const PinSetView &);
void displayEditHotkeys(Window &);
// hotkey helpers
void displayHotkey(Window &win, const string &text, const vector<chtype> &keys);
void displayHotkey(Window &win, const string &text, const string &key);

class UpperCasingFormKeyEventConsumer : public SimpleFormKeyEventConsumer {
  public:
	static KeyFeedback keyCharacter(FORM *ctx, const int ch) {
		form_driver(ctx, std::toupper(ch));
		return FEEDBACK_CONTINUE;
	}
};

typedef BasicForm<UpperCasingFormKeyEventConsumer> UpperCasingForm;
typedef EventEmittingForm<class PinsetEventer, UpperCasingFormKeyEventConsumer> PinsetForm;

class PinsetEventer : public FormEventHandler {
  public:
	PinsetEventer(Window &win, PinsetForm &form, const vector<Package> &pkgs, unordered_map<string, string> &signals,
		      vector<PinView::ConfigView> initCviews, int sgnCol) :
	    window(win), form(form), packages(pkgs), signalAndDescMap(signals), signalColumn(sgnCol) {
		row = 0; // row is locally inside the derived window!
		descColumn = sgnCol + FIELD_WIDTH_SIGNAL + 1 /* blank */;

		string c = "";
		for ( PinView::ConfigView &cv : initCviews ) {
			for ( const pair<int, string> &idxdSgn : cv.signals ) {
				addSignal(idxdSgn.first, idxdSgn.second, c);
			}
			c += CHAR_CONFIGSET; // add an asterisk to indicate config set, not sure if definitive
		}
		// add single set of fields to add a new signal
		addExtraFieldPair();
		form.repost();
	}

  private:
	void addExtraFieldPair() {
		Field sgnField(1, FIELD_WIDTH_SIGNAL, row, signalColumn);
		sgnField.justify(JUSTIFY_RIGHT);
		sgnField.optionAutoSkip(Toggle::OFF);
		// test
		// make field required for validation, shouldn't allow leaving the field?
		// result; as-is empty field is allowed, BUT 3 means 2? etc wtf... odd; \0 included?
		// set_field_type(sgnField.raw(), TYPE_ALNUM, 4);

		// calculate description field to get bigger on larger screens, but not excessive
		// ensure that the window can fit it (although application requires 80 cols minimum)
		int descFieldWidth = min(FIELD_WIDTH_DESC, window.size().cols - descColumn);

		Field descField(1, descFieldWidth, row, descColumn);
		descField.optionsActiveAndEditable(Toggle::OFF);
		descField.optionAutoSkip(Toggle::OFF);
		descField.makeDynamic(0); // make it dynamic without size restriction; trust the user ...

		if ( signalAndDescFields.size() % 2 == 1 ) {
			sgnField.setColors(COLOR_PAIR_FORM_SELECTED, COLOR_PAIR_ALTFORM_VALID);
			descField.setColors(COLOR_PAIR_FORM_SELECTED, COLOR_PAIR_ALTFORM_VALID);
		}

		form.addField(sgnField);
		form.addField(descField);
		signalAndDescFields.push_back(pair<Field, Field>(sgnField, descField));
		previousSignals.push_back("");

		row++;
	}

	void addSignal(const int idx, const string signal, const string cv) {
		while ( signalAndDescFields.size() <= idx ) {
			addExtraFieldPair();
		}
		pair<Field, Field> pr = signalAndDescFields[idx];
		pr.first.setBuffer(cv + signal);
		string desc = signalAndDescMap[signal];
		pr.second.setBuffer(desc); // assume a description always exists
		previousSignals[idx] = signal;
	}

	void fieldHopped() {
		int idx = 0;
		bool repost = false;
		bool lastFilledSignalDescEditable = false;

		// if last signal field filled, add pair of fields to form
		// size check just in case
		int signals = signalAndDescFields.size();
		if ( signals > 0 && signals < MAX_SIGNALS ) {
			pair<Field, Field> lastSgn = signalAndDescFields[signalAndDescFields.size() - 1];
			if ( !lastSgn.first.buffer<string>().empty() ) {
				addExtraFieldPair();
				repost = true;
			}
		}

		// all fields: if changed, check signal and set desc (un-)editable or
		for ( pair<Field, Field> &pr : signalAndDescFields ) {
			Field &sgn = pr.first;
			Field &desc = pr.second;

			string signalTxt = sgn.buffer<string>();
			// cut off characters indicating a configset
			int cfIdx = -1;
			if ( (cfIdx = signalTxt.find_last_of(CHAR_CONFIGSET)) != string::npos ) {
				signalTxt = signalTxt.substr(cfIdx + 1);
			}
			// only do anything IF the signal has changed
			if ( signalTxt != previousSignals[idx] ) {
				// this way it is set to false UNLESS the very last iteration sets it to true
				lastFilledSignalDescEditable = false;

				previousSignals[idx] = signalTxt;
				repost = true;
				// no description for empty signal, but not editable either
				if ( signalTxt.empty() ) {
					desc.optionsActiveAndEditable(Toggle::OFF);
				} else {
					if ( signalAndDescMap.find(signalTxt) == signalAndDescMap.end() ) {
						desc.optionsActiveAndEditable(Toggle::ON);
						lastFilledSignalDescEditable = true;
						// also: required (todo: also in other branches)
					} else {
						// exists
						string description = signalAndDescMap[signalTxt];
						desc.setBuffer(description);
						desc.optionsActiveAndEditable(description.empty() ? Toggle::ON : Toggle::OFF);
					}
				}
			}
			idx++;
		}

		if ( repost ) {
			form.repost();

			// using form_driver directly for now
			// there are limited options, most logically a certain field should be focused
			// instead of last or last by one field
			// last field is the (empty) signal field
			// last - 1 is the description field of the last filled signal
			form_driver(form, REQ_LAST_FIELD);
			if ( lastFilledSignalDescEditable ) {
				form_driver(form, REQ_PREV_FIELD);
			}
		}
	}

  public:
	const vector<pair<Field, Field>> &getFieldVector() const {
		return signalAndDescFields;
	}

	void onNextField() {
		fieldHopped();
	}

	void onPreviousField() {
		fieldHopped();
	}

  private:
	int row;
	// column to use for next signal & desc fields
	int signalColumn, descColumn;
	PinsetForm &form;
	Window &window;
	vector<Package> packages;
	unordered_map<string, string> signalAndDescMap;

	vector<pair<Field, Field>> signalAndDescFields;
	vector<string> previousSignals;
};

// helper functions
int scrollToAccomodate(Window &win, int rows, int &currentRow) {
	int rowsToScroll = std::max(0, currentRow + rows - win.maxRows());
	if ( rowsToScroll > 0 ) {
		win.scroll(rowsToScroll);
		currentRow -= rowsToScroll;
	}
	return rowsToScroll; // returns how many were scrolled in reality
}

void drawPinSetHeader(Window &win, const int hdrRow, const PinSetView &vw) {
	int col = 0;
	const Package orderedBy = vw.orderedBy();
	for ( const Package &pkg : vw.pkgs ) {
		col += FIELD_WIDTH_PKG + 1;
		string conc = pkg.drawing + to_string(pkg.pins);
		win.moveCursor(hdrRow, col - conc.length() - 1);
		win.add(orderedBy == pkg ? ACS_DARROW : ' ');
		win.add(conc);
	}

	int sgnCol = col + 1;
	int descCol = sgnCol + FIELD_WIDTH_SIGNAL + 1;

	win.add(hdrRow, sgnCol, SIGNAL_HDR);
	win.add(hdrRow, descCol, DESCRIPTION_HDR);
}

// this function renders a pinset; it assumes that there is enough room available to render 1 signal/line
// +1 line for a horizontal ruler below the last signal
void drawPinSet(Window &win, int &row, const PinSetView &vw, const PinView &pv) {
	int col = 0;
	for ( const Package &pkg : vw.pkgs ) {
		col += FIELD_WIDTH_PKG + 1;
		if ( pv.pins.end() == pv.pins.find(pkg) || pv.pins.at(pkg).empty() ) {
			win.add(row, col - 1, '-');
		} else {
			const string pstr = pv.pins.at(pkg); // implicit casting
			win.add(row, col - pstr.length(), pstr);
		}
	}

	col++;
	int ccRow = row, descCol = col + FIELD_WIDTH_SIGNAL + 1;
	// cut off descriptions if need be
	int maxDescLength = std::min(FIELD_WIDTH_DESC, win.maxCols() - descCol - 1);
	const string CUT_CHARS = "...";
	string configChar = "", sgn, desc;
	int sgnIdx = 0;
	for ( int c = 0; c < pv.cviews.size(); c++, configChar += CHAR_CONFIGSET ) {
		for ( const pair<int, string> &indexedSignal : pv[c] ) {
			sgnIdx = indexedSignal.first;
			sgn = indexedSignal.second;
			if ( ccRow + sgnIdx < win.maxRows() && !sgn.empty() ) {
				win.moveCursor(ccRow + sgnIdx, col);
				win.add(configChar);
				win.add(sgn);
				desc = vw.signalDescs.at(sgn);
				if ( desc.length() > maxDescLength ) {
					win.add(ccRow + sgnIdx, descCol,
						desc.substr(0, maxDescLength - CUT_CHARS.length()) + CUT_CHARS);
				} else {
					win.add(ccRow + sgnIdx, descCol, desc);
				}
				row++; // for each printed signal, row is advanced; it'll even out with cached row
			}
			sgnIdx++;
		}
	}
}

// gets data from form and updates signalset AND signals/description data
void formToSignalData(PinView &pv, PinSetView &vw, Field pinFields[], PinsetEventer &pev) {
	for ( int i = 0; i < vw.pkgs.size(); i++ ) {
		// packages have the same ordering as the fields
		string pin = pinFields[i].buffer<string>();
		string::size_type idx = pin.find_first_of("0123456789");
		if ( pin.empty() || idx == string::npos ) { // no legal pin number (always contains a number)
			pv.pins[vw.pkgs[i]] = Pin{};
		} else {
			string bga = (idx == 0) ? "" : pin.substr(0, idx);
			int nr = (idx == 0 ? stoi(pin) : stoi(pin.substr(idx, pin.size())));
			pv.pins[vw.pkgs[i]] = Pin{bga, nr};
		}
	}

	pv.clearSignals();
	int cfIdx = 0, cfCharIdx = 0, sgnIdx = 0;
	for ( const pair<Field, Field> &sgnAndDesc : pev.getFieldVector() ) {
		string sgn = sgnAndDesc.first.buffer<string>();
		string desc = sgnAndDesc.second.buffer<string>();
		if ( !sgn.empty() ) {
			cfIdx = 0;
			if ( (cfCharIdx = sgn.find_last_of(CHAR_CONFIGSET)) != string::npos ) {
				cfIdx = std::min(cfCharIdx + 1, static_cast<int>(pv.cviews.size()) - 1);
				sgn = sgn.substr(cfCharIdx + 1);
			}

			pv[cfIdx][sgnIdx] = sgn;

			// add signal descriptions for new signals
			if ( vw.signalDescs[sgn].empty() ) {
				vw.signalDescs[sgn] = desc;
			}
		}
		sgnIdx++;
	}
}

void editPinSet(Window &win, int &row, PinSetView &vw, PinView &pv) {
	Window formWin = win.deriveWindow<Window>(MAX_SIGNALS, 0, row, 0);

	FormBuilder fb;
	Field pinFields[vw.pkgs.size()];

	int col = 0;
	for ( int i = 0; i < vw.pkgs.size(); i++ ) {
		col += FIELD_WIDTH_PKG + 1;

		Field fld = Field(1, FIELD_WIDTH_PIN, 0, col - FIELD_WIDTH_PIN);
		fld.justify(JUSTIFY_RIGHT);
		fld.optionAutoSkip(Toggle::OFF);
		if ( !pv.pins[vw.pkgs[i]].empty() ) {
			fld.setBuffer(pv.pins[vw.pkgs[i]]); // implicit cast to string
		}
		fb.addField(fld);
		pinFields[i] = fld;
	}

	PinsetForm form = fb.build<PinsetForm>(formWin);

	int sgnCol = col + 1;
	PinsetEventer pev(formWin, form, vw.pkgs, vw.signalDescs, pv.cviews, sgnCol);

	form.loop(pev);
	formWin.erase(); // important, erase only the form part of the window

	// after looping of edit, complete the pinview
	formToSignalData(pv, vw, pinFields, pev);
	vw.stopEdit();
}

// Window drawing functions

// render all pinviews up to the selected index at least
// MVP: this function keeps the selected index on the last row unless it's on the very first screen
// it might be nicer if it behaves like a text editor: scrollin upwards from below until the first line is reached
void drawPinSetEditingWindow(Window &win, PinSetView &vw) {
	// hard erase and paint() at the end; fixes all possible rendering issues
	// yes, there is data in the window that can be reused; but I'm recreating the entire window anyway
	win.erase();

	int row = 0, idx = 0;
	bool roomToDisplayMore = true, selectionReached = false;
	for ( PinSetView::iterator it = vw.begin(); it != vw.end() && (roomToDisplayMore || !selectionReached); it++, idx++ ) {
		PinView &pv = *it;
		if ( vw.isEdit(idx) ) {
			// ensure there is enough room to display entire form, will not be dynamically expanded
			scrollToAccomodate(win, MAX_SIGNALS + 1 /* header */, row);
			drawPinSetHeader(win, row++, vw);
			editPinSet(win, row, vw, pv);
			row--; // row is restored because this same function call it'll be rendered in THE SAME SPOT

			// if it's not valid, remove it
			if ( !pv.hasPinsAndSignals() ) {
				vw.erase(it);
			}

			// if signals are added or removed, everything below will need to be redrawn
			// but always assume more can be displayed after editing: remember the scrolling earlier
			roomToDisplayMore = true;
		}

		if ( vw.isSelection(idx) ) {
			selectionReached = true;
			scrollToAccomodate(win, pv.countSignals() + 1 /* header */ + 1 /* horiz. ruler */, row);
			win.enableAttributes(WA_BOLD);
			drawPinSetHeader(win, row++, vw);
			for ( int i = 0; i < pv.countSignals(); i++ ) {
				win.add(row + i, 1, ">");
			}
			drawPinSet(win, row, vw, pv);
			mvwhline(win, row++, 1, 0, min(win.maxCols() - 2, 79)); // capped at 80, esthaetics
			win.disableAttributes(WA_BOLD);
		} else if ( !selectionReached ) { // selection not yet reached, keep drawing & scrolling if need be
			int scrolled = scrollToAccomodate(win, pv.countSignals() + 1 /* header */ + 1 /* horiz. ruler */, row);
			drawPinSet(win, row, vw, pv);
			mvwhline(win, row++, 1, 0, min(win.maxCols() - 2, 79)); // capped at 80, esthaetics
			roomToDisplayMore = (scrolled == 0);
		} else if ( roomToDisplayMore ) {    // if there is still some room to render the next signal, do so
			if ( row < win.maxRows() ) { // if there is 1 line available, render partially
				drawPinSet(win, row, vw, pv);
			}
			if ( row < win.maxRows() ) {
				mvwhline(win, row++, 1, 0, min(win.maxCols() - 2, 79)); // capped at 80, esthaetics
			} else {
				roomToDisplayMore = false;
			}
		}
	}

	// at last option editing means inserting a new one
	if ( vw.isEdit(vw.size()) ) {
		PinView pv = vw.createNewPinView();
		scrollToAccomodate(win, MAX_SIGNALS + 1 /* header */, row);
		drawPinSetHeader(win, row++, vw);
		editPinSet(win, row, vw, pv);
		// data has been added to given Signalset but is only valid if at least one pin and one signal
		// signals without pins are useless, pins without signals are as well
		row--; // advanced to draw header above, OVERWRITE exact edit position with view-only
		if ( pv.hasPinsAndSignals() ) {
			win.clearLine(row);
			vw.push_back(pv);
			drawPinSet(win, row, vw, pv);
			mvwhline(win, row++, 1, 0, min(win.maxCols() - 2, 79)); // capped at 80, esthaetics
			vw.moveDown();
		}
	}

	// scrolled/selected all the way to the botton
	// render an empty placeholder
	if ( vw.isSelection(vw.size()) ) {
		scrollToAccomodate(win, MAX_SIGNALS + 1 /* header */, row); // scroll as if editing
		win.enableAttributes(WA_BOLD);
		drawPinSetHeader(win, row++, vw);
		win.add(row++, 1, "> End of signals reached. Inserting will add a new signalset.");
		win.disableAttributes(WA_BOLD);
	}
	win.paint();
}

void drawSetConfigWindow(BorderedWindow &win, const vector<Configset> &cfs) {
	const int MAX_PKG_LEN = 6;
	const int COL_HDR = 1;
	const int COL_DATA = 2;

	// first calculate if there is enough room to display everything
	int reqRows = 0, reqAllMdl = 0, reqAllPkg = 0, reqDefault = 0, reqOther = 0;
	for ( const Configset &cs : cfs ) {
		int reqMdl = cs.toModels().size() /* one line per model */ + 1 /* header */;
		int reqPkg = cs.toPkgs().size() / 2 /* packages are max 5 wide, 2 pkgs/line */
			   + cs.toPkgs().size() % 2 /* when odd, 1 extra pkg, 1 extra line */
			   + 1 /* header */;
		if ( reqDefault == 0 ) { // first set, default set
			reqDefault = reqMdl + reqPkg + 1 /* horizontal ruler */;
		} else {
			reqOther += reqMdl + reqPkg + 1 /* horizontal ruler */;
		}
		reqAllMdl += reqMdl;
		reqAllPkg += reqPkg;
	}
	reqRows = reqDefault + reqOther;

	// idea is to not display default set if no room, not display packages if no room
	// TODO: packages can be removed if same for all sets only
	bool canDisplayAll = reqRows <= win.maxRows();
	bool canDisplayAllIfNoHeaders = !canDisplayAll && reqRows - cfs.size() * 2 <= win.maxRows();
	bool canDisplayDefIfNoPkgs = reqRows - reqAllPkg <= win.maxRows();
	bool canDisplayAllButDef = reqOther <= win.maxRows();
	bool displayDef = canDisplayAll || canDisplayAllIfNoHeaders || canDisplayDefIfNoPkgs;
	bool displayPkg = canDisplayAll || canDisplayAllIfNoHeaders || (!canDisplayDefIfNoPkgs && canDisplayAllButDef);
	bool displayMdl = reqAllMdl <= win.maxRows();
	bool displayHdr = !canDisplayAllIfNoHeaders;

	win.clear();
	int lr = 0, csIdx = 0;
	for ( const Configset &cs : cfs ) {
		if ( csIdx == 0 && !displayDef ) { // do not display default if no room
			csIdx++;
			continue;
		}

		mvwhline(win, lr, 0, ACS_HLINE, win.maxCols());
		if ( csIdx == 0 ) {
			win.print(lr++, 1, ":Default (%d):", cs.orderablesView().size());
		} else {
			win.print(lr++, 1, ":Config F%d (%d):", csIdx + 4, cs.orderablesView().size()); // F5, F6, F7
		}

		if ( displayMdl ) {
			if ( displayHdr ) {
				win.add(lr++, COL_HDR, "Models:");
			}
			for ( const string &model : cs.toModels() ) {
				win.add(lr++, COL_DATA, model);
			}
		}

		if ( displayPkg ) {
			if ( displayHdr ) {
				win.add(lr++, COL_HDR, "Packages:");
			}
			vector<Package> pkgs = cs.toPkgs();
			for ( int i = 0; i < pkgs.size(); i++ ) {
				const string pkg = pkgs[i].drawing + to_string(pkgs[i].pins);
				if ( i % 2 == 0 ) {
					win.add(lr, COL_DATA, pkg);
				} else {
					win.add(lr++, COL_DATA + MAX_PKG_LEN + 1 + (MAX_PKG_LEN - pkg.length()), pkg);
				}
			}
			lr += pkgs.size() % 2; // correctly advance row if ended on an odd number
		}
		mvwhline(win, lr, 0, ACS_HLINE, win.maxCols());
		csIdx++;
	}
	if ( csIdx <= MAX_CONFIGSETS ) {
		win.print(lr++, 1, "Press F%d to add", csIdx + 4);
	}
	win.paint();
}

void drawTopWindow(BorderedWindow &win, const Datasheet &ds, const DatabaseTotals &ttl, const DatabaseTotals &sprt) {
	int topLine = 0;
	int topCol = 1;
	win.setTitle("Search");
	win.add(topLine + 0, topCol, "Datasheet: ");
	win.add(ds.id);
	win.add(topLine + 0, topCol + 24, "(c) ");
	win.add(ds.issueDate);
	win.add(topLine + 1, topCol, "Revision:  ");
	if ( ds.rev.empty() ) {
		win.add("A"); // TODO: justify?
	} else {
		win.add(ds.rev); // TODO: justify?
		win.add(topLine + 1, topCol + 24, "(c) ");
		win.add(ds.revDate);
	}

	static const int BFR = 64;
	char buffer[BFR] = "";
	string fullHdr = "DB supported/total (%): ";
	std::snprintf(buffer, BFR, "%d/%d datasheets (%#.1f%%)", sprt.datasheets, ttl.datasheets,
		      100.0f * sprt.datasheets / ttl.datasheets);
	fullHdr += buffer;
	std::snprintf(buffer, BFR, ", %d/%d devices (%#.1f%%)", sprt.devices, ttl.devices, 100.0f * sprt.devices / ttl.devices);
	fullHdr += buffer;
	std::snprintf(buffer, BFR, " and %d/%d orderables (%#.1f%%)", sprt.orderables, ttl.orderables,
		      100.0f * sprt.orderables / ttl.orderables);
	fullHdr += buffer;
	if ( win.maxCols() >= fullHdr.length() + 2 ) {
		win.add(topLine + 2, topCol, fullHdr);
	} else {
		string shortHdr = "DB (sprt/ttl/%):";
		std::snprintf(buffer, BFR, " %d/%d sheets (%#.1f%%)", sprt.datasheets, ttl.datasheets,
			      100.0f * sprt.datasheets / ttl.datasheets);
		shortHdr += buffer;
		std::snprintf(buffer, BFR, ", %d/%d devs (%#.1f%%)", sprt.devices, ttl.devices,
			      100.0f * sprt.devices / ttl.devices);
		shortHdr += buffer;
		std::snprintf(buffer, BFR, ", %d/%d ordbls (%#.1f%%)", sprt.orderables, ttl.orderables,
			      100.0f * sprt.orderables / ttl.orderables);
		shortHdr += buffer;
		if ( win.maxCols() >= shortHdr.length() + 2 ) {
			win.add(topLine + 2, topCol, shortHdr);
		} else {
			win.add(topLine + 2, topCol, "DB:");
			win.print(" %d/%d DS", sprt.datasheets, ttl.datasheets);
			win.print(", %d/%d DEV", sprt.devices, ttl.devices);
			win.print(", %d/%d ODBL", sprt.orderables, ttl.orderables);
		}
	}
	win.paint();
}

void loopPinsetEditing(Window &win, Window &hot, BorderedWindow &config, ui::PinSetView &vw) {
	// initially render configsets to screen
	drawSetConfigWindow(config, vw.csets);

	int tempChar = 0;
	do {
		switch ( tempChar ) {
			case KEY_UP:
				vw.moveUp();
				break;
			case KEY_DOWN:
				vw.moveDown();
				break;
			case KEY_ENTER:
			case 10 /* RETURN */:
				vw.edit();
				displayEditHotkeys(hot);
				break;
			case KEY_IC /* insert */:
				// insert and edit; will be removed by ui code if no signals are inserted!
				vw.insertView();
				break;
			case KEY_DC /* delete */:
				vw.deleteView();
				break;
			case KEY_F(2):
				vw.orderByNext();
				break;
			case KEY_F(5):
			case KEY_F(6):
			case KEY_F(7):
			case KEY_F(8): // key bindings in case pinout tool created more sets than technically allowed...
			case KEY_F(9): // ignored down below, but have to allow editing/deleting configsets that exist
			case KEY_F(10): {
				int idx = tempChar - KEY_F(5) + 1;
				assert(idx > 0); // never allow editing default set; safety check for coding errors
				if ( idx < vw.csets.size() ) {
					Configset cs = ui::filterForConfigset(vw.csets[0].orderablesView(), vw.csets[idx]);

					if ( cs.empty() ) { // clearing a set is hidden delete function :p
						vw.csets.erase(vw.csets.begin() + idx);
					} else {
						vw.csets[idx] = cs;
					}
				} else if ( idx <= MAX_CONFIGSETS ) {
					// create new set
					Configset cs = ui::filterForConfigset(vw.csets[0].orderablesView());
					if ( !cs.empty() ) {
						vw.add(cs);
					}
				}
				ui::drawSetConfigWindow(config, vw.csets);
			} break;

			case 27 /*ESCAPE*/: // open a menu or something, probably beyond MVP though
				break;
		}

		drawPinSetEditingWindow(win, vw);
		displayBrowseHotkeys(hot, vw); // default hotkeys
	} while ( (tempChar = wgetch(win)) != 27 ); // ESC key for exit
}

// used to create config set, display packages & devices for the user to filter them
Configset filterForConfigset(const vector<db::Orderable> &odv, const Configset &base) {
	static const string TEXT = "Select devices OR packages to use for the set.";
	static const string KEYS = "U/D Move TAB/STAB Models/Packages SPACE Select RETURN Confirm"; // marker for length
	static const int TEXT_WIDTH = max(TEXT.length(), KEYS.length());

	Configset all(odv);
	vector<Package> ps = all.toPkgs();
	vector<string> ms = all.toModels();
	int WIN_WIDTH = max(TEXT_WIDTH, max(FIELD_WIDTH_PKG + 1, 18)) + 2 /* Whitespace Left/right  */ + 2 /* border */;
	int WIN_HEIGHT = std::max(ps.size(), ms.size()) + 3 /* text & empty line */ + 1 /* empty line */ + 2 /* border */;

	BorderedWindow center(WIN_HEIGHT, WIN_WIDTH, (LINES - WIN_HEIGHT) / 2, (COLS - WIN_WIDTH) / 2);
	center.setTitle("Filter");
	center.add(0, (center.maxCols() - TEXT.length()) / 2 - 1, TEXT);
	center.moveCursor(1, (center.maxCols() - KEYS.length()) / 2 - 1);
	displayHotkey(center, "Move", vector<chtype>{ACS_UARROW, '/', ACS_DARROW});
	displayHotkey(center, "Models/Packages", "TAB/STAB");
	displayHotkey(center, "Select", "SPACE");
	displayHotkey(center, "Confirm", "RETURN");
	center.paint();

	enum { MDL = 0, PKG = 1 };
	// using arrays for these greatly simplifies below code
	// of course, using some sort of selection box would do this even more :)
	unsigned int selIdx[2] = {0, 0},
		     maxIdx[] = {static_cast<unsigned int>(ms.size() - 1), static_cast<unsigned int>(ps.size() - 1)};
	int intIdx = MDL; // working with index creates shortest code
	// selection masks; using entire int-space; 15 models is maximum in database though
	// if base is empty, select all; if not, select none and update from base
	// this is the clever bit ... right?
	int bitMsks[] = {base.empty() ? -1 : 0, base.empty() ? -1 : 0};
	int mdlIdx, pkgIdx;
	for ( const db::Orderable &o : base.orderablesView() ) {
		mdlIdx = 0, pkgIdx = 0;
		// there is no find/search with indices in C++? :'(
		// can maybe solved with one of the newer std::* thingies
		for ( vector<string>::const_iterator it = ms.begin(); it < ms.end() && *it != o.model; it++, mdlIdx++ ) { }
		for ( vector<Package>::const_iterator it = ps.cbegin(); it < ps.end() && *it != o.pkg; it++, pkgIdx++ ) { }
		bitMsks[MDL] |= 1 << mdlIdx;
		bitMsks[PKG] |= 1 << pkgIdx;
	}

	int pressedKey = 0;
	do {
		switch ( pressedKey ) {
			case 9 /* tab */:
			case KEY_BTAB:
				intIdx = intIdx == MDL ? PKG : MDL;
				break;
			case KEY_UP:
				selIdx[intIdx] = selIdx[intIdx] > 0 ? selIdx[intIdx] - 1 : maxIdx[intIdx];
				break;
			case KEY_DOWN:
				selIdx[intIdx] = selIdx[intIdx] < maxIdx[intIdx] ? selIdx[intIdx] + 1 : 0;
				break;
			case ' ':
				bitMsks[intIdx] ^= 1 << selIdx[intIdx];
				break;
		}

		int colMdl = 4;
		int colPkg = center.maxCols() - FIELD_WIDTH_PKG - 4 - 4;
		int row = 3;
		for ( int i = 0; i < ms.size(); i++ ) {
			center.add(row + i, colMdl, bitMsks[MDL] & (1 << i) ? " [X] " : " [ ] ");
			center.add(ms[i], i == selIdx[MDL] && intIdx == MDL ? A_STANDOUT : A_NORMAL);
		}
		for ( int i = 0; i < ps.size(); i++ ) {
			center.add(row + i, colPkg, ps[i], i == selIdx[PKG] && intIdx == PKG ? A_STANDOUT : A_NORMAL);
			center.add(row + i, colPkg + FIELD_WIDTH_PKG, bitMsks[PKG] & (1 << i) ? "[X] " : "[ ] ");
		}

		// naive way of moving cursor where it doesn't bother as much, should hide it somehow TODO
		center.moveCursor(center.maxRows() - 1, center.maxCols() - 1);
		center.paint();
	} while ( (pressedKey = wgetch(center)) != KEY_ENTER && pressedKey != 10 );

	vector<db::Orderable> fltrd;
	for ( const db::Orderable &o : odv ) {
		mdlIdx = 0, pkgIdx = 0;
		for ( vector<string>::const_iterator it = ms.begin(); it < ms.end() && *it != o.model; it++, mdlIdx++ ) { }
		for ( vector<Package>::const_iterator it = ps.cbegin(); it < ps.end() && *it != o.pkg; it++, pkgIdx++ ) { }

		// asserts will never hit if models/packages/orderables are correctly retrieved from the DB
		assert(mdlIdx < ms.size());
		assert(pkgIdx < ps.size());

		if ( bitMsks[MDL] & (1 << mdlIdx) && bitMsks[PKG] & (1 << pkgIdx) ) {
			fltrd.push_back(o);
		}
	}
	return Configset(base, fltrd);
}

// reordering packages, given vector is reordered
void reorderPackages(vector<Package> &pkgs) {
	static const string TEXT = "Reorder packages as they are in the datasheet";
	static const string KEYS = "TAB/STAB Select L/R Move Enter/Return Confirm"; // only here for calculations
	static const int TEXT_WIDTH = max(TEXT.length(), KEYS.length());

	int PKGS_WIDTH = pkgs.size() * (FIELD_WIDTH_PKG + 1) - 1;

	int WIN_WIDTH = max(TEXT_WIDTH, PKGS_WIDTH) + 2 /* Whitespace Left/right  */ + 2 /* border */;
	int WIN_HEIGHT = 3 /* text & empty line */ + 1 /* line with packages */ + 1 /* empty line */ + 2 /* border */;

	// TODO: doesn't care about resizing or too small a screen
	BorderedWindow center(WIN_HEIGHT, WIN_WIDTH, (LINES - WIN_HEIGHT) / 2, (COLS - WIN_WIDTH) / 2);
	center.setTitle("Ordering");
	center.add(0, (center.maxCols() - TEXT.length()) / 2, TEXT);
	center.moveCursor(1, (center.maxCols() - KEYS.length()) / 2 - 1);
	displayHotkey(center, "Select", "TAB/STAB");
	displayHotkey(center, "Move", vector<chtype>{ACS_LARROW, '/', ACS_RARROW});
	displayHotkey(center, "Confirm", "Enter/Return");

	int totalPkgsLength = -1;
	for ( const Package &pkg : pkgs ) {
		totalPkgsLength += pkg.drawing.length() + to_string(pkg.pins).length() + 1;
	}

	center.paint();

	// simple manual key detection
	// cccurses should have a good way to capture these without checking key codes etc
	// current KeyEventProducer might be too much for this purpose? should try it out really
	int slIdx = 0;
	int maxIdx = pkgs.size() - 1;
	int pressedKey = 0;
	do {
		switch ( pressedKey ) {
			case 9 /* tab */:
				// allow rollover during selection
				slIdx = (slIdx == maxIdx) ? 0 : slIdx + 1;
				break;
			case KEY_BTAB:
				slIdx = (slIdx == 0) ? maxIdx : slIdx - 1;
				break;
			case KEY_LEFT:
				// no roll-over during move
				if ( slIdx > 0 ) {
					Package temp = pkgs[slIdx];
					pkgs[slIdx] = pkgs[slIdx - 1];
					pkgs[--slIdx] = temp; // hehe
				}
				break;
			case KEY_RIGHT:
				if ( slIdx < maxIdx ) {
					Package temp = pkgs[slIdx];
					pkgs[slIdx] = pkgs[slIdx + 1];
					pkgs[++slIdx] = temp; // hehe
				}
				break;
		}

		int col = (WIN_WIDTH - 4 - totalPkgsLength) / 2;
		center.clearLine(3);
		for ( int i = 0; i < pkgs.size(); i++ ) {
			const string pkg = pkgs[i].drawing + to_string(pkgs[i].pins);
			center.add(3, col, pkg, i == slIdx ? A_STANDOUT : A_NORMAL);
			col += pkg.length() + 1;
		}

		// naive way of moving cursor where it doesn't bother as much, should hide it somehow TODO
		center.moveCursor(WIN_HEIGHT - 3, WIN_WIDTH - 3);
		center.paint();
	} while ( (pressedKey = wgetch(center)) != KEY_ENTER && pressedKey != 10 );
}

// open a window in middle of screen, present all options to user; return chosen datasheet
string searchDatasheet(const unordered_map<string, string> &dsModels, const int modelFieldWidth) {
	// TODO: also add some help on this window/form fields using F1
	static const string DATASHEET_HDR("Datasheet ");
	static const string MODEL_HDR("Model ");
	static const string ERROR("Invalid/unknown datasheet or model!");

	const int LINE = 1;
	const int COL = 1;
	const int ML_FIELD_COL = MODEL_HDR.length() + 1;
	const int DS_FIELD_COL = max<int>(DATASHEET_HDR.length() + 1, ML_FIELD_COL + modelFieldWidth - FIELD_WIDTH_DATASHEET);
	const int MENU_COL = max(DS_FIELD_COL + FIELD_WIDTH_DATASHEET, ML_FIELD_COL + modelFieldWidth) + 2;

	static const int WIN_WIDTH = MENU_COL + modelFieldWidth + 1 + 2 /* border */;
	static const int WIN_HEIGHT = 6 + 2 /* border */;

	// TODO: doesn't care about resizing or too small a screen
	BorderedWindow center(WIN_HEIGHT, WIN_WIDTH, (LINES - WIN_HEIGHT) / 2, (COLS - WIN_WIDTH) / 2);
	center.setTitle("Search");

	FormBuilder fb;
	Field dsField(1, FIELD_WIDTH_DATASHEET, LINE + 0, DS_FIELD_COL);
	dsField.optionAutoSkip(Toggle::OFF);
	dsField.setBuffer("SLAS"); // default can be deleted by user
	fb.addField(dsField);
	// from database or also hard-coded constant
	Field mdField(1, modelFieldWidth, LINE + 2, ML_FIELD_COL);
	mdField.setBuffer("MSP430"); // default can be deleted by user
	mdField.optionAutoSkip(Toggle::OFF);
	fb.addField(mdField);
	UpperCasingForm form = fb.build<UpperCasingForm>(center);

	center.add(LINE + 0, COL, DATASHEET_HDR);
	center.add(LINE + 2, COL, MODEL_HDR);

	// static const string BUTTON_TEXT = "[OPEN]"; // button not part of MVP, enter works always
	static const string BUTTON_TEXT = "[ENTER TO OPEN]"; // MVP
	center.add(WIN_HEIGHT - 3, (WIN_WIDTH - BUTTON_TEXT.length()) / 2, BUTTON_TEXT, COLOR_PAIR(COLOR_PAIR_BUTTON_SELECTED));
	center.paint();

	string validDs;
	while ( validDs.empty() ) {
		form.loop();
		string dsId = dsField.buffer<string>();
		string mdl = mdField.buffer<string>();
		if ( dsModels.find(mdl) != dsModels.end() ) { // model comes first if exists
			validDs = dsModels.at(mdl);
		} else if ( dsId.length() == FIELD_WIDTH_DATASHEET ) { // then datasheet, if correct length
			// need to iterate all since there is no simple collection of datasheets
			for ( const pair<string, string> &pr : dsModels ) {
				if ( pr.second == dsId ) {
					return dsId;
				}
			}
		}
		center.add(LINE - 1, COL, ERROR); // just display error, window will disappear anyway
	}
	return validDs;
}

// hotkey helpers
void displayHotkey(Window &win, const string &text, const vector<chtype> &keys) {
	win.add(' ');
	for ( const chtype key : keys ) {
		win.add(key | A_STANDOUT);
	}
	win.add(' ');
	win.add(text);
}

void displayHotkey(Window &win, const string &text, const string &key) {
	win.add(' ');
	win.add(key, A_STANDOUT);
	win.add(' ');
	win.add(text);
}

void displayBrowseHotkeys(Window &win, const PinSetView &vw) {
	static const int EXL = strlen("ESC QUIT F2 Sort U/D Nav. Enter Edit Ins Insert Del Delete");
	bool shorten = win.maxCols() < EXL; // terminal 80 characters not all available here!
	win.clearLine(0);
	displayHotkey(win, "QUIT", "ESC");
	displayHotkey(win, "Sort", "F2");
	displayHotkey(win, "Nav.", vector<chtype>({ACS_UARROW, '/', ACS_DARROW}));
	displayHotkey(win, "Edit", shorten ? "Ret" : "Enter");
	if ( vw.canInsert() ) {
		displayHotkey(win, shorten ? "In" : "Insert", "Ins");
	}
	displayHotkey(win, shorten ? "Dl" : "Delete", "Del");
	win.paint();
}

void displayEditHotkeys(Window &win) {
	win.clearLine(0);
	displayHotkey(win, "Nav.", "TAB/STAB");
	displayHotkey(win, "Confirm", "Enter");
	win.paint();
}

}} // namespace electarus::ui
