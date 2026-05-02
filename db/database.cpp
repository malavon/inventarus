#include "database.hpp"

#include <cassert>
#include <iostream>
#include <unordered_set>

#include "util.hpp"

namespace electarus { namespace db {

using std::unordered_set;

sqlite3 *createDatabase() {
	sqlite3 *db;
	// in-memory is preferred, but exporting should be simple, formatted and not DIY
	// if not, use a file database?
	// unsigned int rc = sqlite3_open("file:debug.db", &db);
	unsigned int rc = sqlite3_open("file::memory:", &db);

	if ( rc != 0 ) {
		sqlite3_close(db);
		db = nullptr;
	} else {
		// this is apparently required for sqlite3 to behave like an actual database ...
		sqlite3_exec(db, "PRAGMA foreign_keys = ON", nullptr, nullptr, nullptr);
	}
	return db;
}

/* Query DB */

void prepare(sqlite3 *db, sqlite3_stmt **stmt, const char *QUERY) {
	int rc = sqlite3_prepare_v2(db, QUERY, -1, stmt, NULL);
	if ( rc != SQLITE_OK ) {
		std::cerr << "SQLite3 error " << sqlite3_errmsg(db) << std::endl;
	}
	assert(rc == SQLITE_OK);
}

DatabaseTotals countTotals(sqlite3 *db) {
	// a single result row is best, 1 column per count
	// this is the best I can do with my knowledge of sqlite
	static const char *QUERY = "SELECT SUM(ds), SUM(dv), SUM(odbl) "
				   "FROM ("
				   "	SELECT 0 AS ds, 0 AS dv, 0 AS odbl "
				   "	UNION SELECT COUNT( * ), 0, 0 FROM datasheet "
				   "	UNION SELECT 0, COUNT( * ), 0 FROM device "
				   "	UNION SELECT 0, 0, COUNT( * ) FROM orderable )";

	static sqlite3_stmt *stmt;
	if ( stmt == nullptr ) {
		prepare(db, &stmt, QUERY);
	}

	DatabaseTotals totals{-1, -1, -1};
	if ( sqlite3_step(stmt) == SQLITE_ROW ) {
		totals.datasheets = sqlite3_column_int(stmt, 0);
		totals.devices = sqlite3_column_int(stmt, 1);
		totals.orderables = sqlite3_column_int(stmt, 2);
	}

	return totals;
}

DatabaseTotals countSupported(sqlite3 *db) {
	// a single result row is best, 1 column per count
	// this is the best I can do with my knowledge of sqlite
	static const char *QUERY = "SELECT SUM(ds), SUM(dv), SUM(odbl) FROM ("
				   "	SELECT 0 AS ds, 0 AS dv, 0 AS odbl "
				   "UNION "
				   "	SELECT count(DISTINCT ds.id), 0, 0 "
				   "	FROM datasheet ds "
				   "	INNER JOIN device d ON ds.id = d.datasheet_id "
				   "	INNER JOIN orderable o ON d.model = o.device_id "
				   "	WHERE o.pinset_id NOT NULL "
				   "UNION"
				   "	SELECT 0, COUNT(DISTINCT d.model), 0 "
				   "	FROM device d "
				   "	INNER JOIN orderable o ON d.model = o.device_id "
				   "	WHERE o.pinset_id NOT NULL "
				   "UNION"
				   "	SELECT 0, 0, COUNT(DISTINCT o.name) "
				   "	FROM orderable o "
				   "	WHERE o.pinset_id NOT NULL )";

	static sqlite3_stmt *stmt;
	if ( stmt == nullptr ) {
		prepare(db, &stmt, QUERY);
	}

	DatabaseTotals totals{-1, -1, -1};
	if ( sqlite3_step(stmt) == SQLITE_ROW ) {
		totals.datasheets = sqlite3_column_int(stmt, 0);
		totals.devices = sqlite3_column_int(stmt, 1);
		totals.orderables = sqlite3_column_int(stmt, 2);
	}

	return totals;
}

Datasheet findDatasheet(sqlite3 *db, const string id) {
	static const char *QUERY = "SELECT id, revision,"
				   "CONCAT(issue_month, '/', issue_year)	issue_date,"
				   "CONCAT(rev_month, '/', rev_year)	rev_date "
				   "FROM datasheet "
				   "WHERE id=?";

	static sqlite3_stmt *stmt;
	if ( stmt == nullptr ) {
		prepare(db, &stmt, QUERY);
	}

	sqlite3_reset(stmt);
	sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_STATIC);

