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
#include "CometWritePepXML.h"

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
                                         char *szTmpFile,
                                         CometSearchManager &searchMgr)
{
   WriteMzIdentMLHeader(fpout);

   // now loop through szTmpFile file, wr
   ParseTmpFile(fpout, fpdb, szTmpFile, searchMgr);

   fprintf(fpout, "</MzIdentML>\n");
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
   CometWritePepXML::ReadInstrument(szManufacturer, szModel);

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
   fprintf(fpout, "  <cv id=\"PSI-MS\" uri=\"https://raw.githubusercontent.com/HUPO-PSI/psi-ms-CV/master/psi-ms.obo\" fullName=\"PSI-MS\" />\n");
   fprintf(fpout, "  <cv id=\"UNIMOD\" uri=\"http://www.unimod.org/obo/unimod.obo\" fullName=\"UNIMOD\" />\n");
   fprintf(fpout, "  <cv id=\"UO\" uri=\"https://raw.githubusercontent.com/bio-ontology-research-group/unit-ontology/master/unit.obo\" fullName=\"UNIT-ONTOLOGY\" />\n");
   fprintf(fpout, "  <cv id=\"PRIDE\" uri=\"https://github.com/PRIDE-Utilities/pride-ontology/blob/master/pride_cv.obo\" fullName=\"PRIDE\" />\n");
   fprintf(fpout, " </cvList>\n");


   fprintf(fpout, " <AnalysisSoftwareList>\n");
   fprintf(fpout, "  <AnalysisSoftware id=\"Comet\" name=\"Comet\" version=\"%s\">\n", comet_version);
   fprintf(fpout, "   <SoftwareName><cvParam cvRef=\"MS\" accession=\"MS:1002251\" name=\"Comet\" value=\"\" /></SoftwareName>\n");
   fprintf(fpout, "  </AnalysisSoftware>\n");
   fprintf(fpout, " </AnalysisSoftwareList>\n");

   fflush(fpout);

   return true;
}


