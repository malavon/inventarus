#ifndef ELECTARUS_UTIL_HPP
#define ELECTARUS_UTIL_HPP

namespace electarus { namespace util {

// utility class that maps a vector on id values for easy access and simpler algorithms
// returns references from the vector; this is NOT a collection in itself!!
template<typename _Key, typename _Val>
class VectorIdMapAdapter {
  public:
	VectorIdMapAdapter(const vector<_Val> &vv) : store(vv) {
		int idx = 0;
		for ( const _Val &val : vv ) {
			id2idx[val.id] = idx++;
		}
	}

	bool contains(const _Key &key) {
		return id2idx.find(key) != id2idx.end();
	}

	_Val &operator[](const _Key &key) {
		assert(id2idx.find(key) != id2idx.end());
		return store.at(id2idx[key]);
	}

	const _Val &operator[](const _Key &key) const {
		assert(id2idx.find(key) != id2idx.end());
		return store.at(id2idx[key]);
	}

  private:
	vector<_Val> store;
	unordered_map<_Key, int> id2idx;
};

}} // namespace electarus::util
#endif // ELECTARUS_UTIL_HPP