	// assume only a single row
	Datasheet ds;
	if ( sqlite3_step(stmt) == SQLITE_ROW ) {
		ds.id = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
		ds.rev = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
		ds.issueDate = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
		ds.revDate = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
	}
	return ds;
}

vector<Orderable> findOrderablesByDatasheet(sqlite3 *db, const string datasheetId, const vector<Pinset> &psv) {
	static const char *QUERY = "SELECT name, device_id, drawing, pins, pinset_id "
				   "FROM orderable o "
				   "INNER JOIN device d ON o.device_id = d.model "
				   "WHERE datasheet_id = ? "
				   "order by name ASC";

	static sqlite3_stmt *stmt;
	if ( stmt == nullptr ) {
		prepare(db, &stmt, QUERY);
	}

	sqlite3_reset(stmt);
	int rc = sqlite3_bind_text(stmt, 1, datasheetId.c_str(), -1, SQLITE_STATIC);
	assert(SQLITE_OK == rc);

	util::VectorIdMapAdapter<int, Pinset> pinsets(psv);
	vector<Orderable> result;
	while ( sqlite3_step(stmt) == SQLITE_ROW ) {
		Orderable o;
		o.name = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
		o.model = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
		o.pkg.drawing = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
		o.pkg.pins = sqlite3_column_int(stmt, 3);
		o.pinset.id = sqlite3_column_int(stmt, 4);
		if ( o.pinset.id != 0 && pinsets.contains(o.pinset.id) ) {
			o.pinset = pinsets[o.pinset.id];
		}
		result.push_back(o);
	}
	return result;
}

vector<Package> findPackagesByDatasheet(sqlite3 *db, const string datasheetId) {
	static const char *QUERY = "SELECT o.drawing, o.pins "
				   "FROM orderable o "
				   "INNER JOIN device d ON (d.model = o.device_id) "
				   "WHERE d.datasheet_id = ? "
				   "GROUP BY o.drawing, o.pins "
				   "ORDER BY o.drawing ASC, o.pins ASC";

	static sqlite3_stmt *stmt;
	if ( stmt == nullptr ) {
		prepare(db, &stmt, QUERY);
	}

	sqlite3_reset(stmt);
	int rc = sqlite3_bind_text(stmt, 1, datasheetId.c_str(), -1, SQLITE_STATIC);
	assert(SQLITE_OK == rc);

	vector<Package> result;
	while ( sqlite3_step(stmt) == SQLITE_ROW ) {
		string drw = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
		int pins = sqlite3_column_int(stmt, 1);
		result.push_back(Package{drw, pins});
	}
	sqlite3_reset(stmt);

	return result;
}

// fills pinset using data already in database, but Signalsets already in memory
vector<Pinset> findPinsetsByDatasheet(sqlite3 *db, const string &datasheetId, const vector<Signalset> &ssets) {
	static const char *QUERY = "SELECT id, parent_id, group_idx, pins, pss.signalset_id, pss.pin_bga_row, pss.pin_number "
				   "FROM pinset p "
				   "LEFT OUTER JOIN pinset_signalset pss ON (p.id = pss.pinset_id) "
				   "WHERE p.id IN ("
				   "	SELECT id "
				   "	FROM pinset_id_view "
				   "	WHERE datasheet_id = ? )"
				   "ORDER BY p.group_idx ASC, p.id ASC";

	static sqlite3_stmt *stmt;
	if ( stmt == nullptr ) { // assume both are null
		prepare(db, &stmt, QUERY);
	}

	util::VectorIdMapAdapter<int, Signalset> signalsets(ssets);
	unordered_set<int> psIds = {0}; // only used for asserting database consistency (or query issues)

	sqlite3_reset(stmt);
	int rc = sqlite3_bind_text(stmt, 1, datasheetId.c_str(), -1, SQLITE_STATIC);
	assert(SQLITE_OK == rc);

	vector<Pinset> ps;
	rc = sqlite3_step(stmt);
	while ( rc == SQLITE_ROW ) {
		Pinset p;
		p.id = sqlite3_column_int(stmt, 0);
		p.parentId = sqlite3_column_int(stmt, 1);
		p.cset = sqlite3_column_int(stmt, 2);
		p.pins = sqlite3_column_int(stmt, 3);
		assert(psIds.find(p.parentId) != psIds.end());
		psIds.insert(p.id);
		while ( sqlite3_column_int(stmt, 0) == p.id ) {
			int ssId = sqlite3_column_int(stmt, 4);

			const unsigned char *bgaRow = sqlite3_column_text(stmt, 5);
			Pin key;
			key.bgaRow = bgaRow == nullptr ? "" : string(reinterpret_cast<const char *>(bgaRow));
			key.number = sqlite3_column_int(stmt, 6);

			// signalset id can be 0 after changes BECAUSE of left outer join
			if ( ssId != 0 ) {
				assert(signalsets.contains(ssId)); // DB inconsistency
				p.signalsets[key] = signalsets[ssId];
			}
			rc = sqlite3_step(stmt);
		}
		ps.push_back(p);
	}

	return ps;
}

vector<Signalset> findSignalsetsByDatasheet(sqlite3 *db, const string datasheetId) {
	static const char *QUERY = "SELECT DISTINCT datasheet_idx, ss.id signalset_id, ss.parent_id, sss.idx, sss.signal_id "
				   "FROM signalset ss "
				   "LEFT OUTER JOIN pinset_signalset psss ON ss.id = psss.signalset_id "
				   "LEFT OUTER JOIN signalset_signal sss ON ss.id = sss.signalset_id "
				   "WHERE ss.id IN ("
				   "	SELECT id "
				   "	FROM signalset_id_view siv "
				   "	WHERE siv.pinset_id IN ("
				   "		SELECT id "
				   "		FROM pinset_id_view "
				   "		WHERE datasheet_id = ? ))"
				   "ORDER BY datasheet_idx ASC, ss.parent_id ASC, ss.id ASC, sss.idx ASC";
	static sqlite3_stmt *stmt;
	if ( stmt == nullptr ) {
		prepare(db, &stmt, QUERY);
	}

	// only used for asserting database consistency (or query issues)
	unordered_set<int> ssIds = {0};

	sqlite3_reset(stmt);
	int rc = sqlite3_bind_text(stmt, 1, datasheetId.c_str(), -1, SQLITE_STATIC);
	assert(SQLITE_OK == rc);

	vector<Signalset> result;
	rc = sqlite3_step(stmt);
	while ( rc == SQLITE_ROW ) {
		Signalset s;
		s.datasheetIdx = sqlite3_column_int(stmt, 0);
		s.id = sqlite3_column_int(stmt, 1);
		s.parentId = sqlite3_column_int(stmt, 2);
		assert(ssIds.find(s.parentId) != ssIds.end());
		while ( sqlite3_column_int(stmt, 1) == s.id ) {
			int dsIdx = sqlite3_column_int(stmt, 3);
			const char *sgn = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
			if ( sgn != nullptr ) {
				// the query can result in NULL signals when a signalset doesn't have signals
				// linked but is a parent to other signalsets
				s.signals[dsIdx] = sgn;
			}
			rc = sqlite3_step(stmt);
		}
		result.push_back(s);
		ssIds.insert(s.id);
	}

	return result;
}

unordered_map<string, string> listAllModelsAndDatasheets(sqlite3 *db) {
	static const char *QUERY = "SELECT model, datasheet_id FROM device";

	static sqlite3_stmt *stmt;
	if ( stmt == nullptr ) {
		prepare(db, &stmt, QUERY);
	}

	unordered_map<string, string> result;
	while ( sqlite3_step(stmt) == SQLITE_ROW ) {
		string model = string(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0)));
		string datasheet = string(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1)));
		result[model] = datasheet;
	}
	return result;
}

