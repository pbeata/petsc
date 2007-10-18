#!/usr/bin/env python
from __future__ import generators
import user
import config.base
import os
import PETSc.package

class Configure(PETSc.package.Package):
  def __init__(self, framework):
    PETSc.package.Package.__init__(self, framework)
    self.download  = ['ftp://ftp.mcs.anl.gov/pub/petsc/externalpackages/blacs-dev.tar.gz']
    self.liblist   = [['libblacs.a']]
    self.includes  = []
    self.fc        = 1
    self.functions = ['blacs_pinfo']
    self.functionsFortran = 1
    self.complex   = 1
    return

  def setupDependencies(self, framework):

    PETSc.package.Package.setupDependencies(self, framework)
    self.mpi        = framework.require('config.packages.MPI',self)
    self.blasLapack = framework.require('config.packages.BlasLapack',self)
    self.deps       = [self.mpi,self.blasLapack]
    return

  def Install(self):

    g = open(os.path.join(self.packageDir,'Bmake.Inc'),'w')
    g.write('SHELL     = /bin/sh\n')
    g.write('COMMLIB   = MPI\n')
    g.write('SENDIS    = -DSndIsLocBlk\n')
    if (self.mpi.commf2c):
      g.write('WHATMPI      = -DUseMpi2\n')
    else:
      g.write('WHATMPI      = -DCSAMEF77\n')
    g.write('DEBUGLVL  = -DBlacsDebugLvl=1\n')
    g.write('BLACSdir  = '+self.packageDir+'\n')
    g.write('BLACSLIB  = '+os.path.join(self.installDir,self.libdir,'libblacs.a')+'\n')
    g.write('MPILIB    = '+self.libraries.toString(self.mpi.lib)+'\n')
    g.write('SYSINC    = '+self.headers.toString(self.mpi.include)+'\n')
    g.write('BTLIBS    = $(BLACSLIB)  $(MPILIB) \n')
    if self.compilers.fortranManglingDoubleUnderscore:
      blah = 'f77IsF2C'
    elif self.compilers.fortranMangling == 'underscore':
      blah = 'Add_'
    elif self.compilers.fortranMangling == 'capitalize':
      blah = 'UpCase'
    else:
      blah = 'NoChange'
    g.write('INTFACE   = -D'+blah+'\n')
    g.write('DEFS1     = -DSYSINC $(SYSINC) $(INTFACE) $(DEFBSTOP) $(DEFCOMBTOP) $(DEBUGLVL)\n')
    g.write('BLACSDEFS = $(DEFS1) $(SENDIS) $(BUFF) $(TRANSCOMM) $(WHATMPI) $(SYSERRORS)\n')
    self.setCompilers.pushLanguage('FC')  
    g.write('F77       = '+self.setCompilers.getCompiler()+'\n')
    g.write('F77FLAGS  = '+self.setCompilers.getCompilerFlags().replace('-Wall','').replace('-Wshadow','')+'\n')
    g.write('F77LOADER = '+self.setCompilers.getLinker()+'\n')      
    g.write('F77LOADFLAGS ='+self.setCompilers.getLinkerFlags()+'\n')
    self.setCompilers.popLanguage()     
    self.setCompilers.pushLanguage('C')
    g.write('CC          = '+self.setCompilers.getCompiler()+'\n')
    g.write('CCFLAGS     = '+self.setCompilers.getCompilerFlags().replace('-Wall','').replace('-Wshadow','')+'\n')      
    g.write('CCLOADER    = '+self.setCompilers.getLinker()+'\n')
    g.write('CCLOADFLAGS = '+self.setCompilers.getLinkerFlags()+'\n')
    self.setCompilers.popLanguage()
    g.write('ARCH        = '+self.setCompilers.AR+'\n')
    g.write('ARCHFLAGS   = '+self.setCompilers.AR_FLAGS+'\n')    
    g.write('RANLIB      = '+self.setCompilers.RANLIB+'\n')    
    g.close()

    if self.installNeeded('Bmake.Inc'):
      try:
        self.logPrintBox('Compiling Blacs; this may take several minutes')
        output  = config.base.Configure.executeShellCommand('cd '+os.path.join(self.packageDir,'SRC','MPI')+';make clean; make', timeout=2500, log = self.framework.log)[0]
      except RuntimeError, e:
        raise RuntimeError('Error running make on BLACS: '+str(e))
      self.checkInstall(output,'Bmake.Inc')
    return self.installDir

  def checkLib(self,lib,func,mangle,otherLibs = []):
    oldLibs = self.compilers.LIBS
    found = self.libraries.check(lib,func, otherLibs = otherLibs+self.mpi.lib+self.blasLapack.lib+self.compilers.flibs,fortranMangle=mangle)
    self.compilers.LIBS=oldLibs
    if found:
      self.framework.log.write('Found function '+str(func)+' in '+str(lib)+'\n')
    return found
  
  def configureLibraryOld(self): #almost same as package.py/configureLibrary()!
    '''Find an installation ando check if it can work with PETSc'''
    self.framework.log.write('==================================================================================\n')
    self.framework.log.write('Checking for a functional '+self.name+'\n')
    foundLibrary = 0
    foundHeader  = 0

    # get any libraries and includes we depend on
    libs         = []
    incls        = []
    for l in self.deps:
      if hasattr(l,'dlib'):    libs  += l.dlib
      if hasattr(l,self.includedir): incls += l.include
      
    for location, lib,incl in self.generateGuesses():
      if not isinstance(lib, list): lib = [lib]
      if not isinstance(incl, list): incl = [incl]
      self.framework.log.write('Checking for library '+location+': '+str(lib)+'\n')
      #if self.executeTest(self.libraries.check,[lib,self.functions],{'otherLibs' : libs}):
      if self.executeTest(self.checkLib,[lib,self.functions,1]):     
        self.lib = lib
        self.framework.log.write('Checking for headers '+location+': '+str(incl)+'\n')
        if (not self.includes) or self.executeTest(self.libraries.checkInclude, [incl, self.includes],{'otherIncludes' : incls}):
          self.include = incl
          self.found   = 1
          self.dlib    = self.lib+libs
          self.framework.packages.append(self)
          break
    if not self.found:
      raise RuntimeError('Could not find a functional '+self.name+'\n')


if __name__ == '__main__':
  import config.framework
  import sys
  framework = config.framework.Framework(sys.argv[1:])
  framework.setup()
  framework.addChild(Configure(framework))
  framework.configure()
  framework.dumpSubstitutions()
