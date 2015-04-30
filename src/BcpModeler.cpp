/*
 * MyBcpModeler.cpp
 *
 *  Created on: Mar 17, 2015
 *      Author: legraina
 */

#include "BcpModeler.h"
#include "OsiClpSolverInterface.hpp"
#include "CoinTime.hpp"
#include "BCP_lp.hpp"
#include "CbcModeler.h"

/*
 * BCP_lp_user methods
 */

BcpLpModel::BcpLpModel(BcpModeler* pModel):
pModel_(pModel),nbCurrentColumnVarsBeforePricing_(pModel->getNbColumns()),
lpIteration_(0), lastDiveIteration_(-1), cbcEveryXLpIteration_(100),
alreadyDuplicated_(false), dive_(false)
{ }

//Initialize the lp parameters and the OsiSolver
OsiSolverInterface* BcpLpModel::initialize_solver_interface(){
   for(pair<BCP_lp_par::chr_params, bool> entry: pModel_->getLpParameters())
      set_param(entry.first, entry.second);
   OsiClpSolverInterface * clp = new OsiClpSolverInterface();
   int verbosity = max(0, pModel_->getVerbosity()-1);
   clp->messageHandler()->setLogLevel(verbosity);
   return clp;
}

//Try to generate a heuristic solution (or return one generated during cut/variable generation.
//Return a pointer to the generated solution or return a NULL pointer.
BCP_solution* BcpLpModel::generate_heuristic_solution(const BCP_lp_result& lpres,
   const BCP_vec<BCP_var*>& vars,
   const BCP_vec<BCP_cut*>& cuts){

   //if doesn't dive and on the good iteration
   if(!dive_ && ( (lpIteration_-lastDiveIteration_)%cbcEveryXLpIteration_) == 1){
      //copy the solver of the problem
      OsiSolverInterface* solver = getLpProblemPointer()->lp_solver->clone();

      for(int i=0; i<vars.size(); ++i){
         //Set the type of the columns as the columns can be integrated as continuous var
         //As some columns can have been removed, the index are not the same than ours
         BcpColumn* column = dynamic_cast<BcpColumn*>(vars[i]);
         if(column){
            switch (column->getVarType()) {
            case VARTYPE_BINARY:
               solver->setInteger(i);
               solver->setColUpper(i, 1);
               break;
            case VARTYPE_INTEGER:
               solver->setInteger(i);
               break;
            default:
               break;
            }
         }
         //Reset the bounds of all the core variables
         //As they are always in the problem and they are always the first variables,
         //they have the same index than our core variables
         else{
            if(!dynamic_cast<BCP_var_core*>(vars[i]))
               Tools::throwError("Not a core variable.");
            CoinVar* myVar = pModel_->getCoreVars()[i];
            solver->setColLower(i, myVar->getLB());
            solver->setColUpper(i, myVar->getUB());
         }
      }

      //initialize the MIP model
      CbcModeler MIP(solver);
      MIP.setVerbosity(0);
      MIP.solve();

      BCP_solution_generic* sol = new BCP_solution_generic(false);
     const int nbCoreVars = pModel_->getCoreVars().size();
      //same index for BCP_core_var and BcpCoreVar (they are all before the columns)
      for(int i=0; i<nbCoreVars; ++i){
         BcpCoreVar* var = (BcpCoreVar*) pModel_->getCoreVars()[i];
         double value = MIP.getVarValue(var);
         if(value>0)
            sol->add_entry(vars[i], value);
      }
      for(int i=nbCoreVars; i<vars.size(); ++i){
         //take the colum with the index i
         BcpColumn* column = (BcpColumn*)(pModel_->getColumns()[i-nbCoreVars]);
         double value = MIP.getVarValue(column);
         if(value>0)
            sol->add_entry(vars[i], value);
      }
      cout << "CBC: " << sol->objective_value() << endl;
      return sol;
   }
   return NULL;
}

//Modify parameters of the LP solver before optimization.
//This method provides an opportunity for the user to change parameters of the LP solver before optimization in the LP solver starts.
//The second argument indicates whether the optimization is a "regular" optimization or it will take place in strong branching.
//Default: empty method.
void BcpLpModel::modify_lp_parameters ( OsiSolverInterface* lp, const int changeType, bool in_strong_branching){
}

