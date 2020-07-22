/*
   Copyright 2012 University of Washington

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#include "Common.h"
#include "CometDataInternal.h"
#include "CometMassSpecUtils.h"
#include "CometWriteMzIdentML.h"
#include "CometSearchManager.h"
#include "CometStatus.h"

#include "limits.h"
#include "stdlib.h"

#ifdef _WIN32
#define PATH_MAX _MAX_PATH
#endif

CometWriteMzIdentML::CometWriteMzIdentML()
{
}


CometWriteMzIdentML::~CometWriteMzIdentML()
{
}


void CometWriteMzIdentML::WriteMzIdentMLTmp(FILE *fpout,
                                            FILE *fpoutd)
{
   int i;

   // Print temporary results in tab-delimited file
   if (g_staticParams.options.iDecoySearch == 2)
   {
      for (i=0; i<(int)g_pvQuery.size(); i++)
         PrintTmpPSM(i, 1, fpout);
      for (i=0; i<(int)g_pvQuery.size(); i++)
         PrintTmpPSM(i, 2, fpoutd);
   }
   else
   {
      for (i=0; i<(int)g_pvQuery.size(); i++)
         PrintTmpPSM(i, 0, fpout);
   }

   fflush(fpout);
}

void CometWriteMzIdentML::WriteMzIdentML(FILE *fpout,
                                         FILE *fpdb,
                                         char *szTmpFile)
{
   WriteMzIdentMLHeader(fpout);

   // now loop through szTmpFile file, wr

   WriteSequenceCollection(fpout, fpdb, szTmpFile);


   WriteMzIdentMLEndTags(fpout);
}

bool CometWriteMzIdentML::WriteMzIdentMLHeader(FILE *fpout)
{
   time_t tTime;
   char szDate[48];
   char szManufacturer[SIZE_FILE];
   char szModel[SIZE_FILE];

   time(&tTime);
   strftime(szDate, 46, "%Y-%m-%dT%H:%M:%S", localtime(&tTime));

   // Get msModel + msManufacturer from mzXML. Easy way to get from mzML too?
   ReadInstrument(szManufacturer, szModel);

   // The msms_run_summary base_name must be the base name to mzXML input.
   // This might not be the case with -N command line option.
   // So get base name from g_staticParams.inputFile.szFileName here to be sure
   char *pStr;
   char szRunSummaryBaseName[PATH_MAX];          // base name of szInputFile
   char szRunSummaryResolvedPath[PATH_MAX];      // resolved path of szInputFile
   int  iLen = (int)strlen(g_staticParams.inputFile.szFileName);
   strcpy(szRunSummaryBaseName, g_staticParams.inputFile.szFileName);
   if ( (pStr = strrchr(szRunSummaryBaseName, '.')))
      *pStr = '\0';

   if (!STRCMP_IGNORE_CASE(g_staticParams.inputFile.szFileName + iLen - 9, ".mzXML.gz")
         || !STRCMP_IGNORE_CASE(g_staticParams.inputFile.szFileName + iLen - 8, ".mzML.gz"))
   {
      if ( (pStr = strrchr(szRunSummaryBaseName, '.')))
         *pStr = '\0';
   }

   char resolvedPathBaseName[PATH_MAX];
#ifdef _WIN32
   _fullpath(resolvedPathBaseName, g_staticParams.inputFile.szBaseName, PATH_MAX);
   _fullpath(szRunSummaryResolvedPath, szRunSummaryBaseName, PATH_MAX);
#else
   realpath(g_staticParams.inputFile.szBaseName, resolvedPathBaseName);
   realpath(szRunSummaryBaseName, szRunSummaryResolvedPath);
#endif

   // Write out pepXML header.
   fprintf(fpout, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");

   fprintf(fpout, "<MzIdentML id=\"Comet %s\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xsi:schemaLocation=\"http://psidev.info/psi/pi/mzIdentML/1.2 http://www.psidev.info/files/mzIdentML1.2.0.xsd\" xmlns=\"http://psidev.info/psi/pi/mzIdentML/1.2\" version=\"1.2.0\" creationDate=\"%s\">\n", szDate, comet_version);
   fprintf(fpout, " <cvList>\n");
   fprintf(fpout, "  <cv id=\"PSI-MS\" uri=\"https://raw.githubusercontent.com/HUPO-PSI/psi-ms-CV/master/psi-ms.obo\" fullName=\"PSI-MS\"/>\n");
   fprintf(fpout, "  <cv id=\"UNIMOD\" uri=\"http://www.unimod.org/obo/unimod.obo\" fullName=\"UNIMOD\"/>\n");
   fprintf(fpout, "  <cv id=\"UO\" uri=\"https://raw.githubusercontent.com/bio-ontology-research-group/unit-ontology/master/unit.obo\" fullName=\"UNIT-ONTOLOGY\"/>\n");
   fprintf(fpout, "  <cv id=\"PRIDE\" uri=\"https://github.com/PRIDE-Utilities/pride-ontology/blob/master/pride_cv.obo\" fullName=\"PRIDE\"/>\n");
   fprintf(fpout, " </cvList>\n");


   fprintf(fpout, " <AnalysisSoftwareList>\n");
   fprintf(fpout, "  <AnalysisSoftware id=\"AS_Comet\" name=\"Comet\" version=\"%s\">\n", comet_version);
   fprintf(fpout, "   <SoftwareName><cvParam cvRef=\"MS\" accession=\"MS:1002251\" name=\"Comet\" value=\"\"/></SoftwareName>\n");
   fprintf(fpout, "  </AnalysisSoftware>\n");
   fprintf(fpout, " </AnalysisSoftwareList>\n");

   fflush(fpout);

   return true;
}


struct MzidTmpStruct
{
   int    iScanNumber;
   int    iXcorrRank;
   int    iCharge;
   double dExpMass;   // neutral experimental mass
   double dCalcMass;  // neutral calculated mass
   double dExpect;
   float  fXcorr;
   float  fCn;
   float  fSp;
   string strPeptide;
   char   cPrevNext[3];
   string strMods;
   string strProtsTarget;   // delimited list of file offsets
   string strProtsDecoy;    // delimited list of file offsets
};


bool CometWriteMzIdentML::WriteSequenceCollection(FILE *fpout,
                                                  FILE *fpdb,
                                                  char *szTmpFile)
{
   std::vector<MzidTmpStruct> vMzidTmp; // vector to store entire tmp output
   std::vector<long> vProteinTargets;   // store vector of target protein file offsets
   std::vector<long> vProteinDecoys;    // store vector of decoy protein file offsets
   std::vector<string> vstrPeptides;      // vector of peptides of format "QITQMSNSSDLADGLNFDEGDELLK;2:79.9969;4:15.9949;"
   std::vector<string> vstrPeptideEvidence;     // vector of peptides + target&decoy prots, space delmited "QITQMSNSSDLADGLNFDEGDELLK 1;38;75;112; 149;185;221;257;"

   fprintf(fpout, " <SequenceCollection xmlns=\"http://psidev.info/psi/pi/mzIdentML/1.2\">\n");

   // get all protein file positions by parsing through fpout_tmp
   // column 15 is target proteins, column 16 is decoy proteins

   std::ifstream ifsTmpFile(szTmpFile);

   if (ifsTmpFile.is_open())
   {
      std::string strLine;  // line
      std::string strTmpPep;
      std::string strLocal;

      while (std::getline(ifsTmpFile, strLine))
      {
         struct MzidTmpStruct Stmp;


         std::string field;
         std::istringstream isString(strLine);

         int iWhichField = 0;
         while ( std::getline(isString, field, '\t') )
         {
            switch(iWhichField)
            {
               case 0:
                  Stmp.iScanNumber = std::stoi(field);
                  break;
               case 1:
                  Stmp.iXcorrRank = std::stoi(field);
                  break;
               case 2:
                  Stmp.iCharge = std::stoi(field);
                  break;
               case 3:
                  Stmp.dExpMass= std::stod(field);
                  break;
               case 4:
                  Stmp.dCalcMass = std::stod(field);
                  break;
               case 5:
                  Stmp.dExpect = std::stod(field);
                  break;
               case 6:
                  Stmp.fXcorr = std::stof(field);
                  break;
               case 7:
                  Stmp.fCn = std::stof(field);
                  break;
               case 8:
                  Stmp.fSp = std::stof(field);
                  break;
               case 9:
                  break;
               case 10:
                  break;
               case 11:
                  Stmp.strPeptide = field;
                  break;
               case 12:
                  Stmp.cPrevNext[0] = field.at(0);
                  Stmp.cPrevNext[1] = field.at(1);
                  break;
               case 13:
                  Stmp.strMods = field;
                  break;
               case 14:
                  Stmp.strProtsTarget = field;
                  break;
               case 15:
                  Stmp.strProtsDecoy = field;
                  break;
               default:
                  char szErrorMsg[SIZE_ERROR];
                  sprintf(szErrorMsg,  " Error parsing mzid temp file (%d): %s\n", iWhichField, strLine.c_str());
                  string strErrorMsg(szErrorMsg);
                  g_cometStatus.SetStatus(CometResult_Failed, strErrorMsg);
                  logerr(szErrorMsg);
                  ifsTmpFile.close();
                  return false;
            }
            iWhichField++;
         }

         vMzidTmp.push_back(Stmp);

         // first grab all of the target protein offsets
         if (Stmp.strProtsTarget.length() > 0)
         {
            std::istringstream isString(Stmp.strProtsTarget);

            while ( std::getline(isString, strLocal, ';') )
            {
               vProteinTargets.push_back(atol(strLocal.c_str()));
            }
         }

         if (Stmp.strProtsDecoy.length() > 0)
         {
            std::istringstream isString(Stmp.strProtsDecoy);

            while ( std::getline(isString, strLocal, ';') )
            {
               vProteinDecoys.push_back(atol(strLocal.c_str()));
            }
         }

         // vstrPeptides contains "peptide;mods" string
         strTmpPep = Stmp.strPeptide;
         strTmpPep.append(";");
         strTmpPep.append(Stmp.strMods);
         vstrPeptides.push_back(strTmpPep);

         strTmpPep = Stmp.strPeptide;
         strTmpPep.append(" ");
         strTmpPep.append(Stmp.strProtsTarget);
         strTmpPep.append(" ");
         strTmpPep.append(Stmp.strProtsDecoy);
         vstrPeptideEvidence.push_back(strTmpPep);
      }

      ifsTmpFile.close();

      // now generate unique lists of file offsets and peptides
      std::sort(vProteinTargets.begin(), vProteinTargets.end());
      vProteinTargets.erase(std::unique(vProteinTargets.begin(), vProteinTargets.end()), vProteinTargets.end());

      std::sort(vProteinDecoys.begin(), vProteinDecoys.end());
      vProteinDecoys.erase(std::unique(vProteinDecoys.begin(), vProteinDecoys.end()), vProteinDecoys.end());

      std::sort(vstrPeptides.begin(), vstrPeptides.end());
      vstrPeptides.erase(std::unique(vstrPeptides.begin(), vstrPeptides.end()), vstrPeptides.end());

      std::sort(vstrPeptideEvidence.begin(), vstrPeptideEvidence.end());
      vstrPeptideEvidence.erase(std::unique(vstrPeptideEvidence.begin(), vstrPeptideEvidence.end()), vstrPeptideEvidence.end());

      // print DBSequence element
      std::vector<long>::iterator it;
      char szProteinName[100];

      for (it = vProteinTargets.begin(); it != vProteinTargets.end(); it++)
      {
         CometMassSpecUtils::GetProteinName(fpdb, *it, szProteinName);
         fprintf(fpout, " <DBSequence id=\"%s\" accession=\"%s\" searchDatabase_ref=\"DB_1\"/>\n", szProteinName, szProteinName);
      }
      for (it = vProteinDecoys.begin(); it != vProteinDecoys.end(); it++)
      {
         CometMassSpecUtils::GetProteinName(fpdb, *it, szProteinName);
         fprintf(fpout, " <DBSequence id=\"%s%s\" accession=\"%s%s\" searchDatabase_ref=\"DB_1\"/>\n",
               g_staticParams.szDecoyPrefix, szProteinName, g_staticParams.szDecoyPrefix, szProteinName);
      }

      vProteinTargets.clear();
      vProteinDecoys.clear();

      // print Peptide element
      std::vector<string>::iterator it2;
      int iLen;
      for (it2 = vstrPeptides.begin(); it2 != vstrPeptides.end(); it2++)
      {
         std::istringstream isString(*it2);

         fprintf(fpout, " <Peptide id=\"%s\">\n", (*it2).c_str());

         std::getline(isString, strLocal, ';');
         fprintf(fpout, "  <PeptideSequence>%s</PeptideSequence>\n", strLocal.c_str());
         iLen = strLocal.length();

         while ( std::getline(isString, strLocal, ';') )
         {
            if (strLocal.size() > 0)
            {
               int iPosition = 0;
               double dMass = 0;

               sscanf(strLocal.c_str(), "%d:%lf", &iPosition, &dMass);

               if (iPosition == iLen)  // n-term
                  iPosition = 0;
               else if (iPosition == iLen+1)  // c-term
                  iPosition = iLen;
               else
                  iPosition += 1;

               fprintf(fpout, "  <Modification location=\"%d\" monoisotopicMassDelta=\"%f\"></Modification>\n", iPosition, dMass);
            }
         }

         fprintf(fpout, " </Peptide>\n");
      }


      // Now write PeptideEvidence to map every peptide to every protein sequence.
      // Need unique set of peptide+mods and proteins
      for (it2 = vstrPeptideEvidence.begin(); it2 != vstrPeptideEvidence.end(); it2++)
      {
         string strPeptide;
         string strTargets;
         string strDecoys;
         bool bIsDecoy = true;

         std::istringstream isString(*it2);

         int n=0;
         while ( std::getline(isString, strLocal, ' ') )
         {
            switch (n)
            {
               case 0:
                  strPeptide = strLocal;
                  break;
               case 1:
                  if (strLocal.length() > 0)
                  {
                     bIsDecoy = false;

//                   Now parse out individual target entries
                  }

                  break;
               case 2:
                  if (strLocal.length() > 0)
                  {
//                   Now parse out individual decoy entries
                  }
                  break;
            }
            vProteinTargets.push_back(atol(strLocal.c_str()));

            n++;
         }

         fprintf(fpout, " <PeptideEvidence id=\"%s\" isDecoy=\"%s\" DBSequence_Ref=\"%s\" />\n", strPeptide.c_str(), bIsDecoy?"true":"false", "");
      }
      
   }

   fprintf(fpout, " </SequenceCollection>\n");

   return true;
}


void CometWriteMzIdentML::WriteAnalysisProtocol(FILE *fpout)
{
   fprintf(fpout, " <AnalysisCollection>\n");
   fprintf(fpout, "  <SpectrumIdentification spectrumIdentificationList_ref=\"SIL_1\" spectrumIdentificationProtocol_ref=\"SIP_1\" id=\"SpecIdent_1\">\n");
   fprintf(fpout, "   <InputSpectra spectraData_ref=\"qExactive01819.mgf\"/>\n");
   fprintf(fpout, "   <SearchDatabaseRef searchDatabase_ref=\"SearchDB_1\"/>\n");
   fprintf(fpout, "  </SpectrumIdentification>\n");
   fprintf(fpout, "  <ProteinDetection proteinDetectionProtocol_ref=\"PeptideShaker_1\" proteinDetectionList_ref=\"Protein_groups\" id=\"PD_1\">\n");
   fprintf(fpout, "   <InputSpectrumIdentifications spectrumIdentificationList_ref=\"SIL_1\"/>\n");
   fprintf(fpout, "  </ProteinDetection>\n");

   fprintf(fpout, " </AnalysisCollection>\n");

   fprintf(fpout, " <AnalysisProtocolCollection>\n");
   fprintf(fpout, "  <SpectrumIdentificationProtocol analysisSoftware_ref=\"ID_software\" id=\"SIP_1\">\n");
   fprintf(fpout, "   <SearchType>\n");
   fprintf(fpout, "    <cvParam cvRef=\"PSI-MS\" accession=\"MS:1001083\" name=\"ms-ms search\"/>\n");
   fprintf(fpout, "   </SearchType>\n");
   fprintf(fpout, "  </SpectrumIdentificationProtocol>\n");
   fprintf(fpout, " </AnalysisProtocolCollection>\n");


   fprintf(fpout, " <ModificationParams>\n");
   fprintf(fpout, " </ModificationParams>\n");
}

void CometWriteMzIdentML::WriteVariableMod(FILE *fpout,
                                           CometSearchManager &searchMgr,
                                           string varModName,
                                           bool bWriteTerminalMods)
{
   VarMods varModsParam;
   if (searchMgr.GetParamValue(varModName, varModsParam))
   {
      char cSymbol = '-';
      if (varModName[13]=='1')
         cSymbol = g_staticParams.variableModParameters.cModCode[0];
      else if (varModName[13]=='2')
         cSymbol = g_staticParams.variableModParameters.cModCode[1];
      else if (varModName[13]=='3')
         cSymbol = g_staticParams.variableModParameters.cModCode[2];
      else if (varModName[13]=='4')
         cSymbol = g_staticParams.variableModParameters.cModCode[3];
      else if (varModName[13]=='5')
         cSymbol = g_staticParams.variableModParameters.cModCode[4];
      else if (varModName[13]=='6')
         cSymbol = g_staticParams.variableModParameters.cModCode[5];
      else if (varModName[13]=='7')
         cSymbol = g_staticParams.variableModParameters.cModCode[6];
      else if (varModName[13]=='8')
         cSymbol = g_staticParams.variableModParameters.cModCode[7];
      else if (varModName[13]=='9')
         cSymbol = g_staticParams.variableModParameters.cModCode[8];

      if (cSymbol != '-' && !isEqual(varModsParam.dVarModMass, 0.0))
      {
         int iLen = (int)strlen(varModsParam.szVarModChar);
         for (int i=0; i<iLen; i++)
         {
            if (varModsParam.szVarModChar[i]=='n' && bWriteTerminalMods)
            {
               if (varModsParam.iVarModTermDistance == 0 && (varModsParam.iWhichTerm == 1 || varModsParam.iWhichTerm == 3))
               {
                  // ignore if N-term mod on C-term
               }
               else
               {
                  double dMass = 0.0;
                  searchMgr.GetParamValue("add_Nterm_protein", dMass);

                  // print this if N-term protein variable mod or a generic N-term mod there's also N-term protein static mod
                  if (varModsParam.iWhichTerm == 0 && varModsParam.iVarModTermDistance == 0)
                  {
                     // massdiff = mod mass + h
                     fprintf(fpout, "  <terminal_modification terminus=\"N\" massdiff=\"%0.6f\" mass=\"%0.6f\" variable=\"Y\" protein_terminus=\"Y\" symbol=\"%c\"/>\n",
                           varModsParam.dVarModMass,
                           varModsParam.dVarModMass
                              + dMass
                              + g_staticParams.precalcMasses.dNtermProton
                              - PROTON_MASS + g_staticParams.massUtility.pdAAMassFragment[(int)'h'],
                           cSymbol);
                  }
                  // print this if non-protein N-term variable mod
                  else
                  {
                     fprintf(fpout, "  <terminal_modification terminus=\"N\" massdiff=\"%0.6f\" mass=\"%0.6f\" variable=\"Y\" protein_terminus=\"N\" symbol=\"%c\"/>\n",
                           varModsParam.dVarModMass,
                           varModsParam.dVarModMass
                              + g_staticParams.precalcMasses.dNtermProton
                              - PROTON_MASS + g_staticParams.massUtility.pdAAMassFragment[(int)'h'],
                           cSymbol);
                  }
               }
            }
            else if (varModsParam.szVarModChar[i]=='c' && bWriteTerminalMods)
            {
               if (varModsParam.iVarModTermDistance == 0 && (varModsParam.iWhichTerm == 0 || varModsParam.iWhichTerm == 2))
               {
                  // ignore if C-term mod on N-term
               }
               else
               {
                  double dMass = 0.0;
                  searchMgr.GetParamValue("add_Cterm_protein", dMass);

                  // print this if C-term protein variable mod or a generic C-term mod there's also C-term protein static mod
                  if (varModsParam.iWhichTerm == 1 && varModsParam.iVarModTermDistance == 0)
                  {
                     // massdiff = mod mass + oh
                     fprintf(fpout, "  <terminal_modification terminus=\"C\" massdiff=\"%0.6f\" mass=\"%0.6f\" variable=\"Y\" protein_terminus=\"Y\" symbol=\"%c\"/>\n",
                           varModsParam.dVarModMass,
                           varModsParam.dVarModMass
                              + dMass
                              + g_staticParams.precalcMasses.dCtermOH2Proton
                              - PROTON_MASS
                              - g_staticParams.massUtility.pdAAMassFragment[(int)'h'],
                           cSymbol);
                  }
                  // print this if non-protein C-term variable mod
                  else
                  {
                     fprintf(fpout, "  <terminal_modification terminus=\"C\" massdiff=\"%0.6f\" mass=\"%0.6f\" variable=\"Y\" protein_terminus=\"N\" symbol=\"%c\"/>\n",
                           varModsParam.dVarModMass,
                           varModsParam.dVarModMass
                              + g_staticParams.precalcMasses.dCtermOH2Proton
                              - PROTON_MASS
                              - g_staticParams.massUtility.pdAAMassFragment[(int)'h'],
                           cSymbol);
                  }
               }
            }
            else if (!bWriteTerminalMods && varModsParam.szVarModChar[i]!='c' && varModsParam.szVarModChar[i]!='n')
            {
               fprintf(fpout, "  <aminoacid_modification aminoacid=\"%c\" massdiff=\"%0.6f\" mass=\"%0.6f\" variable=\"Y\" %ssymbol=\"%c\"/>\n",
                     varModsParam.szVarModChar[i],
                     varModsParam.dVarModMass,
                     g_staticParams.massUtility.pdAAMassParent[(int)varModsParam.szVarModChar[i]] + varModsParam.dVarModMass,
                     (varModsParam.iBinaryMod?"binary=\"Y\" ":""),
                     cSymbol);
            }
         }
      }
   }
}


void CometWriteMzIdentML::WriteStaticMod(FILE *fpout,
                                         CometSearchManager &searchMgr,
                                         string paramName)
{
/*
   double dMass = 0.0;
   if (searchMgr.GetParamValue(paramName, dMass))
   {
      if (!isEqual(dMass, 0.0))
      {
         fprintf(fpout, "<SearchModification residues=\"%c\" massDelta=\"%0.6f\" fixedMod= \"true\" >\n", paramName[4], dMass,);
         fprintf(fpout, "  <cvParam cvRef=\"UNIMOD\" accession=\"UNIMOD:4\" name=\"Carbamidomethyl\"/>\n");
         fprintf(fpout, "</SearchModification>\n");
      }
   }
*/
}

