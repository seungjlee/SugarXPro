#ifndef BOOK_H_INCLUDED
#define BOOK_H_INCLUDED

#include <fstream>
#include <string>

#include "misc.h"
#include "position.h"

class PolyglotBook : private std::ifstream {
public:
  PolyglotBook();
 ~PolyglotBook();
  Move probe(const Position& pos, const std::string& fName, bool pickBest);

private:
  template<typename T> PolyglotBook& operator>>(T& n);

  bool open(const char* fName);
  size_t find_first(Key key);

  PRNG rng;
  std::string fileName;
};

#endif // #ifndef BOOK_H_INCLUDED