bool CometWriteMzIdentML::ParseTmpFile(FILE *fpout,
                                       FILE *fpdb,
                                       char *szTmpFile,
                                       CometSearchManager &searchMgr)
{
   std::vector<MzidTmpStruct> vMzidTmp; // vector to store entire tmp output
   std::vector<long> vProteinTargets;   // store vector of target protein file offsets
   std::vector<long> vProteinDecoys;    // store vector of decoy protein file offsets
   std::vector<string> vstrPeptides;          // vector of peptides of format "QITQMSNSSDLADGLNFDEGDELLK;2:79.9969;4:15.9949;"
   std::vector<string> vstrPeptideEvidence;   // vector of peptides + target&decoy prots, space delmited "QITQMSNSSDLADGLNFDEGDELLK 1;38;75;112; 149;185;221;257;"

   fprintf(fpout, " <SequenceCollection xmlns=\"http://psidev.info/psi/pi/mzIdentML/1.2\">\n");

   // get all protein file positions by parsing through fpout_tmp
   // column 15 is target proteins, column 16 is decoy proteins

   std::ifstream ifsTmpFile(szTmpFile);

   if (!ifsTmpFile.is_open())
   {
      printf(" Error cannot read tmp file \"%s\"\n", szTmpFile);  // FIX
      exit(1);
   }

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
               Stmp.iRankSp = std::stoi(field);
               break;
            case 10:
               Stmp.iMatchedIons = std::stoi(field);
               break;
            case 11:
               Stmp.iTotalIons = std::stoi(field);
               break;
            case 12:
               Stmp.strPeptide = field;
               break;
            case 13:
               Stmp.cPrevNext[0] = field.at(0);
               Stmp.cPrevNext[1] = field.at(1);
               break;
            case 14:
               Stmp.strMods = field;
               break;
            case 15:
               Stmp.strProtsTarget = field;
               break;
            case 16:
               Stmp.strProtsDecoy = field;
               break;
            case 17:
               Stmp.iWhichQuery = std::stoi(field);
               break;
            case 18:
               Stmp.iWhichResult = std::stoi(field);
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

      // vstrPeptideEvidence contains "peptide delimtedtargetprots delimiteddecoyprots"
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
      if (*it >= 0)
      {
         CometMassSpecUtils::GetProteinName(fpdb, *it, szProteinName);
         fprintf(fpout, " <DBSequence id=\"%s\" accession=\"%s\" searchDatabase_ref=\"DB0\" />\n", szProteinName, szProteinName);
      }
   }
   for (it = vProteinDecoys.begin(); it != vProteinDecoys.end(); it++)
   {
      if (*it >= 0)
      {
         CometMassSpecUtils::GetProteinName(fpdb, *it, szProteinName);
         fprintf(fpout, " <DBSequence id=\"%s%s\" accession=\"%s%s\" searchDatabase_ref=\"DB0\" />\n",
               g_staticParams.szDecoyPrefix, szProteinName, g_staticParams.szDecoyPrefix, szProteinName);
      }
   }

   vProteinTargets.clear();
   vProteinDecoys.clear();

   // print Peptide element
   std::vector<string>::iterator it2;
   int iLen;
   for (it2 = vstrPeptides.begin(); it2 != vstrPeptides.end(); it2++)
   {
      std::istringstream isString(*it2);

      fprintf(fpout, " <Peptide id=\"%s\">\n", (*it2).c_str());  // Note: id is "peptide;mod-string"

      std::getline(isString, strLocal, ';');
      fprintf(fpout, "  <PeptideSequence>%s</PeptideSequence>\n", strLocal.c_str());
      iLen = (int)strLocal.length();

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
                  std::istringstream isTargets(strLocal);
                  std::string strOffset;

                  // Now parse out individual target entries (file offsets) delimited by ";"
                  while ( std::getline(isTargets, strOffset, ';') )
                  {
                     long lOffset = stol(strOffset);

                     if (lOffset >= 0)
                     {
                        CometMassSpecUtils::GetProteinName(fpdb, lOffset, szProteinName);

                        fprintf(fpout, " <PeptideEvidence id=\"%s;%s\" isDecoy=\"%s\" dBSequence_Ref=\"%s\" />\n",
                              strPeptide.c_str(),
                              szProteinName,
                              "false",
                              szProteinName);
                     }
                  }
               }

               break;
            case 2:
               if (strLocal.length() > 0)
               {
                  std::istringstream isDecoys(strLocal);
                  std::string strOffset;

                  // Now parse out individual decoy entries (file offsets) delimited by ";"
                  while ( std::getline(isDecoys, strOffset, ';') )
                  {
                     long lOffset = stol(strOffset);
                     
                     if (lOffset >= 0)
                     {
                        CometMassSpecUtils::GetProteinName(fpdb, lOffset, szProteinName);

                        fprintf(fpout, " <PeptideEvidence id=\"%s;%s%s\" isDecoy=\"%s\" dBSequence_Ref=\"%s%s\" />\n",
                              strPeptide.c_str(),
                              g_staticParams.szDecoyPrefix,
                              szProteinName,
                              "true",
                              g_staticParams.szDecoyPrefix,
                              szProteinName);
                     }
                  }
               }
               break;
         }
         vProteinTargets.push_back(atol(strLocal.c_str()));

         n++;
      }
   }

   fprintf(fpout, " </SequenceCollection>\n");

   fprintf(fpout, " <AnalysisCollection>\n");
   fprintf(fpout, "  <SpectrumIdentification spectrumIdentificationList_ref=\"SIL\" spectrumIdentificationProtocol_ref=\"SIP\" id=\"SI\">\n");
   fprintf(fpout, "   <InputSpectra spectraData_ref=\"SD1\" />\n");
   fprintf(fpout, "   <SearchDatabaseRef searchDatabase_ref=\"DB0\" />\n");
   fprintf(fpout, "  </SpectrumIdentification>\n");
   fprintf(fpout, "  <ProteinDetection proteinDetectionProtocol_ref=\"Comet\" proteinDetectionList_ref=\"PDL\" id=\"PD\">\n");
   fprintf(fpout, "   <InputSpectrumIdentifications spectrumIdentificationList_ref=\"SIL\" />\n");
   fprintf(fpout, "  </ProteinDetection>\n");
   fprintf(fpout, " </AnalysisCollection>\n");

   fprintf(fpout, " <AnalysisProtocolCollection>\n");
   fprintf(fpout, "  <SpectrumIdentificationProtocol analysisSoftware_ref=\"Comet\" id=\"SIP\">\n");
   fprintf(fpout, "   <SearchType>\n");
   fprintf(fpout, "    <cvParam cvRef=\"PSI-MS\" accession=\"MS:1001083\" name=\"ms-ms search\" />\n");
   fprintf(fpout, "   </SearchType>\n");

   fprintf(fpout, "   <AdditionalSearchParams>\n");
   if (g_staticParams.massUtility.bMonoMassesParent)
      fprintf(fpout, "    <cvParam cvRef=\"PSI-MS\" accession=\"MS:1001211\" name=\"parent mass type mono\" />\n");
   else
      fprintf(fpout, "    <cvParam cvRef=\"PSI-MS\" accession=\"MS:1001212\" name=\"parent mass type average\" />\n");

   if (g_staticParams.massUtility.bMonoMassesFragment)
      fprintf(fpout, "    <cvParam cvRef=\"PSI-MS\" accession=\"MS:1001256\" name=\"fragment mass type mono\" />\n");
   else
      fprintf(fpout, "    <cvParam cvRef=\"PSI-MS\" accession=\"MS:1001255\" name=\"fragment mass type average\" />\n");

   // write out all Comet parameters
   std::map<std::string, CometParam*> mapParams = searchMgr.GetParamsMap();
   for (std::map<std::string, CometParam*>::iterator it=mapParams.begin(); it!=mapParams.end(); ++it)
   {
      if (it->first != "[COMET_ENZYME_INFO]")
      {
         fprintf(fpout, "    <userParam name=\"%s\" value=\"%s\" />\n", it->first.c_str(), it->second->GetStringValue().c_str());
      }
   }
   fprintf(fpout, "   </AdditionalSearchParams>\n");

   WriteMods(fpout, searchMgr);
   WriteEnzyme(fpout);
   WriteMassTable(fpout);
   WriteTolerance(fpout);

   fprintf(fpout, "  </SpectrumIdentificationProtocol>\n");
   fprintf(fpout, " </AnalysisProtocolCollection>\n");

   fprintf(fpout, " <DataCollection>\"\n");

   WriteInputs(fpout);

   fprintf(fpout, "  <AnalysisData>\n");

   WriteSpectrumIdentificationList(fpout, fpdb, &vMzidTmp);

   fprintf(fpout, "  </AnalysisData>\n");
   fprintf(fpout, " </DataCollection>\n");

   return true;
}


