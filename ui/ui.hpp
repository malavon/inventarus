#ifndef ELECTARUS_UI_HPP
#define ELECTARUS_UI_HPP

#include <set>
#include <unordered_map>

#include "cccurses/form.hpp"
#include "cccurses/window.hpp"
#include "db/database.hpp"

namespace electarus { namespace ui {

using namespace cccurses;

class Configset {
  public:
	Configset() { }
	Configset(const vector<db::Orderable> &vod) : odbls(vod) { }
	// looks like an odd copy constructor, let's call it a base-it-on constructor: copy everything except given
	Configset(const Configset &cs, const vector<db::Orderable> &vod) : odbls(vod), pinsetIds(cs.pinsetIds) { }

	const vector<string> toModels() const {
		std::set<string> models;
		for ( const db::Orderable &o : odbls ) {
			models.insert(o.model);
		}
		return vector<string>(models.begin(), models.end());
	}

	const vector<Package> toPkgs() const {
		std::set<Package> pkgs;
		for ( const db::Orderable &o : odbls ) {
			pkgs.insert(o.pkg);
		}
		return vector<Package>(pkgs.begin(), pkgs.end());
	}

	vector<db::Orderable> &orderables() {
		return odbls;
	}

	const vector<db::Orderable> &orderablesView() const {
		return odbls;
	}

	void add(db::Orderable &o) {
		add(o, o.pinset.id);
	}

	void add(db::Orderable &o, int pinsetId) {
		odbls.push_back(o);
		pinsetIdFor(o.pkg, pinsetId);
	}

	bool contains(const db::Orderable &o) const {
		for ( const db::Orderable &op : odbls ) {
			if ( op == o ) {
				return true;
			}
		}
		return false;
	}

	bool contains(const vector<db::Orderable> &vo) const {
		bool hasAll = true;
		for ( vector<db::Orderable>::const_iterator reqr = vo.cbegin(); hasAll && reqr != vo.end(); reqr++ ) {
			hasAll = contains(*reqr);
		}
		return hasAll;
	}

	const bool empty() const {
		return odbls.empty();
	}

	void update(db::Orderable &o) {
		if ( o.pinset.id == 0 ) {
			o.pinset.id = pinsetIdFor(o.pkg);
		} else {
			pinsetIdFor(o.pkg, o.pinset.id);
		}
		for ( auto it = odbls.begin(); it != odbls.end(); it++ ) {
			if ( o.name == (*it).name ) {
				*it = o;
				return;
			}
		}
	}

	int pinsetIdFor(const Package &pkg) const {
		return pinsetIds.find(pkg) == pinsetIds.end() ? 0 : pinsetIds.at(pkg);
	}

	const unordered_map<Package, int> getPinsetIds() const {
		return pinsetIds;
	}

  private:
	void pinsetIdFor(const Package &pkg, int id) {
		if ( id != 0 ) {
			assert(pinsetIds.find(pkg) == pinsetIds.end() || pinsetIds[pkg] == id);
			pinsetIds[pkg] = id;
		}
	}

  private:
	vector<db::Orderable> odbls;
	// configset requires pinset ids to ensure correct re-creation of all database objects
	// cache pinset ids for each configset, no matter what orderables are linked and should be linked
	unordered_map<Package, int> pinsetIds;
};

struct PinView {
  private:
	PinView() { } // only allow creation by 'friend' class

