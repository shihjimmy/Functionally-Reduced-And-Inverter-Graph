/****************************************************************************
  FileName     [ util.h ]
  PackageName  [ util ]
  Synopsis     [ Define the prototypes of the exported utility functions ]
  Author       [ Chung-Yang (Ric) Huang ]
  Copyright    [ Copyleft(c) 2007-present LaDs(III), GIEE, NTU, Taiwan ]
****************************************************************************/
#ifndef UTIL_H
#define UTIL_H

#include <istream>
#include <vector>
#include "rnGen.h"
#include "myUsage.h"



// In myString.cpp
extern int myStrNCmp(const std::string& s1, const std::string& s2, unsigned n);
extern size_t myStrGetTok(const std::string& str, std::string& tok, size_t pos = 0,
                          const char del = ' ');
extern bool myStr2Int(const std::string& str, int& num);
extern bool isValidVarName(const std::string& str);

// In myGetChar.cpp
extern char myGetChar(std::istream&);
extern char myGetChar();

// In util.cpp
extern int listDir(std::vector<std::string>&, const std::string&, const std::string&);
extern size_t getHashSize(size_t s);

// Other utility template functions
template<class T>
void clearList(T& l)
{
   T tmp;
   l.swap(tmp);
}

template<class T, class D>
void removeData(T& l, const D& d)
{
   size_t des = 0;
   for (size_t i = 0, n = l.size(); i < n; ++i) {
      if (l[i] != d) { // l[i] will be kept, so des should ++
         if (i != des) l[des] = l[i];
         ++des;
      }
      // else l[i] == d; to be removed, so des won't ++
   }
   l.resize(des);
}

#endif // UTIL_H