void CometWriteMzIdentML::WriteMods(FILE *fpout,
                                    CometSearchManager &searchMgr)
{
   string strModID;
   string strModRef;
   string strModName;

   fprintf(fpout, "   <ModificationParams>\n");

   WriteStaticMod(fpout, searchMgr, "add_G_glycine");
   WriteStaticMod(fpout, searchMgr, "add_A_alanine");
   WriteStaticMod(fpout, searchMgr, "add_S_serine");
   WriteStaticMod(fpout, searchMgr, "add_P_proline");
   WriteStaticMod(fpout, searchMgr, "add_V_valine");
   WriteStaticMod(fpout, searchMgr, "add_T_threonine");
   WriteStaticMod(fpout, searchMgr, "add_C_cysteine");
   WriteStaticMod(fpout, searchMgr, "add_L_leucine");
   WriteStaticMod(fpout, searchMgr, "add_I_isoleucine");
   WriteStaticMod(fpout, searchMgr, "add_N_asparagine");
   WriteStaticMod(fpout, searchMgr, "add_O_ornithine");
   WriteStaticMod(fpout, searchMgr, "add_D_aspartic_acid");
   WriteStaticMod(fpout, searchMgr, "add_Q_glutamine");
   WriteStaticMod(fpout, searchMgr, "add_K_lysine");
   WriteStaticMod(fpout, searchMgr, "add_E_glutamic_acid");
   WriteStaticMod(fpout, searchMgr, "add_M_methionine");
   WriteStaticMod(fpout, searchMgr, "add_H_histidine");
   WriteStaticMod(fpout, searchMgr, "add_F_phenylalanine");
   WriteStaticMod(fpout, searchMgr, "add_R_arginine");
   WriteStaticMod(fpout, searchMgr, "add_Y_tyrosine");
   WriteStaticMod(fpout, searchMgr, "add_W_tryptophan");
   WriteStaticMod(fpout, searchMgr, "add_B_user_amino_acid");
   WriteStaticMod(fpout, searchMgr, "add_J_user_amino_acid");
   WriteStaticMod(fpout, searchMgr, "add_U_user_amino_acid");
   WriteStaticMod(fpout, searchMgr, "add_X_user_amino_acid");
   WriteStaticMod(fpout, searchMgr, "add_Z_user_amino_acid");

   double dMass = 0.0;
   if (searchMgr.GetParamValue("add_Cterm_peptide", dMass))
   {
      if (!isEqual(dMass, 0.0))
      {
         double dMassDiff = g_staticParams.precalcMasses.dCtermOH2Proton - PROTON_MASS - g_staticParams.massUtility.pdAAMassFragment[(int)'h'];

         fprintf(fpout, "  <SearchModification fixedMod=\"true\" massDelta=\"%0.6f\" residues=\".\">\n", dMassDiff);
         fprintf(fpout, "   <SpecificityRules>\n");
         fprintf(fpout, "     <cvParam cvRef=\"PSI-MS\" accession=\"MS:1001190\" name=\"modification specificity peptide C-term\" />\n");
         fprintf(fpout, "   </SpecificityRules>\n");

         GetModificationID('.', dMassDiff, &strModID, &strModRef, &strModName);
         fprintf(fpout, "   <cvParam cvRef=\"%s\" accession=\"UNIMOD:%s\" name=\"%s\" />\n",
               strModRef.c_str(), strModID.c_str(), strModName.c_str());

         fprintf(fpout, "  </SearchModification>\n");
      }
   }

   dMass = 0.0;
   if (searchMgr.GetParamValue("add_Nterm_peptide", dMass))
   {
      if (!isEqual(dMass, 0.0))
      {
         double dMassDiff = g_staticParams.precalcMasses.dNtermProton - PROTON_MASS - g_staticParams.massUtility.pdAAMassFragment[(int)'h'];

         fprintf(fpout, "  <SearchModification fixedMod=\"true\" massDelta=\"%0.6f\" residues=\".\">\n", dMassDiff);
         fprintf(fpout, "   <SpecificityRules>\n");
         fprintf(fpout, "     <cvParam cvRef=\"PSI-MS\" accession=\"MS:1001189\" name=\"modification specificity peptide N-term\" />\n");
         fprintf(fpout, "   </SpecificityRules>\n");

         GetModificationID('.', dMassDiff, &strModID, &strModRef, &strModName);
         fprintf(fpout, "   <cvParam cvRef=\"%s\" accession=\"UNIMOD:%s\" name=\"%s\" />\n",
               strModRef.c_str(), strModID.c_str(), strModName.c_str());

         fprintf(fpout, "  </SearchModification>\n");
      }
   }

   dMass = 0.0;
   if (searchMgr.GetParamValue("add_Cterm_protein", dMass))
   {
      if (!isEqual(dMass, 0.0))
      {
         double dMassDiff = g_staticParams.precalcMasses.dCtermOH2Proton - PROTON_MASS - g_staticParams.massUtility.pdAAMassFragment[(int)'h'];

         fprintf(fpout, "  <SearchModification fixedMod=\"true\" massDelta=\"%0.6f\" residues=\".\">\n", dMassDiff);
         fprintf(fpout, "   <SpecificityRules>\n");
         fprintf(fpout, "     <cvParam cvRef=\"PSI-MS\" accession=\"MS:1002058\" name=\"modification specificity protein C-term\" />\n");
         fprintf(fpout, "   </SpecificityRules>\n");

         GetModificationID('.', dMassDiff, &strModID, &strModRef, &strModName);
         fprintf(fpout, "   <cvParam cvRef=\"%s\" accession=\"UNIMOD:%s\" name=\"%s\" />\n",
               strModRef.c_str(), strModID.c_str(), strModName.c_str());

         fprintf(fpout, "  </SearchModification>\n");
      }
   }

   dMass = 0.0;
   if (searchMgr.GetParamValue("add_Nterm_protein", dMass))
   {
      if (!isEqual(dMass, 0.0))
      {
         double dMassDiff = g_staticParams.precalcMasses.dNtermProton - PROTON_MASS - g_staticParams.massUtility.pdAAMassFragment[(int)'h'];

         fprintf(fpout, "  <SearchModification fixedMod=\"true\" massDelta=\"%0.6f\" residues=\".\">\n", dMassDiff);
         fprintf(fpout, "   <SpecificityRules>\n");
         fprintf(fpout, "     <cvParam cvRef=\"PSI-MS\" accession=\"MS:1002057\" name=\"modification specificity protein N-term\" />\n");
         fprintf(fpout, "   </SpecificityRules>\n");

         GetModificationID('.', dMassDiff, &strModID, &strModRef, &strModName);
         fprintf(fpout, "   <cvParam cvRef=\"%s\" accession=\"UNIMOD:%s\" name=\"%s\" />\n",
               strModRef.c_str(), strModID.c_str(), strModName.c_str());

         fprintf(fpout, "  </SearchModification>\n");
      }
   }

   // Write out properly encoded mods
   WriteVariableMod(fpout, searchMgr, "variable_mod01", 0); // this writes aminoacid_modification
   WriteVariableMod(fpout, searchMgr, "variable_mod02", 0);
   WriteVariableMod(fpout, searchMgr, "variable_mod03", 0);
   WriteVariableMod(fpout, searchMgr, "variable_mod04", 0);
   WriteVariableMod(fpout, searchMgr, "variable_mod05", 0);
   WriteVariableMod(fpout, searchMgr, "variable_mod06", 0);
   WriteVariableMod(fpout, searchMgr, "variable_mod07", 0);
   WriteVariableMod(fpout, searchMgr, "variable_mod08", 0);
   WriteVariableMod(fpout, searchMgr, "variable_mod09", 0);
   WriteVariableMod(fpout, searchMgr, "variable_mod01", 1);  // this writes terminal_modification
   WriteVariableMod(fpout, searchMgr, "variable_mod02", 1);  // which has to come after aminoaicd_modification
   WriteVariableMod(fpout, searchMgr, "variable_mod03", 1);
   WriteVariableMod(fpout, searchMgr, "variable_mod04", 1);
   WriteVariableMod(fpout, searchMgr, "variable_mod05", 1);
   WriteVariableMod(fpout, searchMgr, "variable_mod06", 1);
   WriteVariableMod(fpout, searchMgr, "variable_mod07", 1);
   WriteVariableMod(fpout, searchMgr, "variable_mod08", 1);
   WriteVariableMod(fpout, searchMgr, "variable_mod09", 1);

   fprintf(fpout, "   </ModificationParams>\n");
}