unordered_map<string, string> listAllSignalDescriptions(sqlite3 *db) {
	static const char *QUERY = "SELECT id, desc FROM signal";

	static sqlite3_stmt *stmt;
	if ( stmt == nullptr ) {
		prepare(db, &stmt, QUERY);
	}
	sqlite3_reset(stmt);

	unordered_map<string, string> result;
	while ( sqlite3_step(stmt) == SQLITE_ROW ) {
		string name = string(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0)));
		string desc = string(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1)));
		result[name] = desc;
	}
	return result;
}

/* Modify data in DB */

int linkOrderableToItsPinset(sqlite3 *db, const Orderable &odbl) {
	static const char *UPDATE = "UPDATE orderable "
				    "SET pinset_id = ? "
				    "WHERE name = ?";
	static sqlite3_stmt *stmt;
	if ( stmt == nullptr ) {
		prepare(db, &stmt, UPDATE);
	}

	sqlite3_reset(stmt);
	int rc = sqlite3_bind_int(stmt, 1, odbl.pinset.id);
	assert(SQLITE_OK == rc);
	rc = sqlite3_bind_text(stmt, 2, odbl.name.c_str(), -1, SQLITE_STATIC);
	assert(SQLITE_OK == rc);

	rc = sqlite3_step(stmt);
	assert(SQLITE_DONE == rc);
	return sqlite3_changes(db);
}