//This method provides an opportunity for the user to tighten the bounds of variables.
//The method is invoked after reduced cost fixing. The results are returned in the last two parameters.
//Parameters:
//lpres    the result of the most recent LP optimization,
//vars  the variables in the current formulation,
//status   the stati of the variables as known to the system,
//var_bound_changes_since_logical_fixing    the number of variables whose bounds have changed (by reduced cost fixing) since the most recent invocation of this method that has actually forced changes returned something in the last two arguments,
//changed_pos    the positions of the variables whose bounds should be changed
//new_bd   the new bounds (lb/ub pairs) of these variables.
void BcpLpModel::logical_fixing (const BCP_lp_result& lpres,
   const BCP_vec<BCP_var*>& vars,
   const BCP_vec<BCP_cut*>& cuts,
   const BCP_vec<BCP_obj_status>& var_status,
   const BCP_vec<BCP_obj_status>& cut_status,
   const int var_bound_changes_since_logical_fixing,
   BCP_vec<int>& changed_pos,
   BCP_vec<double>& new_bd){

   //If the node has already been duplicated and that we are diving
   if(var_bound_changes_since_logical_fixing > 0 && alreadyDuplicated_ && dive_){
      vector<MyObject*> fixingCandidates;

      //add all possibilities
      for(BCP_var* var: vars){
         BcpColumn* col = dynamic_cast<BcpColumn*>(var);
         if(col)
            fixingCandidates.push_back(col);
      }

      //remove all bad candidates
      pModel_->logical_fixing(fixingCandidates);

      //if there some candidates
      if(fixingCandidates.size() > 0){
         changed_pos.reserve(fixingCandidates.size());
         new_bd.reserve(2 * fixingCandidates.size());

         //fix if some candidates
         vector<MyObject*>::iterator it = fixingCandidates.begin();
         BcpColumn* col = (BcpColumn*) *it;
         for(int i=pModel_->getCoreVars().size(); i<vars.size(); ++i){
            BcpColumn* var = dynamic_cast<BcpColumn*>(vars[i]);

            //search the column var in fixingCandidates.
            //If find, set the lower bound of the column to 1
            if(col->getIndex() == var->getIndex()){
               changed_pos.push_back(i);
               new_bd.push_back(1);
               new_bd.push_back(var->ub());

               //take the next candidate
               ++it;
               //if the last one, break;
               if(it == fixingCandidates.end())
                  break;
               col = (BcpColumn*) *it;
            }
         }//end loop on fixingCandidates

      }
   }
}

// Restoring feasibility.
//This method is invoked before fathoming a search tree node that has been found infeasible and
//the variable pricing did not generate any new variables.
void BcpLpModel::restore_feasibility(const BCP_lp_result& lpres,
   const std::vector<double*> dual_rays,
   const BCP_vec<BCP_var*>& vars,
   const BCP_vec<BCP_cut*>& cuts,
   BCP_vec<BCP_var*>& vars_to_add,
   BCP_vec<BCP_col*>& cols_to_add){
   //dive is finished
   dive_ = false;
}

//Convert a set of variables into corresponding columns for the current LP relaxation.
void BcpLpModel::vars_to_cols(const BCP_vec<BCP_cut*>& cuts, // on what to expand
   BCP_vec<BCP_var*>& vars,       // what to expand
   BCP_vec<BCP_col*>& cols,       // the expanded cols
   // few things that the user can use for lifting vars if allowed
   const BCP_lp_result& lpres,
   BCP_object_origin origin, bool allow_multiple)
{
   TransformVarsToColumns(vars, cols);
}

//vars = are just the giver vars
//cols is the vector where the new columns will be stored
void BcpLpModel::TransformVarsToColumns(BCP_vec<BCP_var*>& vars, BCP_vec<BCP_col*>& cols){
   const int varnum = vars.size();
   if (varnum == 0)
      return;
   cols.reserve(varnum);

   for (int i = 0; i < varnum; ++i) {
      CoinVar* var = dynamic_cast<CoinVar*>(vars[i]);
      if(!var)
         Tools::throwError("Bad variable casting.");

      //Copy the vectors var->getIndexRows() and var->getCoeffRows() in arrays
      const int size = var->getNbRows();

      //create a new array which will be deleted by ~BCP_col()
      int* indexRows = new int[size];
      vector<int> index = var->getIndexRows();
      std::copy(index.begin(), index.end(), indexRows);

      //create a new array which will be deleted by ~BCP_col()
      double* coeffRows= new double[size];
      vector<double> coeffs = var->getCoeffRows();
      std::copy(coeffs.begin(), coeffs.end(), coeffRows);

      cols.unchecked_push_back(
         new BCP_col(size, indexRows, coeffRows, var->getCost(), var->getLB(), var->getUB()));
   }
}

