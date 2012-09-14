#ifndef latexPrintouts_HH
#define latexPrintouts_HH

#if !defined(__CINT__) || defined(__MAKECINT__)
#include <TROOT.h>                  // access to gROOT, entry point to ROOT system

#include <vector>                   // STL vector class

#include "../Include/DYTools.hh"        // helper class for organizing input ntuple files
     
#endif

void latexPrintoutAcceptance2D(TMatrixD accv, TMatrixD accErrv);


#endif