void CometWriteMzIdentML::WriteStaticMod(FILE *fpout,
                                         CometSearchManager &searchMgr,
                                         string paramName)
{
   double dMass = 0.0;
   if (searchMgr.GetParamValue(paramName, dMass))
   {
      if (!isEqual(dMass, 0.0))
      {
         fprintf(fpout, "    <SearchModification residues=\"%c\" massDelta=\"%0.6f\" fixedMod= \"true\" >\n",
               paramName[4], dMass);

         string strModID;
         string strModRef;
         string strModName; 

         GetModificationID(paramName[4], dMass, &strModID, &strModRef, &strModName);

         fprintf(fpout, "     <cvParam cvRef=\"%s\" accession=\"UNIMOD:%s\" name=\"%s\" />\n",
               strModRef.c_str(), strModID.c_str(), strModName.c_str());
         fprintf(fpout, "    </SearchModification>\n");
      }
   }
}


void CometWriteMzIdentML::GetModificationID(char cResidue,
                                            double dModMass,
                                            string *strModID,
                                            string *strModRef,
                                            string *strModName)
{
   *strModID = "MS:1001460";
   *strModRef = "PSI-MS";
   *strModName = "unknown modification";

   if (fabs(dModMass - 15.994915) < 0.01)
   {
      *strModID = "UNIMOD:35";
      *strModRef = "UNIMOD";
      *strModName = "Oxidation";
   }
   else if (fabs(dModMass - 79.966331) < 0.01)
   {
      *strModID = "UNIMOD:21";
      *strModRef = "UNIMOD";
      *strModName = "Phospho";
   }
   else if (fabs(dModMass - 42.010565) < 0.01)
   {
      *strModID = "UNIMOD:1";
      *strModRef = "UNIMOD";
      *strModName = "Acetyl";
   }
   else if (fabs(dModMass - 226.077598) < 0.01)
   {
      *strModID = "UNIMOD:3";
      *strModRef = "UNIMOD";
      *strModName = "Biotin";
   }
   else if (fabs(dModMass - 57.021464) < 0.01)
   {
      *strModID = "UNIMOD:4";
      *strModRef = "UNIMOD";
      *strModName = "Carbamidomethyl";
   }
   else if (fabs(dModMass - 58.005479) < 0.01)
   {
      *strModID = "UNIMOD:6";
      *strModRef = "UNIMOD";
      *strModName = "Carboxymethyl";
   }
   else if (fabs(dModMass - 0.984016) < 0.01)
   {
      *strModID = "UNIMOD:7";
      *strModRef = "UNIMOD";
      *strModName = "Deamidated";
   }
   else if (fabs(dModMass - -18.010565) < 0.01)
   {
      if (cResidue == 'E')
      {
         *strModID = "UNIMOD:27";
         *strModRef = "UNIMOD";
         *strModName = "Glu->pyro-Glu";
      }
      else
      {
         *strModID = "UNIMOD:23";
         *strModRef = "UNIMOD";
         *strModName = "Dehydrated";
      }
   }
   else if (fabs(dModMass - -17.026549) < 0.01)
   {
      if (cResidue == 'Q')
      {
         *strModID = "UNIMOD:28";
         *strModRef = "UNIMOD";
         *strModName = "Gln->pyro-Glu";
      }
      else
      {
         *strModID = "UNIMOD:385";
         *strModRef = "UNIMOD";
         *strModName = "Ammonia-loss";
      }
   }
   else if (fabs(dModMass - 14.01565) < 0.01)
   {
      *strModID = "UNIMOD:34";
      *strModRef = "UNIMOD";
      *strModName = "Methyl";
   }
   else if (fabs(dModMass - 114.042927) < 0.01)
   {
      *strModID = "UNIMOD:121";
      *strModRef = "UNIMOD";
      *strModName = "GG";
   }
   else if (fabs(dModMass - 229.162932) < 0.01)
   {
      *strModID = "UNIMOD:737";
      *strModRef = "UNIMOD";
      *strModName = "TMT6plex";
   }
   else if (fabs(dModMass - 224.152478) < 0.01)
   {
      *strModID = "UNIMOD:739";
      *strModRef = "UNIMOD";
      *strModName = "TMT";
   }
   else if (fabs(dModMass - 304.207146) < 0.01)
   {
      *strModID = "UNIMOD:2016";
      *strModRef = "UNIMOD";
      *strModName = "TMTpro";
   }
}
                                            

