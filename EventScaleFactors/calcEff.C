#if !defined(__CINT__) || defined(__MAKECINT__)
#include <TROOT.h>                  // access to gROOT, entry point to ROOT system
#include <TFile.h>                  // file handle class
#include <TTree.h>                  // class to access ntuples
#include <TChain.h>
#include <TCanvas.h>                // class for drawing
#include <TH1F.h>                   // 1D histograms
#include <TGraphErrors.h>           // graphs
#include <TProfile.h>               // profile histograms
#include <TBenchmark.h>             // class to track macro running statistics
#include <TLorentzVector.h>         // class for Lorentz vector computations
#include <TGraphAsymmErrors.h>
#include <TClonesArray.h>
#include <TMatrixD.h>
#include <iostream>                 // standard I/O
#include <iomanip>                  // functions to format standard I/O
#include <fstream>                  // functions for file I/O
#include <vector>                   // STL vector class
#include <string>                   // C++ string class
#include <sstream>                  // class for parsing strings

#include "RooRealVar.h"
#include "RooDataHist.h"
#include "RooBreitWigner.h"
#include "RooExponential.h"
#include "RooCBShape.h"
#include "RooGaussian.h"
#include "RooGenericPdf.h"
#include "RooHistPdf.h"
#include "RooAddPdf.h"
#include "RooExtendPdf.h"
#include "RooFFTConvPdf.h"
#include "RooCategory.h"
#include "RooSimultaneous.h"
#include "RooDataSet.h"
#include "RooFitResult.h"

#include "../Include/CPlot.hh"          // helper class for plots
#include "../Include/MitStyleRemix.hh"  // style settings for drawing

// define classes and constants to read in ntuple
#include "../Include/EWKAnaDefs.hh"
#include "../Include/TGenInfo.hh"
#include "../Include/TEventInfo.hh"
#include "../Include/TDielectron.hh"
#include "../Include/TElectron.hh"
#include "../Include/TPhoton.hh"
#include "../Include/TVertex.hh"
#include "../Include/DYTools.hh"
#include "../Include/DYToolsUI.hh"
#include "../Include/EleIDCuts.hh"
#include "../Include/TriggerSelection.hh"

#include "../Include/cutFunctions.hh"
#include "../Include/fitFunctions.hh"
#include "../Include/fitFunctionsCore.hh"
#include "../EventScaleFactors/tnpSelectEvents.hh"

#include "../Include/EventSelector.hh"



#endif

using namespace mithep;


//=== COMMON CONSTANTS ===========================================================================================

//const int performPUReweight=0;

//=== FUNCTION DECLARATIONS ======================================================================================

//=== MAIN MACRO =================================================================================================

