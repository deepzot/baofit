#!/usr/bin/env python

# usage example: ./parsescan.py BOSSDR11QSOLyaF_scan.dat BOSSDR11QSOLyaF.scan 10 11

import sys

# expected command-line args are <infile> <outfile> <index1> <index2>
infile,outfile,index1,index2 = sys.argv[1:]

# check that the parameter indices are integers
try:
	index1,index2 = map(int,(index1,index2))
except ValueError, e:
	print 'indices should be integer'
	sys.exit(-1)

try:
	# open the files
	with open(infile,'r') as fin, open(outfile,'w') as fout:
		# read the header...
		# npar = total number of floating+fixed parameters
		# ndump = number of r values used to dump ell=0,2,4 multipoles for each fit
		# nfit = number of different fits performed (1 or 2)
		npar,ndump,nfit = map(int,fin.readline().split())
		# best-fit errors on each parameter from each fit
		errors = map(float,fin.readline().split())
		if len(errors) != npar*nfit:
			print 'unexpected length of header line 2'
			sys.exit(-1)
		# parameter values (and optional multipole dumps) from best fit and each scan point
		bestfit = map(float,fin.readline().split())
		if len(bestfit) != 1+npar+3*ndump:
			print 'unexpected length',len(bestfit),'of header line 3'
			sys.exit(-1)
		lineno = 3
		bestchisq = bestfit[npar]
		for line in fin.readlines():
			lineno += 1
			scanfit = map(float,line.split())
			if len(scanfit) != 1+npar+3*ndump:
				print 'unexpected length',len(scanfit),'of line',lineno
				sys.exit(-1)
			print >>fout, scanfit[index1],scanfit[index2],scanfit[npar]-bestchisq
		print 'parsed',lineno-3,'scan points'
except IOError,e:
	print str(e)
	sys.exit(-1)
