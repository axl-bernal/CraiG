/*****************************************************************************
 * This is the main of the genes2tags program. It converts gene locations into
 * tags, which will be later used within the lless library. The structure of
 * each gene is specified with the --species option
 *
 *   Copyright (C) 2002-2007  Axel E. Bernal (abernal@seas.upenn.edu)
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License as
 *   published by the Free Software Foundation; either version 2 of the
 *   License, or (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *   General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *   02111-1307, USA.
 *
 *   The GNU General Public License is contained in the file COPYING.
 *
 ****************************************************************************/



#include <string.h>
#include <math.h>
#include "Utils.h"
#include "ContextIMM.h"
#include <stdlib.h>
#include <iostream>
#include <iomanip>
#include "Sequence.h"
#include "SequenceUtils.h"
#include "ResourceEngine.h"
#include "FeatureEngine.h"
#include "GeneUtils.h"
#include "FSM.h"
#include "TagUtils.h"
#include "InpFile.h"
#include "ArgParseUtils.h"
#include "IntervalSet.h"
#include "GeneEvaluator.h"
#include "GeneTagPrinter.h"
#include "Lattice.h"
#include "StructureCore.h"
#define NUM_PHASES 3

void printHelp(const char * pname, ::ofstream &fd) {

    fd << "CRAIG v. " << CRAIG_VERSION << " tool for converting gene locations into tags.\n";
    fd << "Written by Axel E. Bernal (" << AUTHOR_EMAIL << ")\n\n";
    fd << "Usage : " << pname << " [options] PARAMS_FILE FASTA_FILE ANNOT_FILE > ANNOT_FILE\n\n";
    ArgParseUtils::displayOption(fd, "PARAMS_FILE", "Name of the file containing the gene model parameters. If PARAMS_FILE is not found, craig assumes the filename to be $(CRAIG_HOME)/models/PARAMS_FILE");
    ArgParseUtils::displayOption(fd, "FASTA_FILE", "Name of the file containing the input query sequence(s) in fasta format");
    ArgParseUtils::displayOption(fd,  "ANNOT_FILE", "Name of the file containing genes in locs format");
    fd << "optional arguments:\n";
    ArgParseUtils::displayOption(fd, "--version", "Print version name and license information");
    ArgParseUtils::displayOption(fd, "-h --help", "Show this message and exit");
    ArgParseUtils::displayOption(fd, "-v --verbose", "Turns on output of debugging information");
    ArgParseUtils::displayOption(fd, "-c --complete", "Allows for prediction of complete genes only. Incomplete genes are predicted by default");
    ArgParseUtils::displayOption(fd, "-utr --have-utrs", "Genes have UTR regions If not specified, make sure tags used as training do not contain UTR-labeled regions");
    ArgParseUtils::displayOption(fd, "-m --masked", "Treat the input sequences as if they had been  previously masked. When this option is specified, lowercase letters MUST be used to represent the masked-out regions");
    ArgParseUtils::displayOption(fd, "--output=OUTPUT_FILE", "Specifies output file. If not given, the default is the standard output");
    ArgParseUtils::displayOption(fd, "--signal-resource=RESOURCE_NAME", "Read signal locations from resource RESOURCE_NAME. Here, signal is either START, STOP, DONOR, ACCEPTOR, TSS or PAS");
    ArgParseUtils::displayOption(fd, "--prefix-evidence=PREFIX_EVIDENCE", "prefix for fetching files used as input resources");
    ArgParseUtils::displayOption(fd, "ANNOT_FILE", "Formatted List of genes and scores");
    fd << "Report bugs to <" << AUTHOR_EMAIL << ">\n";
}

bool verbose = false;