void CometWriteMzIdentML::WriteMzIdentMLEndTags(FILE *fpout)
{
   fprintf(fpout, "</MzIdentML>\n");
   fflush(fpout);
}

void CometWriteMzIdentML::PrintTmpPSM(int iWhichQuery,
                                      int iPrintTargetDecoy,
                                      FILE *fpout)
{
   if ((iPrintTargetDecoy != 2 && g_pvQuery.at(iWhichQuery)->_pResults[0].fXcorr > XCORR_CUTOFF)
         || (iPrintTargetDecoy == 2 && g_pvQuery.at(iWhichQuery)->_pDecoys[0].fXcorr > XCORR_CUTOFF))
   {
      Query* pQuery = g_pvQuery.at(iWhichQuery);

      Results *pOutput;
      int iNumPrintLines;

      if (iPrintTargetDecoy == 2)  // decoys
      {
         pOutput = pQuery->_pDecoys;
         iNumPrintLines = pQuery->iDecoyMatchPeptideCount;
      }
      else  // combined or separate targets
      {
         pOutput = pQuery->_pResults;
         iNumPrintLines = pQuery->iMatchPeptideCount;
      }

      if (iNumPrintLines > g_staticParams.options.iNumPeptideOutputLines)
         iNumPrintLines = g_staticParams.options.iNumPeptideOutputLines;

      int iMinLength = 999;
      for (int i=0; i<iNumPrintLines; i++)
      {
         int iLen = (int)strlen(pOutput[i].szPeptide);
         if (iLen == 0)
            break;
         if (iLen < iMinLength)
            iMinLength = iLen;
      }

      int iRankXcorr = 1;

      for (int iWhichResult=0; iWhichResult<iNumPrintLines; iWhichResult++)
      {
         int j;
         double dDeltaCn = 1.0;

         if (pOutput[iWhichResult].fXcorr <= XCORR_CUTOFF)
            continue;

         // go one past iNumPrintLines to calculate deltaCn value
         for (j=iWhichResult+1; j<iNumPrintLines+1; j++)
         {
            if (j<g_staticParams.options.iNumStored)
            {
               // very poor way of calculating peptide similarity but it's what we have for now
               int iDiffCt = 0;

               for (int k=0; k<iMinLength; k++)
               {
                  // I-L and Q-K are same for purposes here
                  if (pOutput[iWhichResult].szPeptide[k] != pOutput[j].szPeptide[k])
                  {
                     if (!((pOutput[0].szPeptide[k] == 'K' || pOutput[0].szPeptide[k] == 'Q')
                              && (pOutput[j].szPeptide[k] == 'K' || pOutput[j].szPeptide[k] == 'Q'))
                           && !((pOutput[0].szPeptide[k] == 'I' || pOutput[0].szPeptide[k] == 'L')
                              && (pOutput[j].szPeptide[k] == 'I' || pOutput[j].szPeptide[k] == 'L')))
                     {
                        iDiffCt++;
                     }
                  }
               }

               // calculate deltaCn only if sequences are less than 0.75 similar
               if ( ((double) (iMinLength - iDiffCt)/iMinLength) < 0.75)
               {
                  if (pOutput[iWhichResult].fXcorr > 0.0 && pOutput[j].fXcorr >= 0.0)
                     dDeltaCn = 1.0 - pOutput[j].fXcorr/pOutput[iWhichResult].fXcorr;
                  else if (pOutput[iWhichResult].fXcorr > 0.0 && pOutput[j].fXcorr < 0.0)
                     dDeltaCn = 1.0;
                  else
                     dDeltaCn = 0.0;

                  break;
               }
            }
         }

         if (iWhichResult > 0 && !isEqual(pOutput[iWhichResult].fXcorr, pOutput[iWhichResult-1].fXcorr))
            iRankXcorr++;

         fprintf(fpout, "%d\t", pQuery->_spectrumInfoInternal.iScanNumber);
         fprintf(fpout, "%d\t", iRankXcorr);
         fprintf(fpout, "%d\t", pQuery->_spectrumInfoInternal.iChargeState);
         fprintf(fpout, "%0.6f\t", pQuery->_pepMassInfo.dExpPepMass - PROTON_MASS);
         fprintf(fpout, "%0.6f\t", pOutput[iWhichResult].dPepMass - PROTON_MASS);
         fprintf(fpout, "%0.2E\t", pOutput[iWhichResult].dExpect);
         fprintf(fpout, "%0.4f\t", pOutput[iWhichResult].fXcorr);
         fprintf(fpout, "%0.4f\t", dDeltaCn);
         fprintf(fpout, "%0.1f\t", pOutput[iWhichResult].fScoreSp);
         fprintf(fpout, "%d\t", pOutput[iWhichResult].iMatchedIons);
         fprintf(fpout, "%d\t", pOutput[iWhichResult].iTotalIons);

         // plain peptide
         fprintf(fpout, "%s\t", pOutput[iWhichResult].szPeptide);

         // prev/next AA
         fprintf(fpout, "%c%c\t", pOutput[iWhichResult].szPrevNextAA[0], pOutput[iWhichResult].szPrevNextAA[1]);

         // modifications:  zero-position:mass; semi-colon delimited; length=nterm, length+1=c-term

         if (pOutput[iWhichResult].piVarModSites[pOutput[iWhichResult].iLenPeptide] > 0)
         {
            fprintf(fpout, "%d:%0.6f;", pOutput[iWhichResult].iLenPeptide, 
                  g_staticParams.variableModParameters.varModList[(int)pOutput[iWhichResult].piVarModSites[pOutput[iWhichResult].iLenPeptide]-1].dVarModMass);
         }

         if (pOutput[iWhichResult].piVarModSites[pOutput[iWhichResult].iLenPeptide+1] > 0)
         {
            fprintf(fpout, "%d:%0.6f;", pOutput[iWhichResult].iLenPeptide + 1, 
                  g_staticParams.variableModParameters.varModList[(int)pOutput[iWhichResult].piVarModSites[pOutput[iWhichResult].iLenPeptide+1]-1].dVarModMass);
         }

         for (int i=0; i<pOutput[iWhichResult].iLenPeptide; i++)
         {
            if (pOutput[iWhichResult].piVarModSites[i] != 0)
               fprintf(fpout, "%d:%0.6f;", i, pOutput[iWhichResult].pdVarModSites[i]);
         }

         fprintf(fpout, "\t");

         // comma separated list of fpdb pointers for target proteins
         std::vector<ProteinEntryStruct>::iterator it;
         if (pOutput[iWhichResult].pWhichProtein.size() > 0)
         {
            for (it=pOutput[iWhichResult].pWhichProtein.begin(); it!=pOutput[iWhichResult].pWhichProtein.end(); ++it)
            {
               fprintf(fpout, "%ld;", (*it).lWhichProtein);
            }
            fprintf(fpout, "\t");
         }
         else
         {
            fprintf(fpout, "-1\t");
         }

         // comma separated list of fpdb pointers for decoy proteins
         if (pOutput[iWhichResult].pWhichDecoyProtein.size() > 0)
         {
            for (it=pOutput[iWhichResult].pWhichDecoyProtein.begin(); it!=pOutput[iWhichResult].pWhichDecoyProtein.end(); ++it)
            {
               fprintf(fpout, "%ld;", (*it).lWhichProtein);
            }
            fprintf(fpout, "\t");
         }
         else
         {
            fprintf(fpout, "-1\t");
         }

 
         fprintf(fpout, "\n");
      }
   }
}