int saveOrUpdatePinset(sqlite3 *db, Pinset &ps) { // assumes signal sets are all in DB!
	static const char *INSERT = "INSERT INTO pinset (parent_id, group_idx, pins) VALUES (?, ?, ?)";
	static const char *UPDATE = "UPDATE pinset SET group_idx = ?, parent_id = ?, pins = ? WHERE id = ?";
	static const char *UNLINK = "DELETE FROM pinset_signalset WHERE pinset_id = ?";
	static const char *DOLINK = "INSERT INTO pinset_signalset (pinset_id, signalset_id, pin_bga_row, pin_number) "
				    "VALUES (:psetId, :ssetId, :bgaRow, :pinNumber)";
	static sqlite3_stmt *insStmt, *updStmt, *ulkStmt, *lnkStmt;
	if ( insStmt == nullptr ) { // assume both are null
		prepare(db, &insStmt, INSERT);
		prepare(db, &updStmt, UPDATE);
		prepare(db, &ulkStmt, UNLINK);
		prepare(db, &lnkStmt, DOLINK);
	}

	int alteredRows = 0, pm;
	std::array<int, 5> rc;
	// totalpins is set to # of signalsets
	if ( ps.id == 0 ) {
		pm = 0;
		sqlite3_reset(insStmt);
		if ( ps.parentId == 0 ) {
			rc[pm] = sqlite3_bind_null(insStmt, ++pm);
		} else {
			rc[pm] = sqlite3_bind_int(insStmt, ++pm, ps.parentId);
		}
		rc[pm] = sqlite3_bind_int(insStmt, ++pm, ps.cset);
		rc[pm] = sqlite3_bind_int(insStmt, ++pm, ps.signalsets.size());
		std::for_each(rc.begin() + 1, rc.begin() + 1 + pm, [](int n) { assert(n == SQLITE_OK); });
		rc[pm] = sqlite3_step(insStmt);
		assert(SQLITE_DONE == rc[pm]);
		ps.id = sqlite3_last_insert_rowid(db);
		alteredRows++;
	} else { // if already in database, update all fields, remove signalsets
		pm = 0;
		sqlite3_reset(updStmt);
		rc[pm] = sqlite3_bind_int(updStmt, ++pm, ps.cset);
		if ( ps.parentId == 0 ) {
			rc[pm] = sqlite3_bind_null(updStmt, ++pm);
		} else {
			rc[pm] = sqlite3_bind_int(updStmt, ++pm, ps.parentId);
		}
		rc[pm] = sqlite3_bind_int(updStmt, ++pm, ps.signalsets.size());
		rc[pm] = sqlite3_bind_int(updStmt, ++pm, ps.id);
		std::for_each(rc.begin() + 1, rc.begin() + 1 + pm, [](int n) { assert(n == SQLITE_OK); });
		rc[pm] = sqlite3_step(updStmt);
		assert(SQLITE_DONE == rc[pm]);
		alteredRows++;

		pm = 0;
		sqlite3_reset(ulkStmt);
		rc[pm] = sqlite3_bind_int(ulkStmt, ++pm, ps.id);
		rc[++pm] = sqlite3_step(ulkStmt);
		assert(SQLITE_OK == rc[1] && SQLITE_DONE == rc[pm]);
		alteredRows += sqlite3_changes(db);
	}

	for ( const std::pair<Pin, Signalset> &pr : ps.signalsets ) {
		pm = 0;
		const Pin pin = pr.first;
		const Signalset ss = pr.second;
		sqlite3_reset(lnkStmt);
		rc[pm] = sqlite3_bind_int(lnkStmt, ++pm, ps.id);
		rc[pm] = sqlite3_bind_int(lnkStmt, ++pm, ss.id);
		if ( pin.bgaRow.empty() ) {
			rc[pm] = sqlite3_bind_null(lnkStmt, ++pm);
		} else {
			rc[pm] = sqlite3_bind_text(lnkStmt, ++pm, pin.bgaRow.c_str(), -1, SQLITE_STATIC);
		}
		if ( pin.number == 0 ) { // 0 is in-app value for no pin, save it as NULL
			rc[pm] = sqlite3_bind_null(lnkStmt, ++pm);
		} else {
			rc[pm] = sqlite3_bind_int(lnkStmt, ++pm, pin.number);
		}
		std::for_each(rc.begin() + 1, rc.begin() + 1 + pm, [](int n) { assert(n == SQLITE_OK); });
		// execute, ignore errors for duplicates but reset statement before next
		if ( SQLITE_DONE == sqlite3_step(lnkStmt) ) {
			alteredRows++;
		}
	}
	return alteredRows;
}