//Generate variables within the LP process.
void BcpLpModel::generate_vars_in_lp(const BCP_lp_result& lpres,
   const BCP_vec<BCP_var*>& vars, const BCP_vec<BCP_cut*>& cuts, const bool before_fathom,
   BCP_vec<BCP_var*>& new_vars, BCP_vec<BCP_col*>& new_cols)
{
   ++lpIteration_;
   if(dive_) lastDiveIteration_ = lpIteration_;
   pModel_->setLPSol(lpres, vars);
   pModel_->pricing(0);

   //check if new columns add been added since the last time
   //if there are some, add all of them in new_vars
   int size = pModel_->getNbColumns();
   if ( size != nbCurrentColumnVarsBeforePricing_ ) { //|| ! before_fathom
      new_vars.reserve(size-nbCurrentColumnVarsBeforePricing_); //reserve the memory for the new columns
      for(int i=nbCurrentColumnVarsBeforePricing_; i<size; ++i){
         BcpColumn* var = dynamic_cast<BcpColumn*>(pModel_->getColumns()[i]);
         //create a new BcpColumn which will be deleted by BCP
         new_vars.unchecked_push_back(new BcpColumn(*var));
      }
      //      TransformVarsToColumns(new_vars, new_cols);
      nbCurrentColumnVarsBeforePricing_ = size;
      return;
   }

   // must be before fathoming. we need vars with red cost below the
   // negative of (lpobj-ub)/ks_num otherwise we can really fathom.
   //    const double rc_bound =
   //   (lpres.dualTolerance() + (lpres.objval() - upper_bound()))/kss.ks_num;
   //    generate_vars(lpres, vars, rc_bound, new_vars);
}

/*
 * BCP_DoNotBranch_Fathomed: The node should be fathomed without even trying to branch.
 * BCP_DoNotBranch: BCP should continue to work on this node.
 * BCP_DoBranch: branch on one of the candidates cands
 *
 */
BCP_branching_decision BcpLpModel::select_branching_candidates(const BCP_lp_result& lpres, //the result of the most recent LP optimization.
   const BCP_vec<BCP_var*> &  vars, //the variables in the current formulation.
   const BCP_vec< BCP_cut*> &  cuts, //the cuts in the current formulation.
   const BCP_lp_var_pool& local_var_pool, //the local pool that holds variables with negative reduced cost.
   //In case of continuing with the node the best so many variables will be added to the formulation (those with the most negative reduced cost).
   const BCP_lp_cut_pool& local_cut_pool, //the local pool that holds violated cuts.
   //In case of continuing with the node the best so many cuts will be added to the formulation (the most violated ones).
   BCP_vec<BCP_lp_branching_object*>&  cands, //the generated branching candidates.
   bool force_branch) //indicate whether to force branching regardless of the size of the local cut/var pools{
{
   pModel_->setLPSol(lpres, vars);

   //if some variables have been generated, do not branch
   if(local_var_pool.size() > 0)
      return BCP_DoNotBranch;

   //If no more columns have been generated, duplicate the node and:
   //let the function logical_fixing perform its work for the first one
   //keep the second for continuing the branching process later
   if(!alreadyDuplicated_){
      cands.push_back(new  BCP_lp_branching_object(2, //2 identical children with nothing changed
         0, 0, 0, 0, 0, 0, 0, 0, 0, 0));
      dive_ = true;
      return BCP_DoBranch;
   }

   //Stop the dive
   if(dive_){
      dive_ = false;
      return BCP_DoNotBranch_Fathomed;
   }

   //branching candidates
   vector<MyObject*> branchingCandidates;
   pModel_->branching_candidates(branchingCandidates);

   //branch if some candidates
   for(MyObject* var: branchingCandidates){
      CoinVar* var2 = dynamic_cast<CoinVar*>(var);
      appendCoreIntegerVar(var2, cands);
   }
   if(cands.size() > 0)
      return BCP_DoBranch;

   //otherwise fathomed
   return BCP_DoNotBranch_Fathomed;
}