  public:
	// this struct was conceived to prevent using a db:: scoped object in the ui (like db::Signalset)
	// and once parenting/configsets has been implemented it will be much more useful
	// hindsight(c) powered by rebase (R)
	struct ConfigView {
		int signalsetId = 0;
		// parentsetId = 0; // not sure if needed, don't use unless proven useful
		// indexed! 0 = (pin) default, does NOT contain empty strings
		unordered_map<int, string> signals;
	};
	vector<ConfigView> cviews; // empty by default, needs to be inserted!
	// one pin per package
	unordered_map<Package, Pin> pins;
	// experiment: getting/setting signals can happen through the [] operator to get to
	// the one and only configview (in the future, multiple signalsets will be required)
	// then again, will NOT break compilation once it is no longer a single one ...
	unordered_map<int, string> &operator[](int idx) {
		return cviews.at(idx).signals;
	}
	const unordered_map<int, string> &operator[](int idx) const {
		return cviews.at(idx).signals;
	}
	void clearSignals() {
		for ( ConfigView &cf : cviews ) {
			cf.signals.clear();
		}
	}
	int countSignals() const {
		int c = 0;
		for ( const ConfigView &cf : cviews ) {
			c += cf.signals.size();
		}
		return c;
	}
	bool hasPins() const {
		for ( const std::pair<Package, Pin> &pr : pins ) {
			if ( !pr.second.empty() ) {
				return true;
			}
		}
		return false;
	}
	bool hasPinsAndSignals() const {
		return hasPins() && countSignals() > 0;
	}

	friend class PinSetView;
};

class PinSetView {
	typedef vector<PinView>::iterator pv_iterator;
	typedef vector<PinView>::const_reverse_iterator pvcr_iterator;

  public:
	// wrapping iterator?
	template<typename dataType>
	class OrderingIterator {
	  public:
		// OrderingIterator traits
		using difference_type = int;
		using value_type = PinView;
		using pointer = const dataType *;
		using reference = const PinView &;
		using iterator_category = std::forward_iterator_tag;
		using wrapped_iterator = vector<int>::iterator;

		OrderingIterator(vector<value_type> &val, wrapped_iterator wrp) : original(val), wrapped(wrp) { }

		OrderingIterator(const OrderingIterator<dataType> &raw) = default;

		bool operator==(const OrderingIterator<dataType> &raw) const {
			return wrapped == raw.wrapped;
		}

		bool operator!=(const OrderingIterator<dataType> &raw) const {
			return wrapped != raw.wrapped;
		}

		OrderingIterator<dataType> &operator+=(const difference_type &movement) {
			wrapped += movement;
			return *this;
		}

		OrderingIterator<dataType> &operator-=(const difference_type &movement) {
			wrapped -= movement;
			return *this;
		}

		OrderingIterator<dataType> &operator++() {
			++wrapped;
			return *this;
		}

		OrderingIterator<dataType> &operator--() {
			--wrapped;
			return *this;
		}

		OrderingIterator<dataType> operator++(int) {
			auto temp(*this);
			++wrapped;
			return temp;
		}

		OrderingIterator<dataType> operator--(int) {
			auto temp(*this);
			--wrapped;
			return temp;
		}

		OrderingIterator<dataType> operator+(const difference_type &movement) {
			wrapped_iterator oldPtr = wrapped;
			wrapped += movement;
			auto temp(*this);
			wrapped = oldPtr;
			return temp;
		}

		OrderingIterator<dataType> operator-(const difference_type &movement) {
			wrapped_iterator oldPtr = wrapped;
			wrapped -= movement;
			auto temp(*this);
			wrapped = oldPtr;
			return temp;
		}

		dataType &operator*() {
			return original[*wrapped];
		}

		const dataType &operator*() const {
			return original[*wrapped];
		}

		wrapped_iterator getWrapped() {
			return wrapped;
		}

	  private:
		wrapped_iterator wrapped;
		vector<value_type> &original;
	};

	typedef OrderingIterator<PinView> iterator;
	typedef OrderingIterator<const PinView> const_iterator;

	PinSetView(const vector<Package> &p) : pkgs(p) { }

	// factory method to create a valid PinView
	PinView createNewPinView() const {
		PinView pv;
		for ( const Configset &cs : csets ) {
			pv.cviews.push_back(PinView::ConfigView());
		}
		return pv;
	}

	void add(Configset &cs) {
		csets.push_back(cs);
		for ( PinView &pv : pinViews ) {
			pv.cviews.push_back(PinView::ConfigView());
		}
	}

	// view manipulation encapsulation

	bool canInsert() const {
		return orderByPkgIdx == ORDER_BY_DATASHEET_IDX;
	}