void CometWriteMzIdentML::PrintMzIdentMLSearchHit(int iWhichQuery,
                                                  int iWhichResult,
                                                  int iRankXcorr,
                                                  bool bDecoy,
                                                  Results *pOutput,
                                                  FILE *fpout,
                                                  FILE *fpdb,
                                                  double dDeltaCn,
                                                  double dDeltaCnStar)
{
   int  i;
   int iNTT;
   int iNMC;
   bool bPrintDecoyPrefix = false;

   Query* pQuery = g_pvQuery.at(iWhichQuery);

   CalcNTTNMC(pOutput, iWhichResult, &iNTT, &iNMC);

   char szProteinName[100];
   std::vector<ProteinEntryStruct>::iterator it;
   
   int iNumTotProteins = 0;

   if (bDecoy)
   {
      it=pOutput[iWhichResult].pWhichDecoyProtein.begin();
      iNumTotProteins = (int)pOutput[iWhichResult].pWhichDecoyProtein.size();
      bPrintDecoyPrefix = true;
   }
   else
   {
      // if not reporting separate decoys, it's possible only matches
      // in combined search are decoy entries
      if (pOutput[iWhichResult].pWhichProtein.size() > 0)
      {
         it=pOutput[iWhichResult].pWhichProtein.begin();
         iNumTotProteins = (int)(pOutput[iWhichResult].pWhichProtein.size() + pOutput[iWhichResult].pWhichDecoyProtein.size());
      }
      else  // only decoy matches in this search
      {
         it=pOutput[iWhichResult].pWhichDecoyProtein.begin();
         iNumTotProteins = (int)(pOutput[iWhichResult].pWhichDecoyProtein.size());
         bPrintDecoyPrefix = true;
      }
   }

   CometMassSpecUtils::GetProteinName(fpdb, (*it).lWhichProtein, szProteinName);
   ++it;

   fprintf(fpout, "   <search_hit hit_rank=\"%d\"", iRankXcorr);
   fprintf(fpout, " peptide=\"%s\"", pOutput[iWhichResult].szPeptide);
   fprintf(fpout, " peptide_prev_aa=\"%c\"", pOutput[iWhichResult].szPrevNextAA[0]);
   fprintf(fpout, " peptide_next_aa=\"%c\"", pOutput[iWhichResult].szPrevNextAA[1]);
   if (bPrintDecoyPrefix)
      fprintf(fpout, " protein=\"%s%s\"", g_staticParams.szDecoyPrefix, szProteinName);
   else
      fprintf(fpout, " protein=\"%s\"", szProteinName);
   fprintf(fpout, " num_tot_proteins=\"%d\"", iNumTotProteins);
   fprintf(fpout, " num_matched_ions=\"%d\"", pOutput[iWhichResult].iMatchedIons);
   fprintf(fpout, " tot_num_ions=\"%d\"", pOutput[iWhichResult].iTotalIons);
   fprintf(fpout, " calc_neutral_pep_mass=\"%0.6f\"", pOutput[iWhichResult].dPepMass - PROTON_MASS);
   fprintf(fpout, " massdiff=\"%0.6f\"", pQuery->_pepMassInfo.dExpPepMass - pOutput[iWhichResult].dPepMass);
   fprintf(fpout, " num_tol_term=\"%d\"", iNTT);
   fprintf(fpout, " num_missed_cleavages=\"%d\"", iNMC);
   fprintf(fpout, " num_matched_peptides=\"%lu\"", bDecoy?(pQuery->_uliNumMatchedDecoyPeptides):(pQuery->_uliNumMatchedPeptides));
   fprintf(fpout, ">\n");

   int iPrintDuplicateProteinCt = 0;

   // Print protein reference/accession.
   for (; it!=(bPrintDecoyPrefix?pOutput[iWhichResult].pWhichDecoyProtein.end():pOutput[iWhichResult].pWhichProtein.end()); ++it)
   {
      szProteinName[0]='\0';
      CometMassSpecUtils::GetProteinName(fpdb, (*it).lWhichProtein, szProteinName);
      if (bPrintDecoyPrefix)
         fprintf(fpout, "    <alternative_protein protein=\"%s%s\"/>\n", g_staticParams.szDecoyPrefix, szProteinName);
      else
         fprintf(fpout, "    <alternative_protein protein=\"%s\"/>\n", szProteinName);

      iPrintDuplicateProteinCt++;
      if (iPrintDuplicateProteinCt == g_staticParams.options.iMaxDuplicateProteins)
         break;
   }

   // If combined search printed out target proteins above, now print out decoy proteins if necessary
   if (!bDecoy && pOutput[iWhichResult].pWhichProtein.size() > 0 && pOutput[iWhichResult].pWhichDecoyProtein.size() > 0
         && iPrintDuplicateProteinCt < g_staticParams.options.iMaxDuplicateProteins)
   {
      for (it=pOutput[iWhichResult].pWhichDecoyProtein.begin(); it!=pOutput[iWhichResult].pWhichDecoyProtein.end(); ++it)
      {
         CometMassSpecUtils::GetProteinName(fpdb, (*it).lWhichProtein, szProteinName);
         fprintf(fpout, "    <alternative_protein protein=\"%s%s\"/>\n", g_staticParams.szDecoyPrefix, szProteinName);

         iPrintDuplicateProteinCt++;
         if (iPrintDuplicateProteinCt == g_staticParams.options.iMaxDuplicateProteins)
            break;
      }
   }

   // check if peptide is modified
   bool bModified = 0;

   if (!isEqual(g_staticParams.staticModifications.dAddNterminusPeptide, 0.0)
         || !isEqual(g_staticParams.staticModifications.dAddCterminusPeptide, 0.0))
      bModified = 1;

   if (pOutput[iWhichResult].szPrevNextAA[0]=='-' && !isEqual(g_staticParams.staticModifications.dAddNterminusProtein, 0.0))
      bModified = 1;
   if (pOutput[iWhichResult].szPrevNextAA[1]=='-' && !isEqual(g_staticParams.staticModifications.dAddCterminusProtein, 0.0))
      bModified = 1;

   if (pOutput[iWhichResult].cPeffOrigResidue != '\0' && pOutput[iWhichResult].iPeffOrigResiduePosition != -9)
      bModified = 1;

   if (!bModified)
   {
      for (i=0; i<pOutput[iWhichResult].iLenPeptide; i++)
      {
         if (!isEqual(g_staticParams.staticModifications.pdStaticMods[(int)pOutput[iWhichResult].szPeptide[i]], 0.0)
               || pOutput[iWhichResult].piVarModSites[i] != 0)
         {
            bModified = 1;
            break;
         }
      }

      // check n- and c-terminal variable mods
      i=pOutput[iWhichResult].iLenPeptide;
      if (pOutput[iWhichResult].piVarModSites[i] != 0  || pOutput[iWhichResult].piVarModSites[i+1] != 0)
         bModified = 1;
   }

   if (bModified)
   {
      // construct modified peptide string
      char szModPep[512];

      szModPep[0]='\0';

      bool bNterm = false;
      bool bNtermVariable = false;
      bool bCterm = false;
      bool bCtermVariable = false;
      double dNterm = 0.0;
      double dCterm = 0.0;

      // See if n-term mod (static and/or variable) needs to be reported
      if (pOutput[iWhichResult].piVarModSites[pOutput[iWhichResult].iLenPeptide] > 0
            || !isEqual(g_staticParams.staticModifications.dAddNterminusPeptide, 0.0)
            || (pOutput[iWhichResult].szPrevNextAA[0]=='-'
               && !isEqual(g_staticParams.staticModifications.dAddNterminusProtein, 0.0)) )
      {
         bNterm = true;

         // pepXML format reports modified term mass (vs. mass diff)
         dNterm = g_staticParams.precalcMasses.dNtermProton - PROTON_MASS + g_staticParams.massUtility.pdAAMassFragment[(int)'h'];

         if (pOutput[iWhichResult].piVarModSites[pOutput[iWhichResult].iLenPeptide] > 0)
         {
            dNterm += g_staticParams.variableModParameters.varModList[(int)pOutput[iWhichResult].piVarModSites[pOutput[iWhichResult].iLenPeptide]-1].dVarModMass;
            bNtermVariable = true;
         }

         if (pOutput[iWhichResult].szPrevNextAA[0]=='-' && !isEqual(g_staticParams.staticModifications.dAddNterminusProtein, 0.0))
            dNterm += g_staticParams.staticModifications.dAddNterminusProtein;
      }

      // See if c-term mod (static and/or variable) needs to be reported
      if (pOutput[iWhichResult].piVarModSites[pOutput[iWhichResult].iLenPeptide+1] > 0
            || !isEqual(g_staticParams.staticModifications.dAddCterminusPeptide, 0.0)
            || (pOutput[iWhichResult].szPrevNextAA[1]=='-'
               && !isEqual(g_staticParams.staticModifications.dAddCterminusProtein, 0.0)) )
      {
         bCterm = true;

         dCterm = g_staticParams.precalcMasses.dCtermOH2Proton - PROTON_MASS - g_staticParams.massUtility.pdAAMassFragment[(int)'h'];

         if (pOutput[iWhichResult].piVarModSites[pOutput[iWhichResult].iLenPeptide+1] > 0)
         {
            dCterm += g_staticParams.variableModParameters.varModList[(int)pOutput[iWhichResult].piVarModSites[pOutput[iWhichResult].iLenPeptide+1]-1].dVarModMass;
            bCtermVariable = true;
         }

         if (pOutput[iWhichResult].szPrevNextAA[1]=='-' && !isEqual(g_staticParams.staticModifications.dAddCterminusProtein, 0.0))
            dCterm += g_staticParams.staticModifications.dAddCterminusProtein;
      }

      // generate modified_peptide string
      if (bNtermVariable)
         sprintf(szModPep+strlen(szModPep), "n[%0.0f]", dNterm);
      for (i=0; i<pOutput[iWhichResult].iLenPeptide; i++)
      {
         sprintf(szModPep+strlen(szModPep), "%c", pOutput[iWhichResult].szPeptide[i]);

         if (pOutput[iWhichResult].piVarModSites[i] != 0)
         {
            sprintf(szModPep+strlen(szModPep), "[%0.0f]",
                  pOutput[iWhichResult].pdVarModSites[i] + g_staticParams.massUtility.pdAAMassFragment[(int)pOutput[iWhichResult].szPeptide[i]]);
         }
      }
      if (bCtermVariable)
         sprintf(szModPep+strlen(szModPep), "c[%0.0f]", dCterm);

      fprintf(fpout, "    <modification_info modified_peptide=\"%s\"", szModPep);
      if (bNterm)
         fprintf(fpout, " mod_nterm_mass=\"%0.6f\"", dNterm);
      if (bCterm)
         fprintf(fpout, " mod_cterm_mass=\"%0.6f\"", dCterm);
      fprintf(fpout, ">\n");

      for (i=0; i<pOutput[iWhichResult].iLenPeptide; i++)
      {
         if (!isEqual(g_staticParams.staticModifications.pdStaticMods[(int)pOutput[iWhichResult].szPeptide[i]], 0.0)
               || pOutput[iWhichResult].piVarModSites[i] != 0)
         {
            int iResidue = (int)pOutput[iWhichResult].szPeptide[i];
            double dStaticMass = g_staticParams.staticModifications.pdStaticMods[iResidue];

            fprintf(fpout, "     <mod_aminoacid_mass position=\"%d\" mass=\"%0.6f\"",
                  i+1,
                  g_staticParams.massUtility.pdAAMassFragment[iResidue] + pOutput[iWhichResult].pdVarModSites[i]);
            
            if (!isEqual(dStaticMass, 0.0))
               fprintf(fpout, " static=\"%0.6f\"", dStaticMass);

            if (pOutput[iWhichResult].piVarModSites[i] != 0)
               fprintf(fpout, " variable=\"%0.6f\"", pOutput[iWhichResult].pdVarModSites[i]);

            if (pOutput[iWhichResult].piVarModSites[i] < 0)
            {
               fprintf(fpout, " source=\"peff\" id=\"%s\"/>\n", pOutput[iWhichResult].pszMod[i]);
            }
            else if (pOutput[iWhichResult].piVarModSites[i] > 0)
               fprintf(fpout, " source=\"param\"/>\n");
            else
               fprintf(fpout, "/>\n");
         }
      }

      // Report PEFF substitution
      if (pOutput[iWhichResult].cPeffOrigResidue != '\0' && pOutput[iWhichResult].iPeffOrigResiduePosition != -9)
      {
         if (pOutput[iWhichResult].iPeffOrigResiduePosition == -1)
         {
            fprintf(fpout, "     <aminoacid_substitution peptide_prev_aa=\"%c\" orig_aa=\"%c\"/>\n",
                  pOutput[iWhichResult].szPrevNextAA[0], pOutput[iWhichResult].cPeffOrigResidue);
         }
         else if (pOutput[iWhichResult].iPeffOrigResiduePosition == pOutput[iWhichResult].iLenPeptide)
         {
            fprintf(fpout, "     <aminoacid_substitution peptide_next_aa=\"%c\" orig_aa=\"%c\"/>\n",
                  pOutput[iWhichResult].szPrevNextAA[1], pOutput[iWhichResult].cPeffOrigResidue);
         }
         else
         {
            fprintf(fpout, "     <aminoacid_substitution position=\"%d\" orig_aa=\"%c\"/>\n",
                  pOutput[iWhichResult].iPeffOrigResiduePosition+1, pOutput[iWhichResult].cPeffOrigResidue);
         }
      }


      fprintf(fpout, "    </modification_info>\n");
   }

   fprintf(fpout, "    <search_score name=\"xcorr\" value=\"%0.3f\"/>\n", pOutput[iWhichResult].fXcorr);

   fprintf(fpout, "    <search_score name=\"deltacn\" value=\"%0.3f\"/>\n", dDeltaCn);
   fprintf(fpout, "    <search_score name=\"deltacnstar\" value=\"%0.3f\"/>\n", dDeltaCnStar);

   fprintf(fpout, "    <search_score name=\"spscore\" value=\"%0.1f\"/>\n", pOutput[iWhichResult].fScoreSp);
   fprintf(fpout, "    <search_score name=\"sprank\" value=\"%d\"/>\n", pOutput[iWhichResult].iRankSp);
   fprintf(fpout, "    <search_score name=\"expect\" value=\"%0.2E\"/>\n", pOutput[iWhichResult].dExpect);
   fprintf(fpout, "   </search_hit>\n");
}