void CometWriteMzIdentML::WriteVariableMod(FILE *fpout,
                                           CometSearchManager &searchMgr,
                                           string varModName,
                                           bool bWriteTerminalMods)
{
   VarMods varModsParam;

   if (searchMgr.GetParamValue(varModName, varModsParam))
   {
/*
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
*/
      if (!isEqual(varModsParam.dVarModMass, 0.0))
      {
         int iLen = (int)strlen(varModsParam.szVarModChar);

         for (int i=0; i<iLen; i++)
         {
            string strModID;
            string strModRef;
            string strModName;

            GetModificationID(varModsParam.szVarModChar[i], varModsParam.dVarModMass, &strModID, &strModRef, &strModName);

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
                     fprintf(fpout, "    <SearchModification residues=\".\" massDelta=\"%0.6f\" fixedMod= \"false\" >\n", varModsParam.dVarModMass);

                     fprintf(fpout, "     <SpecificityRules>\n");
                     fprintf(fpout, "       <cvParam accession=\"MS:1002057\" cvRef=\"PSI-MS\" name=\"modification specificity protein N-term\" />\n");
                     fprintf(fpout, "     </SpecificityRules>\n");
         
                     fprintf(fpout, "     <cvParam cvRef=\"%s\" accession=\"UNIMOD:%s\" name=\"%s\" />\n",
                           strModRef.c_str(), strModID.c_str(), strModName.c_str());
                     fprintf(fpout, "    </SearchModification>\n");
                  }
                  // print this if non-protein N-term variable mod
                  else
                  {
                     fprintf(fpout, "    <SearchModification residues=\".\" massDelta=\"%0.6f\" fixedMod= \"false\" >\n", varModsParam.dVarModMass);

                     fprintf(fpout, "     <SpecificityRules>\n");
                     fprintf(fpout, "       <cvParam accession=\"MS:1001189\" cvRef=\"PSI-MS\" name=\"modification specificity peptide N-term\" />\n");
                     fprintf(fpout, "     </SpecificityRules>\n");
         
                     fprintf(fpout, "     <cvParam cvRef=\"%s\" accession=\"UNIMOD:%s\" name=\"%s\" />\n",
                           strModRef.c_str(), strModID.c_str(), strModName.c_str());
                     fprintf(fpout, "    </SearchModification>\n");
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
                     fprintf(fpout, "    <SearchModification residues=\".\" massDelta=\"%0.6f\" fixedMod= \"false\" >\n", varModsParam.dVarModMass);

                     fprintf(fpout, "     <SpecificityRules>\n");
                     fprintf(fpout, "       <cvParam accession=\"MS:1002058\" cvRef=\"PSI-MS\" name=\"modification specificity protein C-term\" />\n");
                     fprintf(fpout, "     </SpecificityRules>\n");
         
                     fprintf(fpout, "     <cvParam cvRef=\"%s\" accession=\"UNIMOD:%s\" name=\"%s\" />\n",
                           strModRef.c_str(), strModID.c_str(), strModName.c_str());
                     fprintf(fpout, "    </SearchModification>\n");
                  }
                  // print this if non-protein C-term variable mod
                  else
                  {
                     fprintf(fpout, "    <SearchModification residues=\".\" massDelta=\"%0.6f\" fixedMod= \"false\" >\n", varModsParam.dVarModMass);

                     fprintf(fpout, "     <SpecificityRules>\n");
                     fprintf(fpout, "       <cvParam accession=\"MS:1001190\" cvRef=\"PSI-MS\" name=\"modification specificity peptide C-term\" />\n");
                     fprintf(fpout, "     </SpecificityRules>\n");
         
                     fprintf(fpout, "     <cvParam cvRef=\"%s\" accession=\"UNIMOD:%s\" name=\"%s\" />\n",
                           strModRef.c_str(), strModID.c_str(), strModName.c_str());
                     fprintf(fpout, "    </SearchModification>\n");
                  }
               }
            }
            else if (!bWriteTerminalMods && varModsParam.szVarModChar[i]!='c' && varModsParam.szVarModChar[i]!='n')
            {
               fprintf(fpout, "    <SearchModification residues=\"%c\" massDelta=\"%0.6f\" fixedMod= \"false\" >\n",
                     varModsParam.szVarModChar[i],
                     varModsParam.dVarModMass);
     
               fprintf(fpout, "     <cvParam cvRef=\"%s\" accession=\"UNIMOD:%s\" name=\"%s\" />\n",
                     strModRef.c_str(), strModID.c_str(), strModName.c_str());
               fprintf(fpout, "    </SearchModification>\n");
            }
         }
      }
   }
}


void CometWriteMzIdentML::WriteEnzyme(FILE *fpout)
{
   fprintf(fpout, "   <Enzymes>\n");

   char szSemi[8];
   char szRegExpression[128];
   if (g_staticParams.options.iEnzymeTermini == ENZYME_DOUBLE_TERMINI)
      strcpy(szSemi, "false");
   else
      strcpy(szSemi, "true");

   // regular expression examples
   // Trypsin (?<=[KR])(?!P)
   // Trypsin/P (?<=[KR])
   // Chymotrypsin (?<=[FYWL])(?!P)
   // CNBr (?<=M)
   // Asp-N (?=[BD])
   // Lys-N (?=K)

   //search enzyme 1
   if (g_staticParams.enzymeInformation.iSearchEnzymeOffSet == 1)
   {
      sprintf(szRegExpression, "(?&lt;=[%s])", g_staticParams.enzymeInformation.szSearchEnzymeBreakAA);
      if (g_staticParams.enzymeInformation.szSearchEnzymeNoBreakAA[0] != '-')
         sprintf(szRegExpression+strlen(szRegExpression), " (?!%s)", g_staticParams.enzymeInformation.szSearchEnzymeNoBreakAA);
   }
   else
   {
      szRegExpression[0] = '\0';
      if (g_staticParams.enzymeInformation.szSearchEnzymeNoBreakAA[0] != '-')
         sprintf(szRegExpression, "(?&lt;=[%s]) ", g_staticParams.enzymeInformation.szSearchEnzymeNoBreakAA);
      sprintf(szRegExpression+strlen(szRegExpression), "(?=[%s])", g_staticParams.enzymeInformation.szSearchEnzymeBreakAA);
   }

   fprintf(fpout, "     <Enzyme id=\"ENZ\"  missedCleavages=\"%d\" semiSpecific=\"%s\">\n", 
         g_staticParams.enzymeInformation.iAllowedMissedCleavage, szSemi);
   fprintf(fpout, "      <SiteRegexp>(%s)</SiteRegexp>\n", szRegExpression);
   fprintf(fpout, "     </Enzyme>\n");

   //search enzyme 2
   if (!g_staticParams.enzymeInformation.bNoEnzyme2Selected)
   {
      if (g_staticParams.enzymeInformation.iSearchEnzyme2OffSet == 1)
      {
         sprintf(szRegExpression, "(?&lt;=[%s])", g_staticParams.enzymeInformation.szSearchEnzyme2BreakAA);
         if (g_staticParams.enzymeInformation.szSearchEnzyme2NoBreakAA[0] != '-')
            sprintf(szRegExpression+strlen(szRegExpression), " (?!%s)", g_staticParams.enzymeInformation.szSearchEnzyme2NoBreakAA);
      }
      else
      {
         szRegExpression[0] = '\0';
         if (g_staticParams.enzymeInformation.szSearchEnzyme2NoBreakAA[0] != '-')
            sprintf(szRegExpression, "(?&lt;=[%s])", g_staticParams.enzymeInformation.szSearchEnzyme2BreakAA);
         sprintf(szRegExpression, "(?=[%s])", g_staticParams.enzymeInformation.szSearchEnzyme2BreakAA);
      }

      fprintf(fpout, "     <Enzyme id=\"ENZ2\"  missedCleavages=\"%d\" semiSpecific=\"%s\">\n", 
            g_staticParams.enzymeInformation.iAllowedMissedCleavage, szSemi);
      fprintf(fpout, "      <SiteRegexp>(%s)</SiteRegexp>\n", szRegExpression);
      fprintf(fpout, "     </Enzyme>\n");
   }
  
   fprintf(fpout, "   </Enzymes>\n");
}