//Decide what to do with the children of the selected branching object.
//Fill out the _child_action field in best. This will specify for every child what to do with it.
//Possible values for each individual child are BCP_PruneChild, BCP_ReturnChild and BCP_KeepChild.
//There can be at most child with this last action specified.
//It means that in case of diving this child will be processed by this LP process as the next search tree node.
//Default: Every action is BCP_ReturnChild.
//However, if BCP dives then one child will be mark with BCP_KeepChild. The decision which child to keep is based on the ChildPreference parameter in BCP_lp_par.
//Also, if a child has a presolved lower bound that is higher than the current upper bound then that child is mark as BCP_FathomChild.
void BcpLpModel::set_actions_for_children(BCP_presolved_lp_brobj* best){
   //if first time, duplicate the node
   if(!alreadyDuplicated_){
      best->action()[0] = BCP_KeepChild;
      best->action()[1] = BCP_ReturnChild;
      alreadyDuplicated_ = true;
   }
   //otherwise let perform the default actions
   else
      BCP_lp_user::set_actions_for_children(best);
}

void BcpLpModel::appendCoreIntegerVar(CoinVar* coreVar, BCP_vec<BCP_lp_branching_object*>&  cands){
   BCP_vec<int> vpos; //positions of the variables
   BCP_vec<double> vbd; // old bound and then new one for each variable

   vpos.push_back(coreVar->getIndex());
   double value = pModel_->getVarValue(coreVar);
   vbd.push_back(coreVar->getLB()); // old lower bound
   vbd.push_back(floor(value)); // new upper bound
   vbd.push_back(ceil(value)); // new lower bound
   vbd.push_back(coreVar->getUB()); // new lower bound


   cands.push_back(new  BCP_lp_branching_object(2, //2 children
      0, 0, /* vars/cuts_to_add */
      &vpos, 0, &vbd, 0, /* forced parts: position and bounds (old bound and then new one) */
      0, 0, 0, 0 /* implied parts */));
}

void BcpLpModel::appendNewBranchingVar(vector<MyObject*> columns, BCP_vec<BCP_lp_branching_object*>&  cands){
   const int nbChildren = columns.size();
   BCP_vec<int> vpos; //positions of the variables
   BCP_vec<double> vbd; // old bound and then new one for each variable and for each children
   //this vector is filled is this order:
   //for the first child: old and new bounds for all the variables in vpos
   //then for the second child: old and new bounds for all the variables in vpos
   //....

   int child = 0;
   for (MyObject* var: columns) {
      BcpColumn* col = dynamic_cast<BcpColumn*>(var);
      if(col)
         vpos.push_back(col->getIndex());
      else
         Tools::throwError("The variable fixed is not a column.");
      //set the new bound of col to 1 and all the others columns keep 0
      for(int i=0; i<nbChildren; ++i){
         //fix the old bound (0)
         vbd.push_back(0);
         //if i==child, the new bound==1, otherwise 0
         vbd.push_back( (i==child) ? 1: 0 );
      }
      ++child;
   }

   cands.push_back(new  BCP_lp_branching_object(nbChildren, 0, 0, /* vars/cuts_to_add */
      &vpos, 0, &vbd, 0, /* forced parts */
      0, 0, 0, 0 /* implied parts */));
}

/*
 * BcpBranchingTree
 */

BcpBranchingTree::BcpBranchingTree(BcpModeler* pModel):
      pModel_(pModel) , nbInitialColumnVars_(pModel->getNbColumns()), minGap_(0.05)
{ }

