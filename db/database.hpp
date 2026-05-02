#ifndef ELECTARUS_DATABASE_HPP
#define ELECTARUS_DATABASE_HPP

#include <sqlite3.h>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using std::string;
using std::unordered_map;
using std::unordered_set;
using std::vector;

namespace electarus {

/* Simple structs which are not really database-related */

struct Pin {
	string bgaRow;
	int number;

	bool empty() const {
		return bgaRow.empty() && number <= 0;
	}
	bool operator==(const Pin &o) const {
		return bgaRow == o.bgaRow && number == o.number;
	}
	bool operator<(const Pin &o) const { // for addition to (ordered) containers
		return bgaRow < o.bgaRow || (number < o.number && bgaRow == o.bgaRow);
	}
	operator const string() const {
		return bgaRow + (number == 0 ? "" : std::to_string(number));
	}
};

struct Package {
	string drawing;
	int pins;
	bool operator==(const Package &o) const {
		return drawing == o.drawing && pins == o.pins;
	}
	bool operator!=(const Package &o) const { // for comparison with iterators
		return drawing != o.drawing || pins != o.pins;
	}
	bool operator<(const Package &o) const { // for addition to (ordered) std::set
		return drawing < o.drawing || (pins < o.pins && drawing == o.drawing);
	}
	operator const string() const { // for consistency and ease-of-use
		return drawing + std::to_string(pins);
	}
};
} // namespace electarus:

template<>
struct std::hash<electarus::Package> {
	std::size_t operator()(const electarus::Package &p) const noexcept {
		std::size_t h1 = std::hash<std::string>{}(p.drawing);
		std::size_t h2 = p.pins;
		return h1 ^ (h2 << 1); // or use boost::hash_combine
	}
};

template<>
struct std::hash<electarus::Pin> {
	std::size_t operator()(const electarus::Pin &p) const noexcept {
		std::size_t h1 = std::hash<std::string>{}(p.bgaRow);
		std::size_t h2 = p.number;
		return h1 ^ (h2 << 1); // or use boost::hash_combine
	}
};

namespace electarus { namespace db {

/** Support structs/classes */
struct DatabaseTotals {
	int datasheets;
	int devices;
	int orderables;
};

/* Simple data objects, no need for encapsulation in a privately used tool ... */
struct Datasheet {
	string id;
	string rev;
	string issueDate;
	string revDate;
};

// before Orderable because of the link, cannot forward-declare?
struct Pinset {
	int id = 0; // auto-increment
	int parentId = 0;
	int pins = 0;
	int cset; // groupIdx column
	unordered_map<Pin, struct Signalset> signalsets;
};

struct Orderable {
	string name; // also id (never linked to... useless)
	string model;
	Package pkg;
	Pinset pinset; // reference to correct object, if pinset.id != 0
	bool operator==(const Orderable &o) const {
		return name == o.name;
	}
};

struct Signalset {
	int id = 0;	  // auto-increment
	int parentId = 0; // 0 == NULL
	int datasheetIdx = 0;
	// vector<string> signals;
	unordered_map<int, string> signals;
};

/* SQLite 3 init & database import/export */
sqlite3 *createDatabase();
// helper function to prepare a sqlite3 statement
void prepare(sqlite3 *db, sqlite3_stmt **stmt, const char *query);

/* Query DB */
DatabaseTotals countTotals(sqlite3 *db);
DatabaseTotals countSupported(sqlite3 *db);

Datasheet findDatasheet(sqlite3 *db, const string id);
vector<Orderable> findOrderablesByDatasheet(sqlite3 *db, const string datasheetId, const vector<Pinset> &);
vector<Package> findPackagesByDatasheet(sqlite3 *db, const string datasheetId);
vector<Pinset> findPinsetsByDatasheet(sqlite3 *db, const string &datasheetId, const vector<Signalset> &);
vector<Signalset> findSignalsetsByDatasheet(sqlite3 *db, const string datasheetId);
unordered_map<string, string> listAllModelsAndDatasheets(sqlite3 *db);
unordered_map<string, string> listAllSignalDescriptions(sqlite3 *db);

/* Modify DB, inserts return inserted rows */
int linkOrderableToItsPinset(sqlite3 *db, const Orderable &odbl);
int saveOrUpdatePinset(sqlite3 *db, Pinset &ps);	 // assumes signalsets are all in DB!
int saveOrUpdateSignalset(sqlite3 *db, Signalset &sets); // assumes signals are all in DB!
int saveSignals(sqlite3 *db, unordered_map<string, string> signals);

/* Beautification ... */
int removeGapsInPinsetIds(sqlite3 *db);
int removeGapsInSignalsetIds(sqlite3 *db);

}} // namespace electarus::db
#endif // ELECTARUS_DATABASE_HPP