void CometWriteMzIdentML::WriteMassTable(FILE *fpout)
{
   if (g_staticParams.massUtility.bMonoMassesParent == g_staticParams.massUtility.bMonoMassesFragment)
   {
      fprintf(fpout, "   <MassTable id=\"MT\" msLevel=\"1 %d\">\n", g_staticParams.options.iMSLevel);
      for (int i = 65; i <= 90; i++)   // 64=A, 90=Z
      {
         if (g_staticParams.massUtility.pdAAMassFragment[i] < 999998.0)
         {
            fprintf(fpout, "    <Residue code=\"%c\" mass=\"%lf\" />\n",
                  i, g_staticParams.massUtility.pdAAMassFragment[i] - g_staticParams.staticModifications.pdStaticMods[i]);
         }
      }
      fprintf(fpout, "   </MassTable>\n");
   }
   else
   {
      fprintf(fpout, "   <MassTable id=\"MT\" msLevel=\"1\">\n");
      for (int i = 65; i <= 90; i++)   // 64=A, 90=Z
      {
         if (g_staticParams.massUtility.pdAAMassParent[i] < 999998.0)
         {
            fprintf(fpout, "    <Residue code=\"%c\" mass=\"%lf\" />\n",
                  i, g_staticParams.massUtility.pdAAMassParent[i] - g_staticParams.staticModifications.pdStaticMods[i]);
         }
      }
      fprintf(fpout, "   </MassTable>\n");

      fprintf(fpout, "   <MassTable id=\"MT2\" msLevel=\"%d\">\n", g_staticParams.options.iMSLevel);
      for (int i = 65; i <= 90; i++)   // 64=A, 90=Z
      {
         if (g_staticParams.massUtility.pdAAMassFragment[i] < 999998.0)
         {
            fprintf(fpout, "    <Residue code=\"%c\" mass=\"%lf\" />\n",
                  i, g_staticParams.massUtility.pdAAMassFragment[i] - g_staticParams.staticModifications.pdStaticMods[i]);
         }
      }
      fprintf(fpout, "   </MassTable>\n");
   }
}


void CometWriteMzIdentML::WriteTolerance(FILE *fpout)
{
   // bin size does not translate to Da or PPM so will use Da
   fprintf(fpout, "   <FragmentTolerance>\n");
   fprintf(fpout, "    <cvParam accession=\"MS:1001412\" name=\"search tolerance plus value\" value=\"%lf\" cvRef=\"PSI-MS\" unitAccession=\"UO:0000221\" unitName=\"dalton\" unitCvRef=\"UO\" />\n",
         g_staticParams.tolerances.dFragmentBinSize / 2.0);
   fprintf(fpout, "    <cvParam accession=\"MS:1001413\" name=\"search tolerance minus value\" value=\"%lf\" cvRef=\"PSI-MS\" unitAccession=\"UO:0000221\" unitName=\"dalton\" unitCvRef=\"UO\" />\n",
         g_staticParams.tolerances.dFragmentBinSize / 2.0);
   fprintf(fpout, "   </FragmentTolerance>\n");


   // not sure how isotope error is handled
   fprintf(fpout, "   <ParentTolerance>\n");

   if (g_staticParams.tolerances.iMassToleranceUnits == 0)
   {
      fprintf(fpout, "    <cvParam accession=\"MS:1001412\" name=\"search tolerance plus value\" value=\"%lf\" cvRef=\"PSI-MS\" unitAccession=\"UO:0000221\" unitName=\"dalton\" unitCvRef=\"UO\" />\n", g_staticParams.tolerances.dInputTolerance);
      fprintf(fpout, "    <cvParam accession=\"MS:1001413\" name=\"search tolerance minus value\" value=\"%lf\" cvRef=\"PSI-MS\" unitAccession=\"UO:0000221\" unitName=\"dalton\" unitCvRef=\"UO\" />\n", g_staticParams.tolerances.dInputTolerance);
   }
   else if (g_staticParams.tolerances.iMassToleranceUnits == 2)
   {
      fprintf(fpout, "   <cvParam cvRef=\"PSI-MS\" accession=\"MS:1001412\" name=\"search tolerance plus value\" value=\"%lf\" unitAccession=\"UO:0000169\" unitName=\"parts per million\" unitCvRef=\"UO\"></cvParam>\n", g_staticParams.tolerances.dInputTolerance);
      fprintf(fpout, "   <cvParam cvRef=\"PSI-MS\" accession=\"MS:1001413\" name=\"search tolerance minus value\" value=\"%lf\" unitAccession=\"UO:0000169\" unitName=\"parts per million\" unitCvRef=\"UO\"></cvParam>\n", g_staticParams.tolerances.dInputTolerance);
   }
   else  // invalid
   {
      fprintf(fpout, "   <cvParam cvRef=\"PSI-MS\" accession=\"MS:1001412\" name=\"search tolerance plus value\" value=\"%lf\"></cvParam>\n", g_staticParams.tolerances.dInputTolerance);
      fprintf(fpout, "   <cvParam cvRef=\"PSI-MS\" accession=\"MS:1001413\" name=\"search tolerance minus value\" value=\"%lf\"></cvParam>\n", g_staticParams.tolerances.dInputTolerance);
   }

   fprintf(fpout, "   </ParentTolerance>\n");

   fprintf(fpout, "   <Threshold>\n");
   fprintf(fpout, "    <cvParam cvRef=\"MS\" accession=\"MS:1001494\" name=\"no threshold\" value=\"\"/>\n");
   fprintf(fpout, "   </Threshold>\n");
}