// setting the base
//Create the core of the problem by filling out the last three arguments.
void BcpBranchingTree::initialize_core(BCP_vec<BCP_var_core*>& vars,
   BCP_vec<BCP_cut_core*>& cuts, BCP_lp_relax*& matrix){
   // initialize tm parameters
   set_search_strategy();
   set_param(BCP_tm_par::TmVerb_SingleLineInfoFrequency, pModel_->getFrequency());
   for(pair<BCP_tm_par::chr_params, bool> entry: pModel_->getTmParameters())
      set_param(entry.first, entry.second);

   //define nb rows and col
   const int rownum = pModel_->getCons().size();
   const int colnum = pModel_->getCoreVars().size();

   // bounds and objective
   double lb[colnum], ub[colnum], obj[colnum], rhs[rownum], lhs[rownum];
   //copy of the core variables
   vars.reserve(colnum);
   for(int i=0; i<colnum; ++i){
      BcpCoreVar* var = dynamic_cast<BcpCoreVar*>(pModel_->getCoreVars()[i]);
      if(!var)
         Tools::throwError("Bad variable casting.");
      //create a new BcpCoreVar which will be deleted by BCP
      vars.push_back(new BcpCoreVar(*var));
      lb[i] = var->getLB();
      ub[i] = var->getUB();
      obj[i] = var->getCost();
   }

   //copy of the core cuts
   cuts.reserve(rownum);
   for(int i=0; i<rownum; ++i){
      BcpCoreCons* cut = dynamic_cast<BcpCoreCons*>(pModel_->getCons()[i]);
      if(!cut)
         Tools::throwError("Bad constraint casting.");
      //create a new BcpCoreCons which will be deleted by BCP
      cuts.push_back(new BcpCoreCons(*cut));
      lhs[i] = cut->getLhs();
      rhs[i] = cut->getRhs();
   }

   matrix = new BCP_lp_relax;
   matrix->copyOf(pModel_->buildCoinMatrix(true), obj, lb, ub, rhs, lhs);
}

// create the root node
//Create the set of extra variables and cuts that should be added
//to the formulation in the root node.
void BcpBranchingTree::create_root(BCP_vec<BCP_var*>& added_vars,
   BCP_vec<BCP_cut*>& added_cuts,
   BCP_user_data*& user_data){

   added_vars.reserve(nbInitialColumnVars_);
   for(int i=0; i<nbInitialColumnVars_; ++i){
      BcpColumn* var = dynamic_cast<BcpColumn*>(pModel_->getColumns()[i]);
      if(!var)
         Tools::throwError("Bad variable casting.");
      //create a new BcpColumn which will be deleted by BCP
      added_vars.unchecked_push_back(new BcpColumn(*var));
   }

}

void BcpBranchingTree::display_feasible_solution(const BCP_solution* sol){
   // store the solution
   if(pModel_->getBestUb() > sol->objective_value())
      pModel_->setBestUb(sol->objective_value());

   BCP_solution_generic* sol2 = (BCP_solution_generic*) sol;
   vector<double> primal(pModel_->getNbVars());
   for(int i=0; i<sol2->_vars.size(); ++i){
      int index = sol2->_vars[i]->bcpind();
      primal[index] = sol2->_values[i];
   }
   pModel_->setPrimal(primal);

   if(pModel_->getSearchStrategy() == DepthFirstSearch){
      double gap = ( sol->objective_value() - pModel_->getBestLb() ) / pModel_->getBestLb();
      if(gap > 0 && gap < minGap_){
         pModel_->setSearchStrategy(BestFirstSearch);
         set_search_strategy();
      }
   }
}

// various initializations before a new phase (e.g., pricing strategy)
void BcpBranchingTree::init_new_phase(int phase, BCP_column_generation& colgen, CoinSearchTreeBase*& candidates) {
   colgen = BCP_GenerateColumns;
}

/*
 * BcpModeler
 */

//solve the model
int BcpModeler::solve(bool relaxatione){
   BcpInitialize bcp(this);
   char* argv[0];
   return bcp_main(0, argv, &bcp);
}

/*
 * Create core variable:
 *    var is a pointer to the pointer of the variable
 *    var_name is the name of the variable
 *    lb, ub are the lower and upper bound of the variable
 *    vartype is the type of the variable: SCIP_VARTYPE_CONTINUOUS, SCIP_VARTYPE_INTEGER, SCIP_VARTYPE_BINARY
 */
int BcpModeler::createCoinVar(CoinVar** var, const char* var_name, int index, double objCoeff, VarType vartype, double lb, double ub){
   *var = new BcpCoreVar(var_name, index, objCoeff, vartype, lb, ub);
   objects_.push_back(*var);
   return 1;
}

int BcpModeler::createColumnCoinVar(CoinVar** var, const char* var_name, int index, double objCoeff, double dualObj, VarType vartype, double lb, double ub){
   *var = new BcpColumn(var_name, index, objCoeff, dualObj, vartype, lb, ub);
   objects_.push_back(*var);
   return 1;
}


/*
 * Create linear constraint:
 *    con is a pointer to the pointer of the constraint
 *    con_name is the name of the constraint
 *    lhs, rhs are the lower and upper bound of the constraint
 *    nonZeroVars is the number of non-zero coefficients to add to the constraint
 */