void CometWriteMzIdentML::ReadInstrument(char *szManufacturer,
                                         char *szModel)
{
   strcpy(szManufacturer, "UNKNOWN");
   strcpy(szModel, "UNKNOWN");

   if (g_staticParams.inputFile.iInputType == InputType_MZXML)
   {
      FILE *fp;

      if ((fp = fopen(g_staticParams.inputFile.szFileName, "r")) != NULL)
      {
         char szMsInstrumentElement[SIZE_BUF];
         char szBuf[SIZE_BUF];

         szMsInstrumentElement[0]='\0';
         while (fgets(szBuf, SIZE_BUF, fp))
         {
            if (strstr(szBuf, "<scan") || strstr(szBuf, "mslevel"))
               break;

            // Grab entire msInstrument element.
            if (strstr(szBuf, "<msInstrument"))
            {
               strcat(szMsInstrumentElement, szBuf);

               while (fgets(szBuf, SIZE_BUF, fp))
               {
                  if (strlen(szMsInstrumentElement)+strlen(szBuf)<8192)
                     strcat(szMsInstrumentElement, szBuf);
                  if (strstr(szBuf, "</msInstrument>"))
                  {
                     GetVal(szMsInstrumentElement, "\"msModel\" value", szModel);
                     GetVal(szMsInstrumentElement, "\"msManufacturer\" value", szManufacturer);
                     break;
                  }
               }
            }
         }

         fclose(fp);
      }
   }
}