void CometWriteMzIdentML::WriteInputs(FILE *fpout)
{
   fprintf(fpout, "  <Inputs>\n");
   fprintf(fpout, "   <SearchDatabase id=\"DB0\" location=\"%s\">\n", g_staticParams.databaseInfo.szDatabase);
   fprintf(fpout, "    <FileFormat>\n");
   fprintf(fpout, "     <cvParam cvRef=\"PSI-MS\" accession=\"MS:1001348\" name=\"FASTA format\" />\n");
   fprintf(fpout, "    </FileFormat>\n");
   fprintf(fpout, "    <DatabaseName>\n");

   char *pStr;
   char szNoPathDatabase[512];
   char cSep;
#ifdef _WIN32
   cSep = '\\';
#else
   cSep = '/';
#endif
   if ( (pStr = strrchr(g_staticParams.databaseInfo.szDatabase, cSep)) != NULL)
      strcpy(szNoPathDatabase, pStr);
   else
      strcpy(szNoPathDatabase, g_staticParams.databaseInfo.szDatabase);

   fprintf(fpout, "     <userParam name=\"%s\" />\n", szNoPathDatabase);
   fprintf(fpout, "    </DatabaseName>\n");
   fprintf(fpout, "    <cvParam cvRef=\"MS\" accession=\"MS:1001073\" name=\"database type amino acid\" value=\"\" />\n");
   if (g_staticParams.options.iDecoySearch == 1)
   {
      fprintf(fpout, "    <cvParam accession=\"MS:XXXXXXX\" cvRef=\"PSI-MS\" name=\"Comet internal target+decoy\" />\n");
      fprintf(fpout, "    <cvParam accession=\"MS:1001283\" cvRef=\"PSI-MS\" value=\"^%s\" name=\"decoy DB accession regexp\" />\n", g_staticParams.szDecoyPrefix);
      fprintf(fpout, "    <cvParam accession=\"MS:XXXXXXX\" cvRef=\"PSI-MS\" name=\"Comet internal reverse peptide\" />\n");
   }
   else if (g_staticParams.options.iDecoySearch == 2)
   {
      fprintf(fpout, "    <cvParam accession=\"MS:XXXXXXX\" cvRef=\"PSI-MS\" name=\"Comet internal separate target-decoy\" />\n");
      fprintf(fpout, "    <cvParam accession=\"MS:1001283\" cvRef=\"PSI-MS\" value=\"^%s\" name=\"decoy DB accession regexp\" />\n", g_staticParams.szDecoyPrefix);
      fprintf(fpout, "    <cvParam accession=\"MS:XXXXXXX\" cvRef=\"PSI-MS\" name=\"Comet internal reverse peptide\" />\n");
   }
   fprintf(fpout, "   </SearchDatabase>\n");

   fprintf(fpout, "   <SpectraData location=\"%s\" id=\"SD_1\" >\n", g_staticParams.inputFile.szFileName);
   fprintf(fpout, "    <FileFormat>\n");

   char szFormatAccession[24];
   char szFormatName[128];
   char szSpectrumAccession[24];
   char szSpectrumName[128];
   int iLen = (int)strlen(g_staticParams.inputFile.szFileName);
   char szFileNameLower[SIZE_FILE];

   for (int x = 0; x < iLen; x++)
      szFileNameLower[x] = tolower(g_staticParams.inputFile.szFileName[x]);
   szFileNameLower[iLen] = '\0';

   if (!strcmp(szFileNameLower + iLen - 4, ".raw"))
   {
      strcpy(szFormatAccession, "MS:1000563"); // Thermo RAW
      strcpy(szFormatName, "Thermo RAW file");

      strcpy(szSpectrumAccession, "MS:1000776");
      strcpy(szSpectrumName, "scan number only nativeID format");
   }
   else if (!strcmp(szFileNameLower + iLen - 6, ".mzxml")
         || !strcmp(szFileNameLower + iLen - 9, ".mzxml.gz"))
   {
      strcpy (szFormatAccession, "MS:1000566");  // mzXML
      strcpy(szFormatName, "ISB mzXML file");

      strcpy(szSpectrumAccession, "MS:1000776");
      strcpy(szSpectrumName, "scan number only nativeID format");
   }
   else if (!strcmp(szFileNameLower + iLen - 5, ".mzml")
         || !strcmp(szFileNameLower + iLen - 8, ".mzml.gz"))
   {
      strcpy (szFormatAccession, "MS:1000584");  // mzML
      strcpy(szFormatName, "mzML file");

      strcpy(szSpectrumAccession, "MS:1001530");
      strcpy(szSpectrumName, "mzML unique identifier");
   }
   else if (!strcmp(szFileNameLower + iLen - 4, ".ms2"))
   {
      strcpy (szFormatAccession, "MS:1001466");  // ms2
      strcpy(szFormatName, "MS2 file");

      strcpy(szSpectrumAccession, "MS:1000776");
      strcpy(szSpectrumName, "scan number only nativeID format");
   }
   else if (!strcmp(szFileNameLower + iLen - 4, ".mgf"))
   {
      strcpy (szFormatAccession, "MS:1001062");  // mgf
      strcpy(szFormatName, "Mascot MGF file");

      strcpy(szSpectrumAccession, "MS:1000776");
      strcpy(szSpectrumName, "scan number only nativeID format");
   }
   else
   {
      strcpy (szFormatAccession, "error");
      strcpy(szFormatName, "error");

      strcpy(szSpectrumAccession, "error");
      strcpy(szSpectrumName, "error");
   }

   fprintf(fpout, "     <cvParam cvRef=\"PSI-MS\" accession=\"%s\" name=\"%s\" />\n", szFormatAccession, szFormatName);
   fprintf(fpout, "    </FileFormat>\n");
   fprintf(fpout, "    <SpectrumIDFormat>\n");
   fprintf(fpout, "     <cvParam cvRef=\"PSI-MS\" accession=\"%s\" name=\"%s\" />\n", szSpectrumAccession, szSpectrumName);
   fprintf(fpout, "    </SpectrumIDFormat>\n");
   fprintf(fpout, "   </SpectraData>\n");
   fprintf(fpout, "  </Inputs>\n");
}