int BcpModeler::createCoinConsLinear(CoinCons** con, const char* con_name, int index, double lhs, double rhs){
   *con = new BcpCoreCons(con_name, index, lhs, rhs);
   objects_.push_back(*con);
   return 1;
}

/*
 * Set the solution
 */

void BcpModeler::setLPSol(const BCP_lp_result& lpres, const BCP_vec<BCP_var*>&  vars){
   obj_history_.push_back(lpres.objval());
   if(best_lb_in_root > lpres.objval())
      best_lb_in_root = lpres.objval();

   //clear the old vectors
   if(primalValues_.size() != 0){
      primalValues_.clear();
      dualValues_.clear();
      reducedCosts_.clear();
      lhsValues_.clear();
   }

   //copy the new arrays in the vectors for the core vars
   const int nbCoreVar = coreVars_.size();
   const int nbColVar = columnVars_.size();
   const int nbCons = cons_.size();

   primalValues_.assign(lpres.x(), lpres.x()+nbCoreVar);
   dualValues_.assign(lpres.pi(), lpres.pi()+nbCons);
   reducedCosts_.assign(lpres.dj(), lpres.dj()+nbCoreVar);
   lhsValues_.assign(lpres.lhs(), lpres.lhs()+nbCons);

   //reserve some space for the columns
   primalValues_.reserve(nbColVar);
   reducedCosts_.reserve(nbColVar);
   //loop through the variables and link the good columns together
   vector<CoinVar*>::iterator it = columnVars_.begin();
   for(int i=nbCoreVar; i<vars.size(); ++i){
      BcpColumn* col = (BcpColumn*) *it;
      BcpColumn* var = dynamic_cast<BcpColumn*>(vars[i]);
      if(! var) Tools::throwError("Column from BCP bad cast");
      while(col->getIndex() != var->getIndex()){
         primalValues_.push_back(0);
         reducedCosts_.push_back(0);
         ++it;
         col = (BcpColumn*) *it;
      }
      primalValues_.push_back(lpres.x()[i]);
      reducedCosts_.push_back(lpres.dj()[i]);
      ++it;
   }
   while(it != columnVars_.end()){
      primalValues_.push_back(0);
      reducedCosts_.push_back(0);
      ++it;
   }
}

/*
 * get the primal values
 */

double BcpModeler::getVarValue(MyObject* var){
   CoinVar* var2 = (CoinVar*) var;
   if(primalValues_.size() ==0 )
      Tools::throwError("Primal solution has been initialized.");
   return primalValues_[var2->getIndex()];
}

/*
 * Get the dual variables
 */

double BcpModeler::getDual(MyObject* cons, bool transformed){
   CoinCons* cons2 = (CoinCons*) cons;
   if(dualValues_.size() == 0)
      Tools::throwError("Dual solution has been initialized.");
   return dualValues_[cons2->getIndex()];
}

/**************
 * Parameters *
 *************/
