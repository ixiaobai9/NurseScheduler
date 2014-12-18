//
//  Nurse.h
//  RosterDesNurses
//

#ifndef __Nurse__
#define __Nurse__

#include <iostream>
#include <string>
#include <vector>
#include <map>

#include "MyTools.h"

using std::vector;



//-----------------------------------------------------------------------------
//
//  C l a s s   N u r s e
//
//  Class that contains all the attributes describing the characteristics and
//  the planning of each nurse
//
//-----------------------------------------------------------------------------

      class Nurse {

public:

// Constructor and destructor
//
      Nurse(char* name, int nbSkills, std::vector<int> skills,
            int minTotalShifts, int maxTotalShifts,
            int minConsDaysWork, int maxConsDaysWork,
            int minConsDaysOff, int maxConsDaysOff,
            int maxTotalWeekEnds, bool isCompleteWeekEnds) :
              name_(name), nbSkills_(nbSkills), skills_(skills),
              minTotalShifts_(minTotalShifts), maxTotalShifts_(maxTotalShifts),
              minConsDaysWork_(minConsDaysWork), maxConsDaysWork_(maxConsDaysWork),
              minConsDaysOff_(minConsDaysOff), maxConsDaysOff_(maxConsDaysOff),
              maxTotalWeekEnds_(maxTotalWeekEnds), isCompleteWeekEnds_(isCompleteWeekEnds) {
      }
      ~Nurse();

private:

// name of the nurse
//
      std::string name_;

//-----------------------------------------------------------------------------
// Constant characteristics of the nurses (no set method)
//-----------------------------------------------------------------------------

// number of skills and vector of the skills indices
//
      const int nbSkills_;
      const vector<int> skills_;

// soft constraints of the nurse: min and max numbers of total assignments,
// min and max consecutive working days, min and max consectuve days off,
// maximum number of working week-ends and presence of absence of the
// complete week end constraints
//
      const int minTotalShifts_, maxTotalShifts_;
      const int minConsDaysWork_, maxConsDaysWork_;
      const int minConsDaysOff_, maxConsDaysOff_;
      const int maxTotalWeekEnds_;
      const int isCompleteWeekEnds_;


public:
      // getters of all the cosntant attributes
      //
      int minTotalShifts() {
              return minTotalShifts_;
      };
      int maxTotalShifts() {
              return maxTotalShifts_;
      };
      int minConsDaysWork() {
              return minConsDaysWork_;
      };
      int maxConsDaysWork() {
              return maxConsDaysWork_;
      };
      int minConsDaysOff() {
              return minConsDaysOff_;
      };
      int maxConsDaysOff() {
              return maxConsDaysOff_;
      };
      int maxTotalWeekEnds() {
              return maxTotalWeekEnds_;
      }
      int isCompleteWeekEnds() {
              return isCompleteWeekEnds_;
      }


      };


#endif /* defined(__ATCSolver__CftSolver__) */
