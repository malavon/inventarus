#include "database-files.hpp"

#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <set>
#include <sstream>

#include "database.hpp"

#ifndef DB_DIRECTORY
  #error "add -DDB_DIRECTORY=\"...\" to the compiler command line"
#endif

using namespace std;
using namespace std::filesystem;

namespace electarus { namespace db {

// constants for formatting of column widths
int MAX_WIDTH_CONFIGSET_INDEX = 1;
int MAX_WIDTH_NULL = 4;
int MAX_WIDTH_ORDERABLE = 18 + 2;
int MAX_WIDTH_PKGDRW = 3 + 2;
int MAX_WIDTH_PIN = 3;
int MAX_WIDTH_PINSET = 4; // id=1-999, but NULL values possible thus 4
int MAX_WIDTH_SIGNAL = 11 + 2;
int MAX_WIDTH_SIGNAL_INDEX = 1; // actually log10 of signal width (without quotes)
int MAX_WIDTH_SIGNALSET = 5;	//id=1-99999
int MAX_WIDTH_SIGNALGROUP = 9 + 2;

// privately used functions forward declarations

// export from a simple query, cannot export joined tables etc
void exportFromPrepStmt(sqlite3_stmt *statement, const string fileName, const ExportConfig &config = ExportConfig{});
string insertStringFromResultSet(sqlite3_stmt *stmt, const vector<int> &widths = {});
string updateStringFromResultSet(sqlite3_stmt *stmt, const vector<int> &widths = {}, int whereColumns = 1);

// partial export functions for datasheet data
void exportDevicesFor(sqlite3 *db, const string &datasheetId, const string filename, const ExportConfig &);
void exportDeviceFeaturesFor(sqlite3 *db, const string &datasheetId, const string filename, const ExportConfig &);
void exportOrderablesFor(sqlite3 *db, const string &datasheetId, const string filename, const ExportConfig &);
void exportOrderablePinsetsFor(sqlite3 *db, const string &datasheetId, const string filename, const ExportConfig &);
void exportPinsetsFor(sqlite3 *db, const string &datasheetId, const string filename, const ExportConfig &);
void exportPinsetSignalsetsFor(sqlite3 *db, const string &datasheetId, const string filename, const ExportConfig &);
void exportSignalsetsFor(sqlite3 *db, const string &datasheetId, const string filename, const ExportConfig &);
void exportSignalsetSignalsFor(sqlite3 *db, const string &datasheetId, const string filename, ExportConfig);

enum class ColumnDecl { NULL_VALUE, NUMBER, TEXT };

string inferColumnName(sqlite3_stmt *stmt, int idx) {
	const char *origin = sqlite3_column_origin_name(stmt, idx); // column name from DB
	if ( origin == nullptr ) {
		const char *col = sqlite3_column_name(stmt, idx); // column name from AS-statement
		return col == nullptr ? "ERROR_NO_COL_NAME" : col;
	} else {
		return origin;
	}
}

ColumnDecl inferColumnDecl(sqlite3_stmt *stmt, int idx) {
	const char *type = sqlite3_column_decltype(stmt, idx);
	const unsigned char *value = sqlite3_column_text(stmt, idx);
	if ( value == nullptr ) {
		return ColumnDecl::NULL_VALUE;
	} else if ( strcmp(type, "INTEGER") == 0 || strcmp(type, "REAL") == 0 ) {
		return ColumnDecl::NUMBER;
	} else {
		return ColumnDecl::TEXT;
	}
}

// basically reads all data from the statement, calculates maxima, then resets statement again
void inferColwidthsFromData(sqlite3_stmt *stmt, ExportConfig &config) {
	const int columns = sqlite3_column_count(stmt);
	config.colWidths.resize(columns); // base it one the config in case something exists already!
	const char *val;
	int qt;
	while ( sqlite3_step(stmt) == SQLITE_ROW ) {
		for ( int c = 0; c < columns; c++ ) {
			ColumnDecl cd = inferColumnDecl(stmt, c);
			qt = ColumnDecl::TEXT == cd ? 2 : 0; // for the 2 ' quotes
			val = reinterpret_cast<const char *>(sqlite3_column_text(stmt, c));
			config.colWidths[c] = max<int>(config.colWidths[c], val == nullptr ? MAX_WIDTH_NULL : strlen(val) + qt);
		}
	}
	sqlite3_reset(stmt);
}

string paddingTo(const int actual, const vector<int> &widths, int idx) {
	if ( widths.size() > idx && actual < widths[idx] /* required width */ ) {
		string pad;
		pad.resize(widths[idx] - actual, ' ');
		return pad;
	}
	return "";
}

string insertStringFromResultSet(sqlite3_stmt *stmt, const vector<int> &widths) {
	const int columns = sqlite3_column_count(stmt);
	if ( columns == 0 ) {
		return "ERROR NO COLUMNS";
	}

	const char *table = sqlite3_column_table_name(stmt, 0);
	stringstream insert;
	insert << "INSERT INTO ";
	insert << table;
	insert << " (";
	for ( int c = 0; c < columns; c++ ) {
		insert << (c > 0 ? ", " : "") << inferColumnName(stmt, c);
	}
	insert << ") VALUES (";

	for ( int c = 0; c < columns; c++ ) {
		insert << (c > 0 ? ", " : "");
		ColumnDecl cd = inferColumnDecl(stmt, c);
		const char *value = reinterpret_cast<const char *>(sqlite3_column_text(stmt, c));
		if ( ColumnDecl::NULL_VALUE == cd ) {
			insert << paddingTo(MAX_WIDTH_NULL, widths, c) << "NULL";
		} else if ( ColumnDecl::NUMBER == cd ) {
			insert << paddingTo(strlen(value), widths, c) << value;
		} else /* ColumnDecl::Text */ {
			insert << '\'' << value << '\'' << paddingTo(strlen(value) + 2 /* quotes */, widths, c);
		}
		const char *type = sqlite3_column_decltype(stmt, c);
	}
	insert << ");";

	return insert.str();
}

// creates an update string from a resultset, first column is required to be the key on which to update!
string updateStringFromResultSet(sqlite3_stmt *stmt, const vector<int> &widths, int whereColumns) {
	const int columns = sqlite3_column_count(stmt);
	if ( columns <= whereColumns ) {
		return "ERROR NOT ENOUGH COLUMNS";
	}

	const char *table = sqlite3_column_table_name(stmt, 0);
	string sql = "UPDATE ";
	sql += table;
	stringstream updates;
	updates << " SET ";
	stringstream condition;
	condition << " WHERE ";

	stringstream sub;
	for ( int c = 0; c < columns; c++ ) {
		sub << (c > 1 && c != whereColumns + 1 ? "," : "");
		sub << inferColumnName(stmt, c) << " = ";
		ColumnDecl cd = inferColumnDecl(stmt, c);
		const char *value = reinterpret_cast<const char *>(sqlite3_column_text(stmt, c));
		if ( ColumnDecl::NULL_VALUE == cd ) {
			sub << paddingTo(MAX_WIDTH_NULL, widths, c) << "NULL";
		} else if ( ColumnDecl::NUMBER == cd ) {
			sub << paddingTo(strlen(value), widths, c) << value;
		} else {
			sub << '\'' << value << '\'' << paddingTo(strlen(value) + 2 /* quotes */, widths, c);
		}

		if ( c == whereColumns - 1 ) {
			condition << sub.str();
			sub = stringstream(); // sub.clear does not work???
		}
	}
	updates << sub.str();

	return sql + updates.str() + condition.str() + ";";
}

void exportFromPrepStmt(sqlite3_stmt *stmt, const string fileName, const ExportConfig &cf) {
	ExportConfig config = cf;
	std::filesystem::path outFile(DB_DIRECTORY);
	outFile /= fileName;
	std::ofstream out(outFile, config.appendFile ? (ios::out | ios::app) : ios::out); // output & append (todo)

	if ( config.fmt == ExportConfig::Format::CALCULATE ) {
		inferColwidthsFromData(stmt, config);
	}

	if ( !config.appendFile ) {
		out << "--" << endl;
		out << "-- File generated with one of the sjabloon 430 database tools." << endl;
		out << "-- Any alterations to this data will be kept by these tools, assuming the SQL is still valid." << endl;
		out << "-- DO NOT REFORMAT THIS FILE AND CHECK IN ONLY AFTER RE-EXPORTING WITH A ELECTARUS TOOL." << endl;
		out << "--" << endl;
		out << "-- Text encoding used: UTF-8" << endl;
		out << "--" << endl;
	}

	if ( config.tx == ExportConfig::Tx::BOTH || config.tx == ExportConfig::Tx::BEGIN ) {
		out << "BEGIN TRANSACTION;" << endl << endl;
	}

	int rc = sqlite3_step(stmt);
	while ( rc == SQLITE_ROW ) {
		if ( config.sql == ExportConfig::SQL::INSERT ) {
			out << insertStringFromResultSet(stmt, config.colWidths) << endl;
		} else {
			out << updateStringFromResultSet(stmt, config.colWidths) << endl;
		}
		if ( (rc = sqlite3_step(stmt)) == SQLITE_DONE ) { // one more endline AFTER block for this query
			out << endl;
		}
	}

	if ( config.tx == ExportConfig::Tx::BOTH || config.tx == ExportConfig::Tx::COMMIT ) {
		out << "COMMIT TRANSACTION;" << endl;
	}
	out.close();
}

// sub-functions for exports of pinout data

void exportDevicesFor(sqlite3 *db, const string &datasheetId, const string filename, const ExportConfig &expConf) {
	static const char *QUERY = "SELECT * "
				   "FROM device "
				   "WHERE datasheet_id = ? "
				   "ORDER BY model ASC";
	static sqlite3_stmt *stmt;
	if ( stmt == nullptr ) {
		prepare(db, &stmt, QUERY);
	}

	sqlite3_reset(stmt);
	sqlite3_bind_text(stmt, 1, datasheetId.c_str(), -1, SQLITE_STATIC);
	exportFromPrepStmt(stmt, filename, expConf);
}

void exportDeviceFeaturesFor(sqlite3 *db, const string &datasheetId, const string filename, const ExportConfig &expConf) {
	static const char *QUERY = "SELECT device_id, feature_id, param1, param2, param3, df.comment "
				   "FROM device_feature df "
				   "INNER JOIN device d ON df.device_id = d.model "
				   "WHERE d.datasheet_id = ? "
				   "ORDER BY df.device_id ASC";
	static sqlite3_stmt *stmt;
	if ( stmt == nullptr ) {
		prepare(db, &stmt, QUERY);
	}

	sqlite3_reset(stmt);
	sqlite3_bind_text(stmt, 1, datasheetId.c_str(), -1, SQLITE_STATIC);
	exportFromPrepStmt(stmt, filename, expConf);
}

void exportOrderablesFor(sqlite3 *db, const string &datasheetId, const string filename, const ExportConfig &expConf) {
	// cannot use select *, pinset_id is forced to NULL so it can be updated with an update statement AFTER
	// pinset export!
	static const char *QUERY = "SELECT name, device_id, drawing, pins, "
				   "status, msl_level, o.op_temp_min, o.op_temp_max, o.comment "
				   "FROM orderable o "
				   "INNER JOIN device d ON d.model = o.device_id "
				   "WHERE d.datasheet_id = ? "
				   "ORDER BY name ASC;";
	static sqlite3_stmt *stmt;
	if ( stmt == nullptr ) {
		prepare(db, &stmt, QUERY);
	}

	sqlite3_reset(stmt);
	sqlite3_bind_text(stmt, 1, datasheetId.c_str(), -1, SQLITE_STATIC);
	exportFromPrepStmt(stmt, filename, expConf);
}

void exportOrderablePinsetsFor(sqlite3 *db, const string &datasheetId, const string filename, const ExportConfig &expConf) {
	// order on pinset_id is first because they're grouped together
	// order including d.model because names are not always logical
	static const char *QUERY = "SELECT name, pinset_id "
				   "FROM orderable o "
				   "INNER JOIN device d ON d.model = o.device_id "
				   "WHERE pinset_id IS NOT NULL "
				   "AND d.datasheet_id = ? "
				   "ORDER BY drawing ASC, pins ASC, pinset_id ASC, d.model ASC, o.name ASC";

	static sqlite3_stmt *stmt;
	if ( stmt == nullptr ) {
		prepare(db, &stmt, QUERY);
	}

	sqlite3_reset(stmt);
	sqlite3_bind_text(stmt, 1, datasheetId.c_str(), -1, SQLITE_STATIC);
	exportFromPrepStmt(stmt, filename, expConf.sqlOverride(ExportConfig::SQL::UPDATE)); // use update statements
}

void exportPinsetsFor(sqlite3 *db, const string &datasheetId, const string filename, const ExportConfig &expConf) {
	static const char *QUERY = "SELECT ps.* "
				   "FROM pinset ps "
				   "WHERE ps.id IN ("
				   "	SELECT id "
				   "	FROM pinset_id_view "
				   "	WHERE datasheet_id = ? )"
				   "ORDER BY id ASC";
	static sqlite3_stmt *stmt;
	if ( stmt == nullptr ) {
		prepare(db, &stmt, QUERY);
	}

	sqlite3_reset(stmt);
	sqlite3_bind_text(stmt, 1, datasheetId.c_str(), -1, SQLITE_STATIC);
	exportFromPrepStmt(stmt, filename, expConf);
}

void exportPinsetSignalsetsFor(sqlite3 *db, const string &datasheetId, const string filename, const ExportConfig &expConf) {
	static const char *QUERY = "SELECT psss.* "
				   "FROM pinset_signalset psss "
				   "INNER JOIN pinset ps ON ps.id = psss.pinset_id "
				   "INNER JOIN signalset ss on psss.signalset_id = ss.id " // for correct ordering
				   "WHERE ps.id IN ("
				   "	SELECT id "
				   "	FROM pinset_id_view "
				   "	WHERE datasheet_id = ? )"
				   "ORDER BY datasheet_idx ASC, pinset_id ASC, pin_bga_row ASC, pin_number ASC";
	static sqlite3_stmt *stmt;
	if ( stmt == nullptr ) {
		prepare(db, &stmt, QUERY);
	}

	sqlite3_reset(stmt);
	sqlite3_bind_text(stmt, 1, datasheetId.c_str(), -1, SQLITE_STATIC);
	exportFromPrepStmt(stmt, filename, expConf);
}

void exportSignalsetsFor(sqlite3 *db, const string &datasheetId, const string filename, const ExportConfig &expConf) {
	static const char *QUERY = "SELECT ss.* "
				   "FROM signalset ss "
				   "WHERE ss.id IN ( "
				   "	SELECT id "
				   "	FROM signalset_id_view siv "
				   "	WHERE siv.pinset_id IN ( "
				   "		SELECT id "
				   "		FROM pinset_id_view "
				   "		WHERE datasheet_id = ? ))"
				   "GROUP BY ss.id "
				   "ORDER BY datasheet_idx ASC, ifnull(ss.parent_id, ss.id) ASC"; // order parent/child
	static sqlite3_stmt *stmt;
	if ( stmt == nullptr ) {
		prepare(db, &stmt, QUERY);
	}

	sqlite3_reset(stmt);
	sqlite3_bind_text(stmt, 1, datasheetId.c_str(), -1, SQLITE_STATIC);
	exportFromPrepStmt(stmt, filename, expConf);
}

void exportSignalsetSignalsFor(sqlite3 *db, const string &datasheetId, const string filename, ExportConfig expConf) {
	static const char *QUERY = "SELECT sss.signalset_id, sss.idx, sss.signal_id "
				   "FROM signalset ss "
				   "INNER JOIN signalset_signal sss ON ss.id = sss.signalset_id "
				   "WHERE ss.id IN ( "
				   "	SELECT id "
				   "	FROM signalset_id_view siv "
				   "	WHERE siv.pinset_id IN ("
				   "		SELECT id "
				   "		FROM pinset_id_view "
				   "		WHERE datasheet_id = ? ))"
				   "GROUP BY sss.signalset_id, sss.signal_id "
				   "ORDER BY ss.datasheet_idx ASC, sss.idx ASC"; // groups child/parents together!
	static sqlite3_stmt *stmt;
	if ( stmt == nullptr ) {
		prepare(db, &stmt, QUERY);
	}

	sqlite3_reset(stmt);
	sqlite3_bind_text(stmt, 1, datasheetId.c_str(), -1, SQLITE_STATIC);
	expConf.colWidths.resize(3);
	expConf.colWidths[2] = MAX_WIDTH_SIGNAL; // override signal width to always maximize
	exportFromPrepStmt(stmt, filename, expConf);
}

// publicly exposed export functions

void exportDataForDatasheet(sqlite3 *db, const string &datasheetId) {
	string lower = datasheetId; // is there no single-function for strings or even const char*??
	for ( char &c : lower ) {
		c = std::tolower(c);
	}
	const string file = "90_" + lower + ".sql";
	ExportConfig start{.tx = ExportConfig::Tx::BEGIN, .fmt = ExportConfig::Format::CALCULATE};
	ExportConfig middle = start.appendOverride(true).txOverride(ExportConfig::Tx::NONE);
	ExportConfig end = middle.txOverride(ExportConfig::Tx::COMMIT);
	exportDevicesFor(db, datasheetId, file, start);
	exportDeviceFeaturesFor(db, datasheetId, file, middle);
	exportOrderablesFor(db, datasheetId, file, middle);
	exportPinsetsFor(db, datasheetId, file, middle);
	exportSignalsetsFor(db, datasheetId, file, middle);
	exportPinsetSignalsetsFor(db, datasheetId, file, middle);
	exportSignalsetSignalsFor(db, datasheetId, file, middle);
	exportOrderablePinsetsFor(db, datasheetId, file, end);
}

void exportDatasheets(sqlite3 *db) {
	static const char *QUERY = "SELECT ds.* "
				   "FROM datasheet ds "
				   "ORDER BY id ASC";
	static sqlite3_stmt *stmt;
	if ( stmt == nullptr ) {
		prepare(db, &stmt, QUERY);
	}
	sqlite3_reset(stmt);
	exportFromPrepStmt(stmt, "20_datasheet.sql", ExportConfig{.fmt = ExportConfig::Format::CALCULATE});
}

void exportPackages(sqlite3 *db) {
	static const char *QUERY = "SELECT drawing, pins, type, comment "
				   "FROM package "
				   "ORDER BY drawing ASC, pins ASC";
	static sqlite3_stmt *stmt;
	if ( stmt == nullptr ) {
		prepare(db, &stmt, QUERY);
	}
	sqlite3_reset(stmt);
	exportFromPrepStmt(stmt, "23_package.sql", ExportConfig{.fmt = ExportConfig::Format::CALCULATE});
}

void exportSignals(sqlite3 *db) {
	static const char *SGROUPS = "SELECT sg.* "
				     "FROM signalgroup sg "
				     "ORDER BY name";
	static const char *SIGNALS = "SELECT signalgroup, id, alias_for, desc "
				     "FROM signal s "
				     "ORDER BY signalgroup ASC, ifnull(alias_for, id) ASC, alias_for ASC";
	static sqlite3_stmt *grpStmt, *sgnStmt;
	if ( sgnStmt == nullptr ) {
		prepare(db, &grpStmt, SGROUPS);
		prepare(db, &sgnStmt, SIGNALS);
	}

	vector<int> cw = {MAX_WIDTH_SIGNALGROUP, MAX_WIDTH_SIGNAL, MAX_WIDTH_SIGNAL};

	exportFromPrepStmt(grpStmt, "24_signal.sql", ExportConfig{.tx = ExportConfig::Tx::BEGIN});
	exportFromPrepStmt(sgnStmt, "24_signal.sql",
			   ExportConfig{.appendFile = true, .tx = ExportConfig::Tx::COMMIT, .colWidths = cw});
}

// maybe this should be a function shared with other programs
void importDatabase(sqlite3 *db, std::function<void(const std::string &file, const int lineNr, const char *error)> callback) {
	std::filesystem::path dbDir(DB_DIRECTORY);
	// iterate all files, sort alphabetically/numerically and check if it's actually a (SQL) file
	// TODO: recursive, should also include pinout per device later, simplifies files
	set<path> files;
	directory_iterator it(dbDir);
	for ( const directory_entry &dir_entry : it ) {
		if ( dir_entry.is_regular_file() && dir_entry.path().extension() == ".sql" ) {
			files.insert(dir_entry.path());
		}
	}

	// insert all files into in-memory DB
	std::string line, stmt;
	int lineNr, cmtIdx;
	char *errorMsg = nullptr;
	for ( const path &p : files ) {
		std::ifstream input(p);
		lineNr = 0;
		errorMsg = nullptr;

		while ( errorMsg == nullptr && input ) {
			stmt.clear();
			do {
				lineNr++;
				std::getline(input, line);
				cmtIdx = line.find("--");
				if ( cmtIdx == std::string::npos ) {
					stmt += line;
				} else {
					stmt += line.substr(0, cmtIdx);
				}
				// line ends in newline character
				// sqlite3 removes linebreaks from errormessages and concatenates lines without whitespace
				// resulting in ugly error; prevent that by adding a space
				// is NOT necessary for correct execution though
				stmt += ' ';
			} while ( input && (line.empty() || line.back() != ';') );
			sqlite3_exec(db, stmt.c_str(), nullptr /*NULL callback is valid?*/, nullptr, &errorMsg);
		}

		callback(p.filename(), lineNr, errorMsg);
		if ( errorMsg != nullptr ) {
			// rollback transaction; if there is no transaction, the error is ignored
			// without rolling back the current transaction, all following files will error, possibly without real cause
			sqlite3_exec(db, "ROLLBACK TRANSACTION;", nullptr, nullptr, nullptr);
			sqlite3_free(errorMsg);
		}
	}
}

}} // namespace electarus::db