int saveOrUpdateSignalset(sqlite3 *db, Signalset &ss) {
	static const char *INSERT = "INSERT INTO signalset(datasheet_idx, parent_id) VALUES (?, ?)";
	static const char *UPDATE = "UPDATE signalset SET datasheet_idx = ?, parent_id = ? WHERE id = ?";
	static const char *UNLINK = "DELETE FROM signalset_signal WHERE signalset_id= ?";
	static const char *DOLINK = "INSERT INTO signalset_signal (signalset_id, signal_id, idx) "
				    "VALUES (:ssetId, :sgnId, :idx)";
	static sqlite3_stmt *insStmt, *updStmt, *delStmt, *lnkStmt;
	if ( insStmt == nullptr ) { // assume both are null
		prepare(db, &insStmt, INSERT);
		prepare(db, &updStmt, UPDATE);
		prepare(db, &delStmt, UNLINK);
		prepare(db, &lnkStmt, DOLINK);
	}

	int alteredRows = 0, rc;
	if ( ss.id == 0 ) {
		sqlite3_reset(insStmt);
		rc = sqlite3_bind_int(insStmt, 1, ss.datasheetIdx);
		assert(SQLITE_OK == rc);
		if ( ss.parentId == 0 ) {
			rc = sqlite3_bind_null(insStmt, 2);
		} else {
			rc = sqlite3_bind_int(insStmt, 2, ss.parentId);
		}
		assert(SQLITE_OK == rc);
		rc = sqlite3_step(insStmt);
		assert(SQLITE_DONE == rc);
		ss.id = sqlite3_last_insert_rowid(db);
		alteredRows++;
		// new signalset doesn't have any signals assigned, no deletes necessary
	} else {
		sqlite3_reset(updStmt);
		rc = sqlite3_bind_int(updStmt, 1, ss.datasheetIdx);
		assert(SQLITE_OK == rc);
		if ( ss.parentId == 0 ) {
			rc = sqlite3_bind_null(updStmt, 2);
		} else {
			rc = sqlite3_bind_int(updStmt, 2, ss.parentId);
		}
		assert(SQLITE_OK == rc);
		rc = sqlite3_bind_int(updStmt, 3, ss.id);
		assert(SQLITE_OK == rc);
		if ( SQLITE_DONE == sqlite3_step(updStmt) ) {
			alteredRows += sqlite3_changes(db);
		}

		// for all others: delete all signals
		sqlite3_reset(delStmt);
		rc = sqlite3_bind_int(delStmt, 1, ss.id);
		assert(SQLITE_OK == rc);
		if ( SQLITE_DONE == sqlite3_step(delStmt) ) {
			alteredRows += sqlite3_changes(db);
		}
	}

	for ( const std::pair<int, string> &sgn : ss.signals ) {
		sqlite3_reset(lnkStmt);
		rc = sqlite3_bind_int(lnkStmt, 1, ss.id);
		assert(SQLITE_OK == rc);
		rc = sqlite3_bind_text(lnkStmt, 2, sgn.second.c_str(), -1, SQLITE_STATIC);
		assert(SQLITE_OK == rc);
		rc = sqlite3_bind_int(lnkStmt, 3, sgn.first);
		assert(SQLITE_OK == rc);
		// execute, ignore errors for duplicates but reset statement before next
		if ( SQLITE_DONE == sqlite3_step(lnkStmt) ) {
			alteredRows++;
		}
	}
	return alteredRows;
}