	// encapsulated view manipulation
	void deleteView() {
		if ( selIdx < size() ) {
			erase(begin() + selIdx);
		}
	}

	void edit() {
		editIdx = selIdx;
	}

	void insertView() {
		if ( selIdx < size() && orderByPkgIdx == ORDER_BY_DATASHEET_IDX ) {
			insertView(begin() + selIdx, createNewPinView());
			editIdx = selIdx;
		}
	}

	bool isEdit(int idx) const {
		return idx == editIdx;
	}

	bool isSelection(int idx) const {
		return idx == selIdx;
	}

	void moveDown() {
		assert(editIdx == -1); // cannot move while editing
		// size() is 1 higher than max to allow selecting pin at the end
		selIdx = min(static_cast<int>(pinViews.size()), selIdx + 1);
	}

	void moveUp() {
		assert(editIdx == -1);
		selIdx = max(0, selIdx - 1);
	}

	void orderBy(const int idx) {
		assert(ordIdx.size() == pinViews.size());
		int os = ordIdx.size();
		int prevSel = selIdx >= os ? os : ordIdx[selIdx];
		if ( idx >= pkgs.size() ) {
			orderByPkgIdx = ORDER_BY_DATASHEET_IDX;
			// I have a feeling I can do this with something from std:: and it's not <ranges>
			for ( int i = 0; i < pinViews.size(); i++ ) {
				ordIdx[i] = i; // 1-1, thus no ordering
			}
		} else {
			const Package pkg = pkgs[idx];
			vector<std::pair<Pin, int>> vsort; // allows duplicate pins to be sorted!
			int backIdx = pinViews.size() - 1, pvIdx = pinViews.size() - 1;
			// create a list of known pins; if not present, start adding to order from the back
			// this keeps datasheetIdx intact for all other pins
			for ( pvcr_iterator it = pinViews.crbegin(); it < pinViews.crend(); it++, pvIdx-- ) {
				PinView pv = (*it);
				Pin p = pv.pins[pkg];
				if ( p.empty() ) {
					ordIdx[backIdx--] = pvIdx;
				} else {
					vsort.push_back({p, pvIdx});
				}
			}
			std::stable_sort(vsort.begin(), vsort.end());
			// then read ordered pins and add them; to ensure there are no errors, also reverse and using backIdx
			for ( auto it = vsort.crbegin(); it != vsort.crend(); it++ ) {
				ordIdx[backIdx--] = (*it).second; // second = index in the pinViews collection
			}
			orderByPkgIdx = idx;
			assert(backIdx == -1); // all pinviews have to be processed! but no negative accesses done either
		}
		// restore selection to the same item
		selIdx = os;
		for ( int i = 0; i < os && selIdx == os; i++ ) {
			selIdx = ordIdx[i] == prevSel ? i : selIdx;
		}
		assert(selIdx <= os && selIdx >= 0);
	}

	void orderByNext() {
		orderBy(orderByPkgIdx + 1);
	}

	Package orderedBy() const {
		// returns dummy when not ordered just in case; if need for checking this, add a simple function to do so!
		return orderByPkgIdx == ORDER_BY_DATASHEET_IDX ? Package{"UNORDERED", -1} : pkgs[orderByPkgIdx];
	}

	void stopEdit() {
		editIdx = -1;
	}

	const vector<PinView> rawView() const {
		return pinViews;
	}

	/* vector<PinView>-like operation */
	iterator begin() {
		assert(ordIdx.size() == pinViews.size());
		return iterator(pinViews, ordIdx.begin());
	}

	const_iterator cbegin() {
		assert(ordIdx.size() == pinViews.size());
		return const_iterator(pinViews, ordIdx.begin());
	}

	const_iterator cend() {
		assert(ordIdx.size() == pinViews.size());
		return const_iterator(pinViews, ordIdx.end());
	}

	iterator end() {
		assert(ordIdx.size() == pinViews.size());
		return iterator(pinViews, ordIdx.end());
	}

