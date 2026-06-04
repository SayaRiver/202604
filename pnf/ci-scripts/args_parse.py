# SPDX-License-Identifier: LicenseRef-CSSL-1.0

#---------------------------------------------------------------------
# Python for CI of OAI-eNB + COTS-UE
#
#   Required Python Version
#     Python 3.x
#
#   Required Python Package
#     pexpect
#---------------------------------------------------------------------

#-----------------------------------------------------------
# Import Libs
#-----------------------------------------------------------
import sys		# arg
import re		# reg
import constants as CONST

#-----------------------------------------------------------
# Parsing Command Line Arguements
#-----------------------------------------------------------


def ArgsParse(argvs,CiTestObj,RAN,HTML,CONTAINERS,HELP,CLUSTER):

    force_local = False
    date_fmt = None
    while len(argvs) > 1:
        myArgv = argvs.pop(1)	# 0th is this file's name

	    #--help
        if re.match(r'^\-\-help$', myArgv, re.IGNORECASE):
            HELP.GenericHelp(CONST.Version)
            sys.exit(0)
        if re.match(r'^\-\-local$', myArgv, re.IGNORECASE):
            force_local = True


	    #consider inline parameters
        elif re.match(r'^\-\-datefmt=(.+)$', myArgv, re.IGNORECASE):
            matchReg = re.match(r'^\-\-datefmt=(.+)$', myArgv, re.IGNORECASE)
            date_fmt = matchReg.group(1)
        elif re.match(r'^\-\-mode=(.+)$', myArgv, re.IGNORECASE):
            matchReg = re.match(r'^\-\-mode=(.+)$', myArgv, re.IGNORECASE)
            mode = matchReg.group(1)
        elif re.match(r'^\-\-eNBRepository=(.+)$|^\-\-ranRepository(.+)$', myArgv, re.IGNORECASE):
            if re.match(r'^\-\-eNBRepository=(.+)$', myArgv, re.IGNORECASE):
                matchReg = re.match(r'^\-\-eNBRepository=(.+)$', myArgv, re.IGNORECASE)
            else:
                matchReg = re.match(r'^\-\-ranRepository=(.+)$', myArgv, re.IGNORECASE)
            CiTestObj.ranRepository = matchReg.group(1)
            RAN.ranRepository=matchReg.group(1)
            HTML.ranRepository=matchReg.group(1)
            CONTAINERS.ranRepository=matchReg.group(1)
            CLUSTER.ranRepository=matchReg.group(1)
        elif re.match(r'^\-\-eNB_AllowMerge=(.+)$|^\-\-ranAllowMerge=(.+)$', myArgv, re.IGNORECASE):
            if re.match(r'^\-\-eNB_AllowMerge=(.+)$', myArgv, re.IGNORECASE):
                matchReg = re.match(r'^\-\-eNB_AllowMerge=(.+)$', myArgv, re.IGNORECASE)
            else:
                matchReg = re.match(r'^\-\-ranAllowMerge=(.+)$', myArgv, re.IGNORECASE)
            doMerge = matchReg.group(1)
            if ((doMerge == 'true') or (doMerge == 'True')):
                CiTestObj.ranAllowMerge = True
                RAN.ranAllowMerge=True
                HTML.ranAllowMerge=True
                CONTAINERS.ranAllowMerge=True
                CLUSTER.ranAllowMerge=True
        elif re.match(r'^\-\-eNBBranch=(.+)$|^\-\-ranBranch=(.+)$', myArgv, re.IGNORECASE):
            if re.match(r'^\-\-eNBBranch=(.+)$', myArgv, re.IGNORECASE):
                matchReg = re.match(r'^\-\-eNBBranch=(.+)$', myArgv, re.IGNORECASE)
            else:
                matchReg = re.match(r'^\-\-ranBranch=(.+)$', myArgv, re.IGNORECASE)
            CiTestObj.ranBranch = matchReg.group(1)
            RAN.ranBranch=matchReg.group(1)
            HTML.ranBranch=matchReg.group(1)
            CONTAINERS.ranBranch=matchReg.group(1)
            CLUSTER.ranBranch=matchReg.group(1)
        elif re.match(r'^\-\-eNBCommitID=(.*)$|^\-\-ranCommitID=(.*)$', myArgv, re.IGNORECASE):
            if re.match(r'^\-\-eNBCommitID=(.*)$', myArgv, re.IGNORECASE):
                matchReg = re.match(r'^\-\-eNBCommitID=(.*)$', myArgv, re.IGNORECASE)
            else:
                matchReg = re.match(r'^\-\-ranCommitID=(.*)$', myArgv, re.IGNORECASE)
            CiTestObj.ranCommitID = matchReg.group(1)
            RAN.ranCommitID=matchReg.group(1)
            HTML.ranCommitID=matchReg.group(1)
            CONTAINERS.ranCommitID=matchReg.group(1)
            CLUSTER.ranCommitID=matchReg.group(1)
        elif re.match(r'^\-\-eNBTargetBranch=(.*)$|^\-\-ranTargetBranch=(.*)$', myArgv, re.IGNORECASE):
            if re.match(r'^\-\-eNBTargetBranch=(.*)$', myArgv, re.IGNORECASE):
                matchReg = re.match(r'^\-\-eNBTargetBranch=(.*)$', myArgv, re.IGNORECASE)
            else:
                matchReg = re.match(r'^\-\-ranTargetBranch=(.*)$', myArgv, re.IGNORECASE)
            CiTestObj.ranTargetBranch = matchReg.group(1)
            RAN.ranTargetBranch=matchReg.group(1)
            HTML.ranTargetBranch=matchReg.group(1)
            CONTAINERS.ranTargetBranch=matchReg.group(1)
            CLUSTER.ranTargetBranch=matchReg.group(1)
        elif re.match(r'^\-\-eNBIPAddress=(.+)$|^\-\-eNB[1-2]IPAddress=(.+)$', myArgv, re.IGNORECASE):
            print("parameters --eNB*IPAddress ignored")
        elif re.match(r'^\-\-eNBUserName=(.+)$|^\-\-eNB[1-2]UserName=(.+)$', myArgv, re.IGNORECASE):
            print("parameters --eNB*UserName ignored")
        elif re.match(r'^\-\-eNBPassword=(.+)$|^\-\-eNB[1-2]Password=(.+)$', myArgv, re.IGNORECASE):
            print("parameter --eNB*Password ignored")
        elif re.match(r'^\-\-eNBSourceCodePath=(.+)$|^\-\-eNB[1-2]SourceCodePath=(.+)$', myArgv, re.IGNORECASE):
            if re.match(r'^\-\-eNBSourceCodePath=(.+)$', myArgv, re.IGNORECASE):
                matchReg = re.match(r'^\-\-eNBSourceCodePath=(.+)$', myArgv, re.IGNORECASE)
                RAN.eNBSourceCodePath=matchReg.group(1)
                CONTAINERS.eNBSourceCodePath=matchReg.group(1)
                CLUSTER.eNBSourceCodePath=matchReg.group(1)
            elif re.match(r'^\-\-eNB1SourceCodePath=(.+)$', myArgv, re.IGNORECASE):
                print("parameter --eNB1SourceCodePath ignored")
            elif re.match(r'^\-\-eNB2SourceCodePath=(.+)$', myArgv, re.IGNORECASE):
                print("parameter --eNB2SourceCodePath ignored")
        elif re.match(r'^\-\-XMLTestFile=(.+)$', myArgv, re.IGNORECASE):
            matchReg = re.match(r'^\-\-XMLTestFile=(.+)$', myArgv, re.IGNORECASE)
            CiTestObj.testXMLfiles.append(matchReg.group(1))
            HTML.testXMLfiles.append(matchReg.group(1))
            HTML.nbTestXMLfiles=HTML.nbTestXMLfiles+1
        elif re.match(r'^\-\-finalStatus=(.+)$', myArgv, re.IGNORECASE):
            matchReg = re.match(r'^\-\-finalStatus=(.+)$', myArgv, re.IGNORECASE)
            finalStatus = matchReg.group(1)
            if ((finalStatus == 'true') or (finalStatus == 'True')):
                CiTestObj.finalStatus = True
        elif re.match(r'^\-\-OCUserName=(.+)$', myArgv, re.IGNORECASE):
            matchReg = re.match(r'^\-\-OCUserName=(.+)$', myArgv, re.IGNORECASE)
            CLUSTER.OCUserName = matchReg.group(1)
        elif re.match(r'^\-\-OCPassword=(.+)$', myArgv, re.IGNORECASE):
            matchReg = re.match(r'^\-\-OCPassword=(.+)$', myArgv, re.IGNORECASE)
            CLUSTER.OCPassword = matchReg.group(1)
        elif re.match(r'^\-\-OCProjectName=(.+)$', myArgv, re.IGNORECASE):
            matchReg = re.match(r'^\-\-OCProjectName=(.+)$', myArgv, re.IGNORECASE)
            CLUSTER.OCProjectName = matchReg.group(1)
        elif re.match(r'^\-\-OCUrl=(.+)$', myArgv, re.IGNORECASE):
            matchReg = re.match(r'^\-\-OCUrl=(.+)$', myArgv, re.IGNORECASE)
            CLUSTER.OCUrl = matchReg.group(1)
        elif re.match(r'^\-\-OCRegistry=(.+)$', myArgv, re.IGNORECASE):
            matchReg = re.match(r'^\-\-OCRegistry=(.+)$', myArgv, re.IGNORECASE)
            CLUSTER.OCRegistry = matchReg.group(1)
        elif re.match(r'^\-\-FlexRicTag=(.+)$', myArgv, re.IGNORECASE):
            matchReg = re.match(r'^\-\-FlexRicTag=(.+)$', myArgv, re.IGNORECASE)
            CONTAINERS.flexricTag = matchReg.group(1)
        else:
            HELP.GenericHelp(CONST.Version)
            sys.exit('Invalid Parameter: ' + myArgv)

    return mode, force_local, date_fmt