void CometWriteMzIdentML::GetVal(char *szElement,
                                 char *szAttribute,
                                 char *szAttributeVal)
{
   char *pStr;

   if ((pStr=strstr(szElement, szAttribute)))
   {
      strncpy(szAttributeVal, pStr+strlen(szAttribute)+2, SIZE_FILE);  // +2 to skip ="
      szAttributeVal[SIZE_FILE-1] = '\0';

      if ((pStr=strchr(szAttributeVal, '"')))
      {
         *pStr='\0';
         return;
      }
      else
      {
         strcpy(szAttributeVal, "unknown");  // Error - expecting an end quote in szAttributeVal.
         return;
      }
   }
   else
   {
      strcpy(szAttributeVal, "unknown"); // Attribute not found.
      return;
   }
}


void CometWriteMzIdentML::CalcNTTNMC(Results *pOutput,
                                     int iWhichResult,
                                     int *iNTT,
                                     int *iNMC)
{
   int i;
   *iNTT=0;
   *iNMC=0;

   // Calculate number of tolerable termini (NTT) based on sample_enzyme
   if (pOutput[iWhichResult].szPrevNextAA[0]=='-')
   {
      *iNTT += 1;
   }
   else if (g_staticParams.enzymeInformation.iSampleEnzymeOffSet == 1)
   {
      if (strchr(g_staticParams.enzymeInformation.szSampleEnzymeBreakAA, pOutput[iWhichResult].szPrevNextAA[0])
            && !strchr(g_staticParams.enzymeInformation.szSampleEnzymeNoBreakAA, pOutput[iWhichResult].szPeptide[0]))
      {
         *iNTT += 1;
      }
   }
   else
   {
      if (strchr(g_staticParams.enzymeInformation.szSampleEnzymeBreakAA, pOutput[iWhichResult].szPeptide[0])
            && !strchr(g_staticParams.enzymeInformation.szSampleEnzymeNoBreakAA, pOutput[iWhichResult].szPrevNextAA[0]))
      {
         *iNTT += 1;
      }
   }

   if (pOutput[iWhichResult].szPrevNextAA[1]=='-')
   {
      *iNTT += 1;
   }
   else if (g_staticParams.enzymeInformation.iSampleEnzymeOffSet == 1)
   {
      if (strchr(g_staticParams.enzymeInformation.szSampleEnzymeBreakAA, pOutput[iWhichResult].szPeptide[pOutput[iWhichResult].iLenPeptide -1])
            && !strchr(g_staticParams.enzymeInformation.szSampleEnzymeNoBreakAA, pOutput[iWhichResult].szPrevNextAA[1]))
      {
         *iNTT += 1;
      }
   }
   else
   {
      if (strchr(g_staticParams.enzymeInformation.szSampleEnzymeBreakAA, pOutput[iWhichResult].szPrevNextAA[1])
            && !strchr(g_staticParams.enzymeInformation.szSampleEnzymeNoBreakAA, pOutput[iWhichResult].szPeptide[pOutput[iWhichResult].iLenPeptide -1]))
      {
         *iNTT += 1;
      }
   }

   // Calculate number of missed cleavage (NMC) sites based on sample_enzyme
   if (g_staticParams.enzymeInformation.iSampleEnzymeOffSet == 1)
   {
      for (i=0; i<pOutput[iWhichResult].iLenPeptide-1; i++)
      {
         if (strchr(g_staticParams.enzymeInformation.szSampleEnzymeBreakAA, pOutput[iWhichResult].szPeptide[i])
               && !strchr(g_staticParams.enzymeInformation.szSampleEnzymeNoBreakAA, pOutput[iWhichResult].szPeptide[i+1]))
         {
            *iNMC += 1;
         }
      }
   }
   else
   {
      for (i=1; i<pOutput[iWhichResult].iLenPeptide; i++)
      {
         if (strchr(g_staticParams.enzymeInformation.szSampleEnzymeBreakAA, pOutput[iWhichResult].szPeptide[i])
               && !strchr(g_staticParams.enzymeInformation.szSampleEnzymeNoBreakAA, pOutput[iWhichResult].szPeptide[i-1]))
         {
            *iNMC += 1;
         }
      }
   }

}