void CometWriteMzIdentML::WriteSpectrumIdentificationList(FILE* fpout,
      FILE *fpdb,
      vector<MzidTmpStruct>* vMzid)
{
   fprintf(fpout, "   <SpectrumIdentificationList id=\"SIL_1\">\n");

   long lCount = 1;

/*
   int    iScanNumber;
   int    iXcorrRank;
   int    iCharge;
   double dExpMass;   // neutral experimental mass
   double dCalcMass;  // neutral calculated mass
   double dExpect;
   float  fXcorr;
   float  fCn;
   float  fSp;
   int    iRankSp;
   string strPeptide;
   char   cPrevNext[3];
   string strMods;
   string strProtsTarget;   // delimited list of file offsets
   string strProtsDecoy;    // delimited list of file offsets
*/

   for (std::vector<MzidTmpStruct>::iterator itMzid = (*vMzid).begin(); itMzid < (*vMzid).end(); itMzid++)
   {
      char szProteinName[512];
      char szTmp[WIDTH_REFERENCE];
      long lOffset;

      fprintf(fpout, "    <SpectrumIdentificationResult id=\"SIR_%d\" spectrumID=\"scan=%d\" spectraData_ref=\"SD0\">\n", (*itMzid).iWhichQuery, (*itMzid).iScanNumber);
      fprintf(fpout, "     <SpectrumIdentificationItem id=\"SII_%d\" rank=\"%d\" chargeState=\"%d\" peptide_ref=\"%s;%s\" experimentalMassToCharge=\"%f\" calculatedMassToCharge=\"%f\" passThreshold=\"true\">\n",
            (*itMzid).iWhichResult,
            (*itMzid).iWhichResult,
            (*itMzid).iCharge,
            (*itMzid).strPeptide.c_str(),
            (*itMzid).strMods.c_str(),
            ((*itMzid).dExpMass + (*itMzid).iCharge * PROTON_MASS) / (*itMzid).iCharge,
            ((*itMzid).dCalcMass + (*itMzid).iCharge * PROTON_MASS) / (*itMzid).iCharge);

      if ((*itMzid).strProtsTarget.size() > 0)
      {
         // walk through semi-colon delimited list to get each protein file offset
         std::string field;
         std::istringstream isString((*itMzid).strProtsTarget);

         while ( std::getline(isString, field, ';') )
         {
            lOffset = stol(field);

            if (lOffset >= 0)
            {
               comet_fseek(fpdb, lOffset, SEEK_SET);
               fread(szTmp, sizeof(char)*WIDTH_REFERENCE, 1, fpdb);
               sscanf(szTmp, "%511s", szProteinName);  // WIDTH_REFERENCE-1

               fprintf(fpout, "      <PeptideEvidenceRef peptideEvidence_ref=\"%s;%s\" />\n", (*itMzid).strPeptide.c_str(), szProteinName);
            }
         }
      }
      if ((*itMzid).strProtsDecoy.size() > 0)
      {
         // walk through semi-colon delimited list to get each protein file offset
         std::string field;
         std::istringstream isString((*itMzid).strProtsDecoy);

         while ( std::getline(isString, field, ';') )
         {
            lOffset = stol(field);

            if (lOffset >= 0)
            {
               comet_fseek(fpdb, lOffset, SEEK_SET);
               fread(szTmp, sizeof(char)*WIDTH_REFERENCE, 1, fpdb);
               sscanf(szTmp, "%511s", szProteinName);  // WIDTH_REFERENCE-1

               fprintf(fpout, "      <PeptideEvidenceRef peptideEvidence_ref=\"%s;%s%s\" />\n", (*itMzid).strPeptide.c_str(), g_staticParams.szDecoyPrefix, szProteinName);
            }
         }
      }

      fprintf(fpout, "      <cvParam cvRef=\"MS\" accession=\"MS:1001121\" name=\"number of matched peaks\" value=\"%d\" />\n", (*itMzid).iMatchedIons);
      fprintf(fpout, "      <cvParam cvRef=\"MS\" accession=\"MS:1001362\" name=\"number of unmatched peaks\" value=\"%d\" />\n", (*itMzid).iTotalIons - (*itMzid).iMatchedIons);
      fprintf(fpout, "      <cvParam cvRef=\"MS\" accession=\"MS:1002252\" name=\"Comet:xcorr\" value=\"%0.4f\" />\n", (*itMzid).fXcorr);
      fprintf(fpout, "      <cvParam cvRef=\"MS\" accession=\"MS:1002253\" name=\"Comet:deltacn\" value=\"%0.4f\" />\n", (*itMzid).fCn);
      fprintf(fpout, "      <cvParam cvRef=\"MS\" accession=\"MS:1002255\" name=\"Comet:spscore\" value=\"%0.4f\" />\n", (*itMzid).fSp);
      fprintf(fpout, "      <cvParam cvRef=\"MS\" accession=\"MS:1002256\" name=\"Comet:sprank\" value=\"%d\" />\n", (*itMzid).iRankSp);
      fprintf(fpout, "      <cvParam cvRef=\"MS\" accession=\"MS:1002257\" name=\"Comet:expectation value\" value=\"%0.2E\" />\n", (*itMzid).dExpect);
      fprintf(fpout, "     </SpectrumIdentificationItem>\n");
      fprintf(fpout, "    </SpectrumIdentificationResult>\n");

      lCount++;
   }

   fprintf(fpout, "   </SpectrumIdentificationList>\n");
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

               if (!g_staticParams.options.bExplicitDeltaCn)
               {
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
               }

               // calculate deltaCn only if sequences are less than 0.75 similar
               if (g_staticParams.options.bExplicitDeltaCn || ((double) (iMinLength - iDiffCt)/iMinLength) < 0.75)
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
         fprintf(fpout, "%d\t", pOutput[iWhichResult].iRankSp);
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

         // semicolon separated list of fpdb pointers for target proteins
         std::vector<ProteinEntryStruct>::iterator it;
         if (pOutput[iWhichResult].pWhichProtein.size() > 0)
         {
            for (it=pOutput[iWhichResult].pWhichProtein.begin(); it!=pOutput[iWhichResult].pWhichProtein.end(); ++it)
            {
#ifdef _WIN32
               fprintf(fpout, "%I64d;", (*it).lWhichProtein);
#else
               fprintf(fpout, "%ld;", (*it).lWhichProtein);
#endif
            }
            fprintf(fpout, "\t");
         }
         else
         {
            fprintf(fpout, "-1;\t");
         }

         // semicolon separated list of fpdb pointers for decoy proteins
         if (pOutput[iWhichResult].pWhichDecoyProtein.size() > 0)
         {
            for (it=pOutput[iWhichResult].pWhichDecoyProtein.begin(); it!=pOutput[iWhichResult].pWhichDecoyProtein.end(); ++it)
            {
#ifdef _WIN32
               fprintf(fpout, "%I64d;", (*it).lWhichProtein);
#else
               fprintf(fpout, "%ld;", (*it).lWhichProtein);
#endif
            }
            fprintf(fpout, "\t");
         }
         else
         {
            fprintf(fpout, "-2;\t");
         }

         fprintf(fpout, "%d\t%d", iWhichQuery, iWhichResult);

 
         fprintf(fpout, "\n");
      }
   }
}
