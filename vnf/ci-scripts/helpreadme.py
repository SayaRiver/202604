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
# Functions Declaration
#-----------------------------------------------------------

def GenericHelp(vers):
	print('----------------------------------------------------------------------------------------------------------------------')
	print('main.py Ver: ' + vers)
	print('----------------------------------------------------------------------------------------------------------------------')
	print('python main.py [options]')
	print('  --help  Show this help.')
	print('  --mode=[Mode]')
	print('      TesteNB')
	print('      InitiateHtml, FinalizeHtml')
	print('      TerminateeNB, TerminateHSS, TerminateMME, TerminateSPGW')
	print('  --local Force local execution: rewrites the test xml script before running to always execute on localhost. Assumes')
	print('          images are available locally, will not remove any images and will run inside the current repo directory')

def GitSrvHelp(repository,branch,commit,mergeallow,targetbranch):
	print('  --ranRepository=[OAI RAN Repository URL]                                      -- ' + repository)
	print('  --ranBranch=[OAI RAN Repository Branch]                                       -- ' + branch)
	print('  --ranCommitID=[OAI RAN Repository Commit SHA-1]                               -- ' + commit)
	print('  --ranAllowMerge=[Allow Merge Request (with target branch) (true or false)]    -- ' + mergeallow)
	print('  --ranTargetBranch=[Target Branch in case of a Merge Request]                  -- ' + targetbranch)

def eNBSrvHelp(sourcepath):
	print('  --eNBSourceCodePath=[eNB\'s Source Code Path]            -- ' + sourcepath)

def XmlHelp(filename):
	print('  --XMLTestFile=[XML Test File to be run]                  -- ' + filename)
	print('	Note: multiple xml files can be specified (--XMLFile=File1 ... --XMLTestFile=FileN) when HTML headers are created ("InitiateHtml" mode)')