void calcEff(const TString configFile, const TString effTypeString, const TString triggerSetString, int performPUReweight, int puDependence=0) 
{

  //  ---------------------------------
  //       Preliminary checks
  //  ---------------------------------

  // verify whether it was a compilation check
  if (configFile.Contains("_DebugRun_") || triggerSetString.Contains("_DebugRun_")) {
    std::cout << "calcEff: _DebugRun_ detected. Terminating the script\n";
    return;
  }

  if (!effTypeString.Contains("RECO") &&
      !effTypeString.Contains("ID") &&
      !effTypeString.Contains("HLT")) {
    std::cout << "calcEff: effTypeString should be \"RECO\",\"ID\" or \"HLT*\"\n";
    return;
  }

  // fast check
  // Construct the trigger object
  TriggerSelection triggers(triggerSetString, true, 0); // later calls actOnData
  assert ( triggers.isDefined() );

  //  ---------------------------------
  //         Normal execution
  //  ---------------------------------

  using namespace mithep; 
 
  gBenchmark->Start("calcEff");
  

  //--------------------------------------------------------------------------------------------------------------
  // Settings 
  //==============================================================================================================
  
  Double_t massLow  = 60;
  Double_t massHigh = 120;

  // Read in the configuratoin file
  TString sampleTypeString = "";
  TString calcMethodString = "";
  TString etBinningString  = "";
  TString etaBinningString = "";
  TString dirTag;
  vector<TString> ntupleFileNames;
  ifstream ifs;
  ifs.open(configFile.Data());
  if (!ifs.is_open()) {
    std::cout << "configFile=" << configFile << "\n";
    assert(ifs.is_open());
  }
  string line;
  Int_t state=0;
  Int_t subState=0;
  while(getline(ifs,line)) {
    if(line[0]=='#') continue;
    if(line[0]=='%') break;
    if(state==0){
      // Read 1st line of content: data or MC?
      sampleTypeString = TString(line);
      state++;
    }else if(state==1) {
      // Read 2d content line: efficiency fitting mode
      size_t pos=line.find(':');
      if (pos==string::npos) {
	std::cout << "expected format is EFFICIENCY:fitting_mode\n";
	return;
      }
      subState++;
      if (line.find(effTypeString)!=string::npos) {
	calcMethodString = TString(line.c_str()+pos+1);
      }
      if (subState==3) state++;
    }else if(state==2) {
      // Read 3rd content line: SC ET binning
      etBinningString = TString(line);
      state++;
    }else if(state==3) {
      // Read 4th content line: SC eta binning
      etaBinningString = TString(line);
      state++;
    }else if(state==4) {
      // Read 5th content line: directory tag
      dirTag = TString(line);
      state++;
    }else if(state==5) {
      ntupleFileNames.push_back(TString(line));
    }
  }
  ifs.close();

  if (state!=5) {
    std::cout << "failed to read input file\n";
    return;
  }
  
  int calcMethod = 0;
  if(calcMethodString == "COUNTnCOUNT")
    calcMethod = DYTools::COUNTnCOUNT;
  else if(calcMethodString == "COUNTnFIT")
    calcMethod = DYTools::COUNTnFIT;
  else if(calcMethodString == "FITnFIT")
    calcMethod = DYTools::FITnFIT;
  else
    assert(0);
  printf("Efficiency calculation method: %s\n", calcMethodString.Data());

  DYTools::TEfficiencyKind_t effType = DetermineEfficiencyKind(effTypeString);
  printf("Efficiency type to measure: %s\n", EfficiencyKindName(effType).Data());

  DYTools::TEtBinSet_t etBinning = DetermineEtBinSet(etBinningString);
  printf("SC ET binning: %s\n", EtBinSetName(etBinning).Data());

  DYTools::TEtaBinSet_t etaBinning = DetermineEtaBinSet(etaBinningString);
  printf("SC eta binning: %s\n", EtaBinSetName(etaBinning).Data());

  int sample;
  if(sampleTypeString == "DATA")
    sample = DYTools::DATA;
  else if(sampleTypeString == "MC")
    sample = DYTools::MC;
  else
    assert(0);
  printf("Sample: %s\n", sampleTypeString.Data());

  // Correct the trigger object
  triggers.actOnData((sample==DYTools::DATA)?true:false);

  // The label is a string that contains the fields that are passed to
  // the function below, to be used to name files with the output later.
  TString label = getLabel(sample, effType, calcMethod, etBinning, etaBinning, triggers);

  //--------------------------------------------------------------------------------------------------------------
  // Main analysis code 
  //==============================================================================================================
  
  //  
  // Set up histograms
  //
  //   TH1F* hMass           = new TH1F("hMass"     ,"",30,massLow,massHigh);
  TH1F* hMassTotal      = new TH1F("hMassTotal","",30,massLow,massHigh);
  TH1F* hMassPass       = new TH1F("hMassPass" ,"",30,massLow,massHigh);
  TH1F* hMassFail       = new TH1F("hMassFail" ,"",30,massLow,massHigh);

  

  // Save MC templates if sample is MC
  TString tagAndProbeDir(TString("../root_files/tag_and_probe/")+dirTag);
  //gSystem->mkdir(tagAndProbeDir,kTRUE);

  TFile *templatesFile = 0;
  vector<vector<TH1F*>*> hPassTemplateV;
  vector<vector<TH1F*>*> hFailTemplateV;
  TString labelMC = getLabel(-1111, effType, calcMethod, etBinning, etaBinning, triggers);
  TString puTag=(performPUReweight) ? "_PU" : "";
  if (puDependence) puTag.Append("_varPU");
  TString templatesLabel = tagAndProbeDir + TString("/mass_templates_")+labelMC + puTag + TString(".root");

  if( sample != DYTools::DATA) {
    // For simulation, we will be saving templates
    templatesFile = new TFile(templatesLabel,"recreate");
    for (int pu_i=0; pu_i<DYTools::nPVBinCount; ++pu_i) {
      vector<TH1F*> *hPassV=new vector<TH1F*>();
      hPassV->reserve(DYTools::getNEtBins(etBinning)*DYTools::getNEtaBins(etaBinning));
      hPassTemplateV.push_back(hPassV);
      vector<TH1F*> *hFailV=new vector<TH1F*>();
      hFailV->reserve(DYTools::getNEtBins(etBinning)*DYTools::getNEtaBins(etaBinning));
      hFailTemplateV.push_back(hFailV);
      if (!puDependence) pu_i=-2;
      for(int i=0; i<DYTools::getNEtBins(etBinning); i++){
	for(int j=0; j<DYTools::getNEtaBins(etaBinning); j++){
	  hPassV->push_back(new TH1F(getTemplateName(i,j,"pass",pu_i+1),"",60,massLow,massHigh));
	  hFailV->push_back(new TH1F(getTemplateName(i,j,"fail",pu_i+1),"",60,massLow,massHigh));
	}
      }
      if (!puDependence) break;
    }
  }
  else {
    // For data, we will be using templates
    // however, if the request is COUNTnCOUNT, do nothing
    if( calcMethod != DYTools::COUNTnCOUNT ){
      templatesFile = new TFile(templatesLabel);
      if( ! templatesFile->IsOpen() ) {
	std::cout << "templatesFile name " << templatesLabel << "\n";
	assert(0);
      }
    }
  }

  // Load selected events
  TString uScore="_";
  TString selectEventsFName=tagAndProbeDir + TString("/selectEvents_") 
    + DYTools::analysisTag + uScore
    + sampleTypeString + uScore +
    + effTypeString + uScore +  triggers.triggerSetName();
  if (performPUReweight) selectEventsFName.Append("_PU");
  selectEventsFName.Append(".root");
  std::cout << "selectEventsFName=<" << selectEventsFName << ">\n"; 
  TFile *selectedEventsFile = new TFile(selectEventsFName);
  if(!selectedEventsFile || !selectedEventsFile->IsOpen()) {
    std::cout << "failed to open file <" << selectEventsFName << ">\n";
    assert(0);
  }

  TTree *passTree = (TTree*)selectedEventsFile->Get("passTree");
  assert(passTree);
  TTree *failTree = (TTree*)selectedEventsFile->Get("failTree");
  assert(failTree);

  int numTagProbePairs = 0;
  int numTagProbePairsPassEt = 0;
  int numTagProbePairsPassEta = 0;
  int numTagProbePairsInMassWindow = 0;

  // Prepare histos
  tnpSelectEvent_t storeData;
  const int new_store_data_code=1;
  Double_t storeMass=0, storeEt=0, storeEta=0;
  UInt_t storeNGoodPV=0;
  if (new_store_data_code) {
    storeData.setBranchAddress(passTree);
    storeData.setBranchAddress(failTree);
  }
  else {
    passTree->SetBranchAddress("mass",&storeMass);
    passTree->SetBranchAddress("et",&storeEt);
    passTree->SetBranchAddress("eta",&storeEta);
    passTree->SetBranchAddress("nGoodPV",&storeNGoodPV);

    failTree->SetBranchAddress("mass",&storeMass);
    failTree->SetBranchAddress("et",&storeEt);
    failTree->SetBranchAddress("eta",&storeEta);
    failTree->SetBranchAddress("nGoodPV",&storeNGoodPV);
  }
  
  // Passing tree
  for (UInt_t ientry=0; ientry<passTree->GetEntries(); ++ientry) {
    passTree->GetEntry(ientry);
    
    numTagProbePairs++;
    // Apply probe cuts
    if (new_store_data_code) {
      if (storeData.et < 10) continue;
    }
    else {
      if(storeEt < 10) continue;
    }
    numTagProbePairsPassEt++;
    
    bool isGap = DYTools::isEcalGap(storeEta);
    if (new_store_data_code) {
      isGap = DYTools::isEcalGap(storeData.eta);
    }
    // For the probe, exclude eta gap only for one specific eta 
    // binning, barrel/endcap split
    if(etaBinning == DYTools::ETABINS2){
      if( isGap ) continue;
    }
    numTagProbePairsPassEta++;

    // Tag and probe is done around the Z peak
    if (new_store_data_code) {
      if (!storeData.insideMassWindow(massLow,massHigh)) continue;
    }
    else {
      if((storeMass < massLow) || (storeMass > massHigh)) continue;
    }
    numTagProbePairsInMassWindow++;
    
    // The probes are fully selected at this point.
    
    int templateBin=-1;
    if (new_store_data_code) {
      // total probes
      hMassTotal->Fill(storeData.mass);
      // passing probes
      hMassPass->Fill(storeData.mass);
      templateBin = 
	getTemplateBin( DYTools::findEtBin(storeData.et,etBinning),
			DYTools::findEtaBin(storeData.eta,etaBinning),
			etaBinning);

      if(sample != DYTools::DATA && templateBin != -1) {      
	int puIdx= (puDependence) ? DYTools::findPUBin(storeData.nGoodPV) : 0;
	if (puIdx>=0)
	  (*hPassTemplateV[puIdx])[templateBin]->Fill(storeData.mass,storeData.weight);
      }
    }
    else {
      // total probes
      hMassTotal->Fill(storeData.mass);
      // passing probes
      hMassPass->Fill(storeData.mass);

      templateBin = 
	getTemplateBin( DYTools::findEtBin(storeEt,etBinning),
			DYTools::findEtaBin(storeEta,etaBinning),
			etaBinning);

      if(sample != DYTools::DATA && templateBin != -1) {      
	int puIdx= (puDependence) ? DYTools::findPUBin(storeNGoodPV) : 0;
	if (puIdx>=0)
	  (*hPassTemplateV[puIdx])[templateBin]->Fill(storeMass);
      }
    }
    

  } // end loop pass entries

  // Failing tree
  for (UInt_t ientry=0; ientry<failTree->GetEntries(); ++ientry) {
    failTree->GetEntry(ientry);
    
    numTagProbePairs++;
    // Apply probe cuts
    if (new_store_data_code) {
      if(storeData.et < 10) continue;
    }
    else {
      if(storeEt < 10) continue;
    }
    numTagProbePairsPassEt++;
    
    bool isGap = DYTools::isEcalGap(storeEta);
    int templateBin=-1;
    if (new_store_data_code) {
      isGap = DYTools::isEcalGap(storeData.eta);
      // For the probe, exclude eta gap only for one specific eta 
      // binning, barrel/endcap split
      if(etaBinning == DYTools::ETABINS2){
	if( isGap ) continue;
      }
      numTagProbePairsPassEta++;
      
      // Tag and probe is done around the Z peak
      if (!storeData.insideMassWindow(massLow,massHigh)) continue;
      numTagProbePairsInMassWindow++;
      
      // The probes are fully selected at this point.
      
      // total probes
      hMassTotal->Fill(storeData.mass);
      // failing probes
      hMassFail->Fill(storeData.mass);
      
      templateBin = 
	getTemplateBin( DYTools::findEtBin(storeData.et,etBinning),
			DYTools::findEtaBin(storeData.eta,etaBinning),
			etaBinning);
      
      if(sample != DYTools::DATA && templateBin != -1) {
	int puIdx= (puDependence) ? DYTools::findPUBin(storeData.nGoodPV) : 0;
	if (puIdx>=0)
	  (*hFailTemplateV[puIdx])[templateBin]->Fill(storeData.mass,storeData.weight);
	else std::cout << "puIdx=" << puIdx << "\n";
      }
    }
    else{
      // For the probe, exclude eta gap only for one specific eta 
      // binning, barrel/endcap split
      if(etaBinning == DYTools::ETABINS2){
	if( isGap ) continue;
      }
      numTagProbePairsPassEta++;
      
      // Tag and probe is done around the Z peak
      if((storeMass < massLow) || (storeMass > massHigh)) continue;
      numTagProbePairsInMassWindow++;
      
      // The probes are fully selected at this point.
      
      // total probes
      hMassTotal->Fill(storeMass);
      // failing probes
      hMassFail->Fill(storeMass);
      
      templateBin = 
	getTemplateBin( DYTools::findEtBin(storeEt,etBinning),
			DYTools::findEtaBin(storeEta,etaBinning),
			etaBinning);
      
      if(sample != DYTools::DATA && templateBin != -1) {
	int puIdx= (puDependence) ? DYTools::findPUBin(storeNGoodPV) : 0;
	if (puIdx>=0)
	  (*hFailTemplateV[puIdx])[templateBin]->Fill(storeMass);
      }
    }
  } // end loop pass entries

  

  //
  // Efficiency analysis
  //
  
  if (effType == DYTools::RECO) {
    printf("\nTotal tag(electron)-probe(supercluster) pairs                %15d\n",numTagProbePairs);
  }
  else {
    printf("\nTotal tag-probe pairs                                        %15d\n",numTagProbePairs);
  }
  printf("               probe Et>10                                   %15d\n",numTagProbePairsPassEt);
  printf("               probe eta in acceptance                       %15d\n",numTagProbePairsPassEta);
  printf("               tag-probe mass in 60-120 GeV window           %15d\n",numTagProbePairsInMassWindow);

  printf("\nNumber of probes, total                                      %15.0f\n", double(passTree->GetEntries() + failTree->GetEntries()));
  printf("Number of probes, passed                                     %15.0f\n", double(passTree->GetEntries()));
  printf("Number of probes, failed                                     %15.0f\n", double(failTree->GetEntries()));

  // Human-readbale text file to store measured efficiencies
  TString reslog = tagAndProbeDir+TString("/efficiency_TnP_")+label+puTag+TString(".txt");
  ofstream effOutput;
  effOutput.open(reslog);
  // Print into the results file the header.
  effOutput << "Efficiency calculation method: " << calcMethodString.Data() << endl;
  effOutput << "Efficiency type to measure: " << effTypeString.Data() << endl;
  effOutput << "SC ET binning: " << etBinningString.Data() << endl;
  effOutput << "SC eta binning: " << etaBinningString.Data() << endl;
  effOutput << "Sample: " << sampleTypeString.Data() << endl;
  effOutput << "Files processed: " << endl;
  for(UInt_t i=0; i<ntupleFileNames.size(); i++)
    effOutput << "   " << ntupleFileNames[i].Data() << endl;
  effOutput << "selectEventsFName=" << selectEventsFName << endl;
  
  // ROOT file to store measured efficiencies in ROOT format
  TString resRootFileBase = tagAndProbeDir+TString("/efficiency_TnP_")+label+puTag;
  //TFile *resultsRootFile = new TFile(resroot,"recreate");

  // Fit log 
  TString fitlogname = TString("results_unsorted/efficiency_TnP_")+label+TString("_fitlog") + puTag + TString(".dat");
  ofstream fitLog;
  fitLog.open(fitlogname);

  //
  //  Find efficiency
  //
  bool useTemplates = false;
  if(sample == DYTools::DATA)
    useTemplates = true;

  int NsetBins=30;
  //   bool isRECO=(effType == DYTools::RECO) ? true : false;
  const char* setBinsType="cache";

  int nDivisions = DYTools::getNEtBins(etBinning)*DYTools::getNEtaBins(etaBinning);
  //std::cout << "nDivisions=" << getNEtBins(etBinning) << "*" << getNEtaBins(etaBinning) << "=" << nDivisions << "\n";
  double ymax = 800;
  if(nDivisions <4 )
    ymax = nDivisions * 200;
  else if (nDivisions>DYTools::maxTnPCanvasDivisions) {
    nDivisions=DYTools::maxTnPCanvasDivisions;
  }

  TCanvas *c1 = MakeCanvas("canvDistr","canvDistr", 600, (int)ymax);
  c1->Divide(2,nDivisions);
  
  measureEfficiencyPU(passTree, failTree,
		    calcMethod, etBinning, etaBinning, c1, effOutput, fitLog,
		    useTemplates, templatesFile, resRootFileBase,
		    NsetBins, effType, setBinsType,
		    dirTag, triggers.triggerSetName(),
		    puDependence);

  

  effOutput.close();
  fitLog.close();
  TString command = "cat ";
  command += reslog;
  system(command.Data());

  TString fitpicname = tagAndProbeDir+TString("/efficiency_TnP_")+label+puTag+TString(".png");
  //c1->Update();
  c1->SaveAs(fitpicname);

  // Save MC templates
  if(sample != DYTools::DATA){
    templatesFile->cd();
    for(int i=0; i<DYTools::getNEtBins(etBinning); i++){
      for(int j=0; j<DYTools::getNEtaBins(etaBinning); j++){
	int templateBin = getTemplateBin( i, j, etaBinning);
	int puMax=(puDependence) ? DYTools::nPVBinCount : 1;
	for (int pu_i=0; pu_i<puMax; ++pu_i) {
	  (*hPassTemplateV[pu_i])[templateBin]->Write();
	  (*hFailTemplateV[pu_i])[templateBin]->Write();
	}
      }
    }
    templatesFile->Close();
    std::cout << "file templatesLabel=" << templatesLabel << " created\n";
  }

    
  selectedEventsFile->Close();
  std::cout << "selectedEventsFile <" << selectEventsFName << "> was used\n";
 
  gBenchmark->Show("calcEff");
  
  
}


