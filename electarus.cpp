
#include <iostream>
#include <set>

#include "cccurses/cccurses.hpp"
#include "cccurses/form.hpp"
#include "cccurses/window.hpp"
#include "db/database-files.hpp"
#include "db/database.hpp"
#include "db/util.hpp"
#include "ui/ui.hpp"

using namespace cccurses;
using namespace electarus;
using electarus::Package;
using electarus::Pin;

static const int WIN_TOP_HEIGHT = 6;
static const int WIN_CONFIGSET_WIDTH = 20 + 2 /* indent */ + 2 /* borders */;

static const int ROW_HOTKEYS = WIN_TOP_HEIGHT - 2 /* borders*/ - 1 /* last line */;

int main() {
	setlocale(LC_ALL, "");

	// SQLite3 in-memory DB
	sqlite3 *db = db::createDatabase();
	if ( db == nullptr ) {
		exit(-1);
	}
	// importing database happens after curses setup, showing which files are read

	/* Initialize curses */
	initCurses();

	{
		BorderedWindow top(WIN_TOP_HEIGHT, COLS - WIN_CONFIGSET_WIDTH, 0, 0);
		// TODO: no border, separate with hline or something?
		Window hotkeys = top.deriveWindow<Window>(1, top.maxCols(), ROW_HOTKEYS, 0);
		Window pins(LINES - WIN_TOP_HEIGHT, COLS - WIN_CONFIGSET_WIDTH, WIN_TOP_HEIGHT, 0);
		pins.optionScrollable(Toggle::ON);
		BorderedWindow configs(LINES, WIN_CONFIGSET_WIDTH, 0, COLS - WIN_CONFIGSET_WIDTH);
		configs.setTitle("Configsets");

		Window &statusWin = pins;
		statusWin.add(0, 0, "Importing SQLite DB:");
		int files = 0, errors = 0;
		db::importDatabase(db, [&](const string &filename, const int lineNr, const char *error) {
			files++;
			if ( error != nullptr ) {
				errors++;
				if ( errors < statusWin.maxRows() - WIN_TOP_HEIGHT - 2 /**/ ) {
					statusWin.print("\n  \"%s\" ERR (line %d): %s", filename.c_str(), lineNr, error);
				} else if ( errors < statusWin.maxRows() - WIN_TOP_HEIGHT - 1 ) {
					statusWin.add("\n  ... more errors ...");
				}
			}
		});
		statusWin.print("\n %d files imported ", files);
		statusWin.print(errors == 0 ? "without errors\n" : "with %d errors\n", errors);
		statusWin.paint();

		// initial state: open search window
		string selectedId = ui::searchDatasheet(db::listAllModelsAndDatasheets(db), 20);
		db::Datasheet selectedDS = db::findDatasheet(db, selectedId);

		db::DatabaseTotals totals = db::countTotals(db);
		db::DatabaseTotals supported = db::countSupported(db);

		ui::drawTopWindow(top, selectedDS, totals, supported);

		vector<Package> pkgs = db::findPackagesByDatasheet(db, selectedDS.id);
		// packages are also used for sets

		// /signalsets/pinsets are also linked to orderables and thus model/package
		vector<db::Signalset> signalsets = db::findSignalsetsByDatasheet(db, selectedId);
		// does not yet use config sets, but this is where the logic could go
		vector<db::Pinset> pinsets = db::findPinsetsByDatasheet(db, selectedId, signalsets);
		// orderables are used to create config sets (the thing at the right :p) -> but not yet implemented
		vector<db::Orderable> ordbls = db::findOrderablesByDatasheet(db, selectedId, pinsets);
	}

	endCurses();

	return EXIT_SUCCESS;
}
