/*
  SugaR, a UCI chess playing engine derived from Stockfish
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2018 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad

  SugaR is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  SugaR is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <algorithm>
#include <cassert>
#include <ostream>
//Hash		
#include <iostream>
//end_Hash
#include <thread>

#include "misc.h"
#include "search.h"
#include "thread.h"
#include "tt.h"
#include "uci.h"
#include "syzygy/tbprobe.h"

////#include <iostream>

using std::string;

Value PawnValueMg = _PawnValueMg;
Value PawnValueEg = _PawnValueEg;
Value KnightValueMg = _KnightValueMg;
Value KnightValueEg = _KnightValueEg;
Value BishopValueMg = _BishopValueMg;
Value BishopValueEg = _BishopValueEg;
Value RookValueMg = _RookValueMg;
Value RookValueEg = _RookValueEg;
Value QueenValueMg = _QueenValueMg;
Value QueenValueEg = _QueenValueEg;

UCI::OptionsMap Options; // Global object

namespace UCI {

/// 'On change' actions, triggered by an option's value change
void on_clear_hash(const Option&) { Search::clear(); }
void on_hash_size(const Option& o) { TT.resize(o); }
void on_large_pages(const Option& o) { TT.resize(o); }  // warning is ok, will be removed
void on_logger(const Option& o) { start_logger(o); }
void on_threads(const Option& o) { Threads.set(o); }
void on_tb_path(const Option& o) { Tablebases::init(o); }
//Hash	
void on_HashFile(const Option& o) { TT.set_hash_file_name(o); }
void SaveHashtoFile(const Option&) { TT.save(); }
void LoadHashfromFile(const Option&) { TT.load(); }
void LoadEpdToHash(const Option&) { TT.load_epd_to_hash(); }
//end_Hash


inline Value rescale(int base, int incr, int scale) {
  return Value(( 2 * base * (scale + incr) / scale + 1 ) / 2);
}

void on_value(const Option& ) {
  int scaleMgValues = (int) Options["ScalePiecesMgValues"];
  int scaleEgValues = (int) Options["ScalePiecesEgValues"];
  ////std::cout << "scaleMgValues = " << scaleMgValues << std::endl;
  ////std::cout << "scaleEgValues = " << scaleEgValues << std::endl;
 
  PawnValueMg   = rescale( _PawnValueMg,   1*scaleMgValues, 10000 );
  KnightValueMg = rescale( _KnightValueMg, 2*scaleMgValues, 10000 );
  BishopValueMg = rescale( _BishopValueMg, 0*scaleMgValues, 10000 );
  RookValueMg   = rescale( _RookValueMg,   2*scaleMgValues, 10000 );
  QueenValueMg  = rescale( _QueenValueMg,  2*scaleMgValues, 10000 );
  PawnValueEg   = rescale( _PawnValueEg,   0*scaleEgValues, 10000 );
  KnightValueEg = rescale( _KnightValueEg, 1*scaleEgValues, 10000 );
  BishopValueEg = rescale( _BishopValueEg, 2*scaleEgValues, 10000 );
  RookValueEg   = rescale( _RookValueEg,   2*scaleEgValues, 10000 );
  QueenValueEg  = rescale( _QueenValueEg,  1*scaleEgValues, 10000 );
  ////std::cout << "PawnValueMg   = " << PawnValueMg   << std::endl;
  ////std::cout << "KnightValueMg = " << KnightValueMg << std::endl;
  ////std::cout << "BishopValueMg = " << BishopValueMg << std::endl;
  ////std::cout << "RookValueMg   = " << RookValueMg   << std::endl;
  ////std::cout << "QueenValueMg  = " << QueenValueMg  << std::endl;
  ////std::cout << "PawnValueEg   = " << PawnValueEg   << std::endl;
  ////std::cout << "KnightValueEg = " << KnightValueEg << std::endl;
  ////std::cout << "BishopValueEg = " << BishopValueEg << std::endl;
  ////std::cout << "RookValueEg   = " << RookValueEg   << std::endl;
  ////std::cout << "QueenValueEg  = " << QueenValueEg  << std::endl;
  
  PieceValue[MG][PAWN]   = PawnValueMg;
  PieceValue[MG][KNIGHT] = KnightValueMg;
  PieceValue[MG][BISHOP] = BishopValueMg;
  PieceValue[MG][ROOK]   = RookValueMg;
  PieceValue[MG][QUEEN]  = QueenValueMg;
  PieceValue[EG][PAWN]   = PawnValueEg;
  PieceValue[EG][KNIGHT] = KnightValueEg;
  PieceValue[EG][BISHOP] = BishopValueEg;
  PieceValue[EG][ROOK]   = RookValueEg;
  PieceValue[EG][QUEEN]  = QueenValueEg;
}

/// Our case insensitive less() function as required by UCI protocol
bool CaseInsensitiveLess::operator() (const string& s1, const string& s2) const {

  return std::lexicographical_compare(s1.begin(), s1.end(), s2.begin(), s2.end(),
         [](char c1, char c2) { return tolower(c1) < tolower(c2); });
}


/// init() initializes the UCI options to their hard-coded default values

void init(OptionsMap& o) {

  // at most 2^32 clusters.
  constexpr int MaxHashMB = Is64Bit ? 131072 : 2048;

  unsigned n = std::thread::hardware_concurrency();
  if (!n) n = 1;
  
  o["Debug Log File"]        << Option("", on_logger);
  o["Book File"]             << Option("book.bin");
  o["Best Book Move"]        << Option(false);
  o["Contempt"]              << Option(24, -100, 100);
  o["Analysis_CT"]           << Option("Both var Off var White var Black var Both", "Both");
  o["Threads"]               << Option(n, unsigned(1), unsigned(512), on_threads);
  o["Hash"]                  << Option(16, 1, MaxHashMB, on_hash_size);
  o["OwnBook"]               << Option(false);
  o["Clear_Hash"]            << Option(on_clear_hash);
  o["Ponder"]                << Option(false);
  o["MultiPV"]               << Option(1, 1, 500);
  o["Skill Level"]           << Option(20, 0, 20);
  o["Move Overhead"]         << Option(100, 0, 5000);
  o["Minimum Thinking Time"] << Option(20, 0, 5000);
  o["Slow Mover"]            << Option(84, 10, 1000);
  o["nodestime"]             << Option(0, 0, 10000);
  o["UCI_Chess960"]          << Option(false);
  o["NeverClearHash"]        << Option(false);
  o["HashFile"]              << Option("hash.hsh", on_HashFile);
  o["SaveHashtoFile"]        << Option(SaveHashtoFile);
  o["LoadHashfromFile"]      << Option(LoadHashfromFile);
  o["LoadEpdToHash"]         << Option(LoadEpdToHash);
  o["UCI_AnalyseMode"]       << Option(false);
  o["Large Pages"]           << Option(true, on_large_pages);
  o["ICCF Analyzes"]         << Option(0, 0,  8);
  o["Clear Search"]          << Option(false);
  o["NullMove"]              << Option(true);
  o["SyzygyPath"]            << Option("<empty>", on_tb_path);
  o["SyzygyProbeDepth"]      << Option(1, 1, 100);
  o["Syzygy50MoveRule"]      << Option(true);
  o["SyzygyProbeLimit"]      << Option(7, 0, 7);
  o["Move Base Importance"]  << Option(5, 0, 2000);
  
  o["ScalePiecesMgValues"]   << Option(5, -3000, 10000, on_value);
  o["ScalePiecesEgValues"]   << Option(10, -3000, 10000, on_value);

  on_value(Option());
}


/// operator<<() is used to print all the options default values in chronological
/// insertion order (the idx field) and in the format defined by the UCI protocol.

std::ostream& operator<<(std::ostream& os, const OptionsMap& om) {

  for (size_t idx = 0; idx < om.size(); ++idx)
      for (const auto& it : om)
          if (it.second.idx == idx)
          {
              const Option& o = it.second;
              os << "\noption name " << it.first << " type " << o.type;

              if (o.type != "button")
                  os << " default " << o.defaultValue;

              if (o.type == "spin")
                  os << " min " << o.min << " max " << o.max;

              break;
          }

  return os;
}


/// Option class constructors and conversion operators

Option::Option(const char* v, OnChange f) : type("string"), min(0), max(0), on_change(f)
{ defaultValue = currentValue = v; }

Option::Option(bool v, OnChange f) : type("check"), min(0), max(0), on_change(f)
{ defaultValue = currentValue = (v ? "true" : "false"); }

Option::Option(OnChange f) : type("button"), min(0), max(0), on_change(f)
{}

Option::Option(int v, int minv, int maxv, OnChange f) : type("spin"), min(minv), max(maxv), on_change(f)
{ defaultValue = currentValue = std::to_string(v); }

Option::Option(const char* v, const char* cur, OnChange f) : type("combo"), min(0), max(0), on_change(f)
{ defaultValue = v; currentValue = cur; }

Option::operator int() const {
  assert(type == "check" || type == "spin");
  return (type == "spin" ? stoi(currentValue) : currentValue == "true");
}

Option::operator std::string() const {
  assert(type == "string");
  return currentValue;
}

bool Option::operator==(const char* s) {
  assert(type == "combo");
  return    !CaseInsensitiveLess()(currentValue, s)
         && !CaseInsensitiveLess()(s, currentValue);
}


/// operator<<() inits options and assigns idx in the correct printing order

void Option::operator<<(const Option& o) {

  static size_t insert_order = 0;

  *this = o;
  idx = insert_order++;
}


/// operator=() updates currentValue and triggers on_change() action. It's up to
/// the GUI to check for option's limits, but we could receive the new value from
/// the user by console window, so let's check the bounds anyway.

Option& Option::operator=(const string& v) {

  assert(!type.empty());

  if (   (type != "button" && v.empty())
      || (type == "check" && v != "true" && v != "false")
      || (type == "spin" && (stoi(v) < min || stoi(v) > max)))
      return *this;

  if (type != "button")
      currentValue = v;

  if (on_change)
      on_change(*this);

  return *this;
}

} // namespace UCI