int main(int argc, char *argv[]) {
    std::string paramFile("");
    std::string prefix_evidence(""), outputFile("");
std:string seqFileName = "", annotFile = "";
    bool haveUTRs = false;
    std::string topology = "partial";
    int origStrand = BOTH_STRANDS;
    bool masked = false;
    int i, num_sigfilters = 4;
    string format("locs");

    std::string **sig_info = new std::string* [3];
    for(i = 0; i < 3; i++)
        sig_info[i] = new std::string [6];

    sig_info[0][0] = "Start-Signal"; sig_info[0][1] = "Stop-Signal";
    sig_info[0][2] = "Donor-Signal"; sig_info[0][3] = "Acceptor-Signal";
    sig_info[0][4] = "TSS-Signal"; sig_info[0][5] =  "PAS-Signal";
    sig_info[1][0] = "S"; sig_info[1][1] = "T"; sig_info[1][2] = "D";
    sig_info[1][3] = "A"; sig_info[1][4] = "X"; sig_info[1][5] = "Z";
    sig_info[2][0] = ""; sig_info[2][1] = ""; sig_info[2][2] = "";
    sig_info[2][3] = ""; sig_info[2][4] = ""; sig_info[2][5] = "";

    boost::RegEx rExSigRes("^\\-\\-(\\S+)\\-resource=(\\S+)$");

    try {

        if(!getenv("CRAIG_HOME"))
            throw EXCEPTION(NOT_ANNOTATED,
                            "CRAIG_HOME must be initialized!. See README for details");

        for(i = 1; i < argc; i++) {
            if(!strncmp(argv[i], "--complete", 10)
               || !strncmp(argv[i], "-c", 2)) {
                topology = std::string("complete");
            }
            else if(!strncmp(argv[i], "--verbose", 9)
                    || !strncmp(argv[i], "-v", 2)) {
                verbose = true;
            }
            else if(!strncmp(argv[i], "--help", 6)
                    || !strncmp(argv[i], "-h", 2)) {
                printHelp("genes2tags", (std::ofstream &)cout);
                exit(0);
            }
            else if(!strncmp(argv[i], "--version", 9)) {
                PRINT_VERSION(cerr, "score_genes", "tool for scoring gene structures");
                PRINT_DISCLAIMER(cerr, "score_genes");
                exit(0);
            }
            else if(!strncmp(argv[i], "--prefix-evidence=", 18)) {
                prefix_evidence = string(argv[i] + 18);
            }
            else if(!strncmp(argv[i], "--output=", 9)) {
                outputFile = string(argv[i] + 9);
            }
            else if(!strncmp(argv[i], "-utr", 4) ||
                    !strncmp(argv[i], "--have-utrs", 12)) {
                haveUTRs = true;
            }
            else if(!strncmp(argv[i], "--masked", 8)
                    || !strncmp(argv[i], "-m", 2)) {
                masked = true;
            }
            else if(rExSigRes.Match(argv[i])) {
                sig_info[2][TypeDefs::stringToTEdgeId2(rExSigRes[1])] = rExSigRes[2];
            }
            else
                break;
        }

        if(argc - i < 3)
            throw EXCEPTION(BAD_USAGE, "insufficient arguments");

        if(haveUTRs)
            num_sigfilters = 6;

        paramFile = std::string(argv[i++]);
        seqFileName = std::string(argv[i++]);
        annotFile = std::string(argv[i++]);

        ::ifstream *paramStream,  ifstr1(paramFile.c_str()), ifstr2;

        paramStream = &ifstr1;

        if(!ifstr1.is_open()) {
            if(!getenv("CRAIG_HOME"))
                throw EXCEPTION(NOT_ANNOTATED,
                                "CRAIG_HOME must be initialized!. See README for details");

            paramFile = std::string(getenv("CRAIG_HOME")) + "/models/" + paramFile;
            ifstr2.open(paramFile.c_str());

            if(!ifstr2.is_open())
                throw EXCEPTION(FILE_UNAVAILABLE, paramFile);

            paramStream = &ifstr2;
        }

        map<std::string, std::string> resource_subs;
        resource_subs["PREFIX_EVIDENCE"] = prefix_evidence;
        ResourceEngine re(*paramStream, &resource_subs);
        Sigma *sigma = (Sigma *)re.getResource(std::string("dna-alpha"));
        FSM & fsm = *(FSM *)re.getResource(topology);
        fsm.setParseStrand((TStrand)origStrand);

        if(!haveUTRs) {
            fsm.removeId2Node(ANY_5UTR);
            fsm.removeId2Node(ANY_3UTR);
            fsm.removeId2Node(ANY_UTR_INTRON);
        }

        GeneEvaluator evaluator(fsm);
        GeneTagPrinter printer;

        FilterEngine fe(re, *paramStream, sig_info, num_sigfilters);
        FeatureEngine fte(fsm, fe, *paramStream);

        GlobalVector params(fte.getFeatures(), paramStream);
        fte.setParamVector(&params);

        //loading annotSeqs and gene annotations
        InpFile seqFile("default", seqFileName, FASTA, sigma, true);
        list<Sequence *> & annotSeqs = (list<Sequence *> &)seqFile.sequences();

        list<Gene> geneSet;
        GeneUtils::loadGenes(geneSet, annotFile.c_str(), false, false, annotSeqs, sigma);

        // Initializing special variables
        TypedFilter<EdgeInst> **signals = new TypedFilter<EdgeInst> * [NUM_EDGE_INSTS];
        for(i = 0; i < NUM_EDGE_INSTS; i++)
            signals[i] = (TypedFilter<EdgeInst> *)fe.findFilter(sig_info[0][i]);

        TypedFilter<UCHAR> *contexts = (TypedFilter<UCHAR> *)fe.findFilter("GC-Content");
        FT_HiddenSeq *vftHidden = (FT_HiddenSeq *)fte.findFeature("Hidden-Stop-Sequence");

        Lattice lattice(fe, fsm, NUM_PHASES, evaluator, MAX_NUM_EXONS,
                        INTERGENIC, signals, contexts, vftHidden);

        if(!masked) {
            Feature *maskFeature = fte.findFeature("Test-Is-Masked");
            if(maskFeature)
                maskFeature->turnOff();
        }

        // Creating StructureCore object
        StructureCore core(fsm, lattice, re, fe, fte,
                           evaluator, printer,
                           AVG_ALL, COMB_EUCLID, (TStrand)origStrand, 1);

        cerr << "\nProcessing Fasta Sequences[PID=" << getpid() << "]...\n";
        list<Sequence *>::iterator cit = annotSeqs.begin();

        std::ofstream tmpStream(outputFile.c_str()), *outputStream = &(std::ofstream &)cout;
        if(tmpStream.is_open())
            outputStream = &tmpStream;

        for( ; cit != annotSeqs.end(); cit++) {
            Sequence &c = *(*cit);
            core.prePSequence(c);
            IntervalSet<double> tag_scores;
            tag_scores.set_empty_key(pair<int,int>(0,0));
            //      GlobalVector *gv = core.computeGlobalVector(c.getTags()[0]);
            //      double score = params*((const GlobalVector &)*gv);

            GeneUtils::annotSeq2Tags(c.getTags(), &fsm, contexts, signals, c);
            double score = lattice.dotModelParams(c.getTags()[0], fte, &tag_scores);
            printer.displayGenes(format, *outputStream,
                                 (list<Gene *> &)c.getAnnotBioFeats(),
                                 0, INT_MAX, &tag_scores);

            fte.updFeatureCacheParams();
            core.postPSequence(true);

        }

        if(tmpStream.is_open())
            tmpStream.close();

        paramStream->close();

        // releasing tags
        SeqTags::releaseMem();
        delete [] signals;

        for(int i = 0; i < 3; i++)
            delete [] sig_info[i];
        delete [] sig_info;

        cerr << "Done\n";

    } catch(exception *e) {
        perror(e->what());

        GenLibExcept *gle = (GenLibExcept *)e;
        if(gle->error() == BAD_USAGE)
            cerr << "use --help for more information\n";

        delete e;
        exit(1);

    }
}