int BcpModeler::setVerbosity(int v){
   verbosity_ = v;

   if(v>=1){
      TmVerb_SingleLineInfoFrequency = 10;

      tm_parameters[BCP_tm_par::VerbosityShutUp] = 1;
      tm_parameters[BCP_tm_par::TmVerb_First] = 1;
      tm_parameters[BCP_tm_par::TmVerb_BetterFeasibleSolutionValue] = 1;
      tm_parameters[BCP_tm_par::TmVerb_NewPhaseStart] = 1;
      tm_parameters[BCP_tm_par::TmVerb_Last] = 1;

      lp_parameters[BCP_lp_par::LpVerb_Last] = 1; // Just a marker for the last LpVerb
   }

   if(v>=2){
      TmVerb_SingleLineInfoFrequency = 5;

      lp_parameters[BCP_lp_par::LpVerb_IterationCount] = 1; // Print the "Starting iteration x" line. (BCP_lp_main_loop)
      lp_parameters[BCP_lp_par::LpVerb_GeneratedVarCount] = 1; // Print the number of variables generated during this iteration. (BCP_lp_main_loop)
      lp_parameters[BCP_lp_par::LpVerb_ReportVarGenTimeout] = 1; // Print information if receiving variables is timed out. (BCP_lp_generate_vars)
      lp_parameters[BCP_lp_par::LpVerb_ReportLocalVarPoolSize] = 1; // Similar as above for variables. (BCP_lp_generate_vars)
      lp_parameters[BCP_lp_par::LpVerb_AddedVarCount] = 1; // Print the number of variables added from the local variable pool in the current iteration. (BCP_lp_main_loop)
   }

   if(v>=3){
      tm_parameters[BCP_tm_par::TmVerb_AllFeasibleSolutionValue] = 1;
      tm_parameters[BCP_tm_par::TmVerb_PrunedNodeInfo] = 1;

      lp_parameters[BCP_lp_par::LpVerb_ChildrenInfo] = 1; // After a branching object is selected print what happens to the presolved children (e.g., fathomed). (BCP_print_brobj_stat)
      lp_parameters[BCP_lp_par::LpVerb_ColumnGenerationInfo] = 1; // Print the number of variables generated before resolving the Lp ir fathoming a node. (BCP_lp_fathom)
      lp_parameters[BCP_lp_par::LpVerb_FathomInfo] = 1; // Print information related to fathoming. (BCP_lp_main_loop, BCP_lp_perform_fathom, BCP_lp_branch) (BCP_lp_fathom)
      lp_parameters[BCP_lp_par::LpVerb_RelaxedSolution] = 1; // Turn on the user hook "display_lp_solution". (BCP_lp_main_loop)
      lp_parameters[BCP_lp_par::LpVerb_LpSolutionValue] = 1; // Print the size of the problem matrix and the LP solution value after resolving the LP. (BCP_lp_main_loop)
   }

   if(v>=4){
      TmVerb_SingleLineInfoFrequency = 1;

      tm_parameters[BCP_tm_par::TmVerb_BetterFeasibleSolution] = 1;
      tm_parameters[BCP_tm_par::TmVerb_AllFeasibleSolution] = 1;
      tm_parameters[BCP_tm_par::TmVerb_TimeOfImprovingSolution] = 1;
      tm_parameters[BCP_tm_par::TmVerb_TrimmedNum] = 1;
      tm_parameters[BCP_tm_par::ReportWhenDefaultIsExecuted] = 1;

      lp_parameters[BCP_lp_par::LpVerb_FinalRelaxedSolution] = 1; // Turn on the user hook "display_lp_solution" for the last LP relaxation solved at a search tree node. (BCP_lp_main_loop)
      lp_parameters[BCP_lp_par::ReportWhenDefaultIsExecuted] = 1; // Print out a message when the default version of an overridable method is executed. Default: 1.
      lp_parameters[BCP_lp_par::LpVerb_MatrixCompression] = 1; // Print the number of columns and rows that were deleted during matrix compression. (BCP_lp_delete_cols_and_rows)
      lp_parameters[BCP_lp_par::LpVerb_PresolvePositions] = 1; // Print detailed information about all the branching candidates during strong branching. LpVerb_PresolveResult must be set for this parameter to have an effect. (BCP_lp_perform_strong_branching)
      lp_parameters[BCP_lp_par::LpVerb_PresolveResult] = 1; // Print information on the presolved branching candidates during strong branching. (BCP_lp_perform_strong_branching)
      lp_parameters[BCP_lp_par::LpVerb_ProcessedNodeIndex] = 1; // Print the "Processing NODE x on LEVEL y" line. (BCP_lp-main_loop)
      lp_parameters[BCP_lp_par::LpVerb_VarTightening] = 1; // Print the number of variables whose bounds have been changed by reduced cost fixing or logical fixing. (BCP_lp_fix_vars)
      lp_parameters[BCP_lp_par::LpVerb_RowEffectivenessCount] = 1; // Print the number of ineffective rows in the current problem. The definition of what rows are considered ineffective is determined by the paramter IneffectiveConstraints. (BCP_lp_adjust_row_effectiveness)
      lp_parameters[BCP_lp_par::LpVerb_StrongBranchPositions] = 1; // Print detailed information on the branching candidate selected by strong branching. LpVerb_StrongBranchResult must be set fo this parameter to have an effect. (BCP_print_brobj_stat)
      lp_parameters[BCP_lp_par::LpVerb_StrongBranchResult] = 1; // Print information on the branching candidate selected by strong branching. (BCP_print_brobj_stat)
   }

   return 1;
}

/**************
 * Outputs *
 *************/

int BcpModeler::printStats(){
}

int BcpModeler::writeProblem(string fileName){
}

int BcpModeler::writeLP(string fileName){
   //   OsiClpSolverInterface solver = ;
   //   solver.writeLp(fileName.c_str(), "lp", 1e-5, 10, 5);
}
