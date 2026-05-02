#ifndef SJABLOON430_TOOLS_DATABASE_FILES_HPP
#define SJABLOON430_TOOLS_DATABASE_FILES_HPP

#include <sqlite3.h>

#include <filesystem>
#include <functional>
#include <string>

namespace electarus {
namespace db {

using std::string;

// modify export behaviour
struct ExportConfig {
	enum class Tx { NONE, BEGIN, COMMIT, BOTH };
	enum class SQL { INSERT, UPDATE };
	enum class Format { CALCULATE /* max widths from data */, FIXED /* user-specified */ };

	bool appendFile = false;
	Tx tx = Tx::BOTH;
	SQL sql = SQL::INSERT;
	Format fmt = Format::FIXED; // harmless default: use colwidth (which is empty by default)
	std::vector<int> colWidths;

	ExportConfig appendOverride(bool app) const {
		ExportConfig res(*this);
		res.appendFile = app;
		return res;
	}

	ExportConfig fmtOverride(Format ovr) const {
		ExportConfig res(*this);
		res.fmt = ovr;
		return res;
	}

	ExportConfig sqlOverride(SQL ovr) const {
		ExportConfig res(*this);
		res.sql = ovr;
		return res;
	}

	ExportConfig txOverride(Tx ovr) const {
		ExportConfig res(*this);
		res.tx = ovr;
		return res;
	}
};
// only publicly-accessible functions are declared here; privately used ones are in the .cpp file

void exportDataForDatasheet(sqlite3 *db, const string &datasheetId);
void exportDatasheets(sqlite3 *db);
void exportPackages(sqlite3 *db);
void exportSignals(sqlite3 *db);
void importDatabase(sqlite3 *db, std::function<void(const std::string &file, const int lineNr, const char *error)> callback);

}} // namespace electarus::db
#endif // SJABLOON430_TOOLS_DATABASE_FILES_HPP