	iterator erase(iterator it) {
		vector<int>::iterator wrp = it.getWrapped();
		int idx = ordIdx[*wrp];
		pinViews.erase(pinViews.begin() + idx);
		// ordIdx cannot be erased as-is, indices are changed ...
		// TODO: trigger 'reorder' instead?
		for ( int &i : ordIdx ) {
			if ( i >= idx ) {
				i--;
			}
		}
		return iterator(pinViews, ordIdx.erase(wrp));
	}

	iterator insertView(iterator it, const PinView &pv) {
		// insert is undefined when ordering! asserted but view code should not allow this!
		assert(orderByPkgIdx == ORDER_BY_DATASHEET_IDX);
		vector<int>::iterator wrp = it.getWrapped();
		int idx = ordIdx[*wrp];
		pv_iterator pvIt = pinViews.insert(pinViews.begin() + idx, pv);
		for ( int &i : ordIdx ) {
			if ( i >= idx ) {
				i++;
			}
		}
		wrp = ordIdx.insert(wrp, idx);
		return iterator(pinViews, wrp);
	}

	void push_back(PinView &pv) {
		int psize = pv.cviews.size(), csize = csets.size();
		// "upgrade" if need be
		for ( int i = csize - psize; i >= 0; i-- ) {
			pv.cviews.push_back(PinView::ConfigView());
		}
		// always adds to end of UNORDERED collection
		ordIdx.push_back(pinViews.size());
		pinViews.push_back(pv);
		// then reorder?
	}

	// bulk import of empty elements
	void ensureSize(int requiredSize) {
		for ( int i = pinViews.size(); i <= requiredSize; i++ ) {
			PinView pv = createNewPinView();
			push_back(pv);
		}
	}

	size_t size() {
		assert(ordIdx.size() == pinViews.size());
		return pinViews.size();
	}

	PinView &operator[](int idx) {
		assert(idx < ordIdx.size());
		return pinViews.at(ordIdx[idx]);
	}

	const PinView &operator[](int idx) const {
		assert(idx < ordIdx.size());
		return pinViews.at(ordIdx[idx]);
	}

  public:
	// fixed list of packages
	const vector<Package> pkgs;
	// map of all existing signals & descriptions, can be modified (well, extended at least)!
	unordered_map<string, string> signalDescs;
	vector<Configset> csets;

  private:
	// each item on the screen
	vector<PinView> pinViews;
	// ordering mapping: pinview cannot be re-ordered directly (for datasheet idx), but is mapped
	vector<int> ordIdx;
	int orderByPkgIdx = ORDER_BY_DATASHEET_IDX;
	/*
	 * index of pin that is edited
	 * if higher than pins.size(), add at end
	 * if -1, either same or indicate that all pins are view only
	 */
	int editIdx = -1;
	/* selection index, for browsing; 0-based */
	int selIdx = 0;

  public:
	static const int ORDER_BY_DATASHEET_IDX = -1; // default ordering as created datasheet_idx; otherwise order by package
};

// partial drawing functions
// TODO: window should scroll working
void drawPinSetHeader(Window &, const int row, const vector<string> &pkgs);
void drawPinSet(Window &, int &row, const vector<string> &pkgs, unordered_map<string, string> &signals, const PinView &pv);
void editPinSet(Window &, int &row, const vector<string> &pkgs, unordered_map<string, string> &signals, PinView &pv);

// Window drawing functions
void drawPinSetEditingWindow(Window &win, PinSetView &pinView);
void drawSetConfigWindow(BorderedWindow &, const vector<Configset> &cfs);
void drawTopWindow(BorderedWindow &, const db::Datasheet &, const db::DatabaseTotals &ttl, const db::DatabaseTotals &sprtd);
Configset filterForConfigset(const vector<db::Orderable> &, const Configset &base = Configset{});
void loopPinsetEditing(Window &pinset, Window &hotkeys, BorderedWindow &config, ui::PinSetView &);
void reorderPackages(vector<Package> &pkgs); // given vector is reordered in-place
string searchDatasheet(const unordered_map<string, string> &dsModels, const int modelFieldWidth);

}} // namespace electarus::ui
#endif // ELECTARUS_UI_HPP