int saveSignals(sqlite3 *db, unordered_map<string, string> signals) {
	static const char *QUERY = "INSERT INTO SIGNAL(id, desc) VALUES (?1, ?2);";

	static sqlite3_stmt *stmt;
	if ( stmt == nullptr ) {
		int rc = sqlite3_prepare_v2(db, QUERY, -1, &stmt, NULL);
		assert(SQLITE_OK == rc);
	}

	int insertedRows = 0, rc;
	for ( const std::pair<string, string> &signal : signals ) {
		sqlite3_reset(stmt);
		rc = sqlite3_bind_text(stmt, 1, signal.first.c_str(), -1, SQLITE_STATIC);
		assert(SQLITE_OK == rc);
		rc = sqlite3_bind_text(stmt, 2, signal.second.c_str(), -1, SQLITE_STATIC);
		assert(SQLITE_OK == rc);
		if ( SQLITE_ROW == sqlite3_step(stmt) ) {
			insertedRows++;
		}
	}
	return insertedRows;
}

/* Beautification ... */

int removeGapsInPinsetIds(sqlite3 *db) {
	static const char *FINDIDS = "SELECT ps1.id, ps2.id "
				     "FROM pinset ps1 "
				     "LEFT OUTER JOIN pinset ps2 ON (ps1.id = ps2.id + 1) "
				     "WHERE ps1.id <> 1 "
				     "AND (ps2.id IS NULL OR ps1.id IS NULL)";
	static const char *CLEANUP = "UPDATE pinset "
				     "SET id = id - 1 "
				     "WHERE id >= ?";
	static sqlite3_stmt *qryStmt, *clnStmt;
	if ( qryStmt == nullptr ) {
		prepare(db, &qryStmt, FINDIDS);
		prepare(db, &clnStmt, CLEANUP);
	}

	// note that changes to other datasheet ids are also possible!
	// TODO: if the application can ever open multiple datasheets in a session, this will become a problem!
	int alteredRows = 0, id, rc;
	sqlite3_reset(qryStmt);
	while ( sqlite3_step(qryStmt) == SQLITE_ROW ) {
		sqlite3_reset(clnStmt);
		id = (sqlite3_column_int(qryStmt, 0));
		sqlite3_reset(qryStmt); // IMPORTANT: RE-EXECUTE QUERY IN CASE OF GAPS BIGGER THAN 1
		rc = sqlite3_bind_int(clnStmt, 1, id);
		assert(SQLITE_OK == rc);
		rc = sqlite3_step(clnStmt);
		assert(SQLITE_DONE == rc);
		alteredRows += sqlite3_changes(db);
	}

	return alteredRows;
}

int removeGapsInSignalsetIds(sqlite3 *db) {
	static const char *FINDIDS = "SELECT ss1.id, ss2.id "
				     "FROM signalset ss1 "
				     "LEFT OUTER JOIN signalset ss2 ON (ss1.id = ss2.id + 1) "
				     "WHERE ss1.id <> 1 "
				     "AND (ss2.id IS NULL OR ss1.id IS NULL)";
	static const char *CLEANUP = "UPDATE signalset "
				     "SET id = id - 1 "
				     "WHERE id >= ?";
	static sqlite3_stmt *qryStmt, *clnStmt;
	if ( qryStmt == nullptr ) {
		prepare(db, &qryStmt, FINDIDS);
		prepare(db, &clnStmt, CLEANUP);
	}

	// note that changes to other datasheet ids do not matter unless also exported!
	// TODO: if the application can ever open multiple datasheets in a session, this will become a problem!
	int alteredRows = 0, id, rc;
	sqlite3_reset(qryStmt);
	while ( sqlite3_step(qryStmt) == SQLITE_ROW ) {
		sqlite3_reset(clnStmt);
		id = (sqlite3_column_int(qryStmt, 0));
		sqlite3_reset(qryStmt); // IMPORTANT: RE-EXECUTE QUERY IN CASE OF GAPS BIGGER THAN 1
		rc = sqlite3_bind_int(clnStmt, 1, id);
		assert(SQLITE_OK == rc);
		rc = sqlite3_step(clnStmt);
		assert(SQLITE_DONE == rc);
		alteredRows += sqlite3_changes(db);
	}

	return alteredRows;
}

}} // namespace electarus::db
